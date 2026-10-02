#include "process.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace hls {
namespace {

class FileActions {
public:
    FileActions() { posix_spawn_file_actions_init(&actions_); }
    ~FileActions() { posix_spawn_file_actions_destroy(&actions_); }
    FileActions(const FileActions&) = delete;
    FileActions& operator=(const FileActions&) = delete;
    posix_spawn_file_actions_t* get() { return &actions_; }

private:
    posix_spawn_file_actions_t actions_;
};

}  // namespace

ProcessResult runProcess(const std::vector<std::string>& argv, const ProcessOptions& options) {
    if (argv.empty()) throw std::invalid_argument("runProcess: empty argv");

    std::vector<char*> cargv;
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    FileActions actions;
    posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(actions.get(), STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    if (options.stderrLog) {
        posix_spawn_file_actions_addopen(actions.get(), STDERR_FILENO,
                                         options.stderrLog->c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
    } else {
        posix_spawn_file_actions_addopen(actions.get(), STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, cargv[0], actions.get(), nullptr, cargv.data(), environ);
    if (rc != 0) throw std::runtime_error("cannot start '" + argv[0] + "': " + std::strerror(rc));

    // Poll rather than block in waitpid() so we can react to cancellation.
    ProcessResult result;
    bool killSent = false;
    int status = 0;
    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) break;

        if (!killSent && options.cancel && options.cancel->isCancelled()) {
            kill(pid, SIGTERM);
            killSent = true;
            result.cancelled = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.signalled = true;
    }
    return result;
}

}  // namespace hls

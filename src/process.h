#pragma once

// Minimal POSIX subprocess helper. Arguments are passed as argv, never through
// a shell, so file names need no quoting.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "encoder.h"

namespace hls {

struct ProcessResult {
    int exitCode = -1;      // valid when !signalled
    bool signalled = false; // killed by a signal (including our own cancel)
    bool cancelled = false; // we killed it because the CancelToken fired
};

struct ProcessOptions {
    std::optional<std::filesystem::path> stderrLog;  // otherwise discarded
    const CancelToken* cancel = nullptr;
};

// Runs argv[0] (looked up in PATH) to completion. stdin and stdout are /dev/null.
// Throws std::runtime_error if the process cannot be started.
ProcessResult runProcess(const std::vector<std::string>& argv, const ProcessOptions& options = {});

}  // namespace hls

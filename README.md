# hls_packager

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/hls_packager --input <source_video> --output <output_dir>
```

| Option | |
|---|---|
| `--input`, `-i` | Source video file |
| `--output`, `-o` | Output directory. Created if missing. The rendition sub-directories and `master.m3u8` are replaced on each run. |
| `--strict` | All-or-nothing: if any rendition fails, cancel the others and write no master playlist. The default publishes the renditions that succeeded. |

| Exit code | Meaning |
|---|---|
| `0` | All renditions packaged |
| `2` | Partial: some renditions failed, and `master.m3u8` lists only the ones that succeeded |
| `1` | Failed: bad arguments, invalid input, or no usable renditions |

Example output:

```
Packaging "clip.mp4" into "output" (3 renditions in parallel)...
  high: ok, 14 segments
  medium: ok, 14 segments
  low: ok, 14 segments
Result: success
Master playlist: output/master.m3u8
```

If a rendition fails, its ffmpeg log is kept at `<output>/<rendition>/ffmpeg.log`.

---

## Design questions

### 1. Extensibility: live input such as RTSP

#### 1. Encoder changes

ffmpeg reads the RTSP address directly in place of the input file. As in the current code, it forces a keyframe every 6 seconds and cuts a segment at each one. 
It is better not to use a ffmpeg process  separately for each rendition on RTSP.

The fix is **one ffmpeg process that reads the stream once and produces all three renditions**. 
ffmpeg's `split` filter copies the decoded video into three branches. 
Each branch is scaled and encoded at its own bitrate, and each writes to its own folder with its own CSV list. 
Every rendition comes from the same decoded frames, so their segments line up exactly.

The encoder interface changes from "encode one rendition" to "encode the whole ladder". The orchestrator starts one encoder per stream instead of one per rendition.

After starting ffmpeg, the program runs a loop that repeats every few hundred milliseconds:

1. **Check each rendition's CSV for new lines.** For every new line, add that segment to the rendition's playlist. 
If the playlist is now longer than the window (for example 5 segments), drop the oldest entry, increase `MEDIA-SEQUENCE` by 1, and delete that old `.ts` file. 
Then save the playlist (write to a temporary file and rename it).
2. **Check that ffmpeg is still running.** If it exited, usually because the camera disconnected, 
start it again and add `#EXT-X-DISCONTINUITY` before the next segment.
3. **Check the `CancelToken`.** If a stop was requested, stop ffmpeg, optionally add `#EXT-X-ENDLIST` to turn the 
stream into a finished recording, and leave the loop.

The master playlist is written once, before the loop starts, and doesn't change after that.

### 2. Error handling: one rendition fails mid-way

The code supports two policies, chosen per job through `Job::policy`.

**Best effort (default).** When one rendition fails, the others keep encoding. 
The failed rendition is reported with its error and left out of `master.m3u8`, 
so players only see renditions that are complete. The job ends as `Partial` and the CLI exits with code 2. 
The advantage is that the content can still be played; the downside is a gap in the quality ladder.

**All or nothing (`--strict`).** The first failure raises the shared `CancelToken`, 
the other ffmpeg processes are stopped, and no master playlist is written. 
The job ends as `Failed` and the CLI exits with code 1. 
The advantage is that you never publish an incomplete ladder; 
the downside is that nothing can be played until the job is run again.

In both cases a failure never crashes the job: each encode's errors are caught and returned as part of the report, 
and the master playlist is always written last, only from renditions that succeeded.

### 3. Scale: 50 concurrent jobs

(I assume we stay at 3 renditions per job).

**What breaks first:** each job starts three ffmpeg processes, so 50 jobs means 150 encoders running at once. One machine can't handle that: there isn't enough memory or CPU, and nothing in the current design limits how many run at the same time.

**What I would change:** a global job queue with a limited number of workers.

- The CLI no longer runs the job itself. It adds the job to a global queue.
- Measure how many ffmpeg processes the machine can run at once (its capacity). Each job needs one process per rendition, so the number of workers is the capacity divided by the renditions per job (with 3 renditions per job, a machine that can run 15 processes gets 5 workers).
- Each worker takes one job from the queue and runs it until it finishes (`Packager::run`), then takes the next one. Jobs beyond the number of workers wait in the queue, so the machine is never asked to run more processes than it can handle.

### 4. ABR ladder integrity: the medium rendition fails

The code currently lists the ladder in order, high first. That's risky: every viewer starts on 1080p at about 4.6 Mbps, and anyone on a slower connection stalls before the player switches down. If medium fails, the next step down is a big jump to 360p, so recovering from that bad start is even harsher.

Better ordering: put a middle variant first (medium), which starts reliably on most connections and lets the player move up quickly, then list the rest from low to high. If medium failed, put low first instead. Starting at a lower quality and moving up is a much better experience than starting too high and stalling.

---

## How the output was verified

- Ran the CLI on several MP4 files I had.
- Played the result with `ffplay output/master.m3u8` to confirm the output is valid, playable video.
- Examined the generated playlists (`master.m3u8` and each `index.m3u8`) to check the tags, segment durations, `BANDWIDTH` and `RESOLUTION` values.

---

## What I would improve with more time

- **Decode once:** one ffmpeg with a `split` filter and three outputs, as a second `IEncoder`
  implementation.
- **Retries and a ladder-integrity policy** for partial failures (question 4).
- **Automated tests:** unit tests for the playlist writer, and orchestrator tests using a fake
  `IEncoder` to exercise failure, `--strict` cancellation and the duration check without ffmpeg,
  plus an end-to-end test on a short generated clip. 
- **Progress reporting** using ffmpeg's `-progress` output, and a timeout per encode.
# dhpool: a real-time job pool for audio plugins

This document covers how to use dhpool, its rules, and its version. dhpool is plain C++17, with no JUCE and no dependency on the plugin that uses it. Cymbalis owns it. Tympanis, and the kit plugin that merges the two, take a copy.

## Version

**Version 1** (`dhpool::kVersion`), first in Cymbalis on branch `claude/mt-job-pool`, multi-threading step 2 (2026-11-25).

A change that alters behaviour or the interface bumps `kVersion` and this line.

## Taking a copy

- **Copy the whole `shared/dhpool/` folder unchanged** into the other project, keeping its own `CMakeLists.txt`.
- **Record where it came from:** the Cymbalis commit it was copied at, and `kVersion`, for example in a file next to it or in the commit message that adds it.
- **Build it:** `add_subdirectory(path/to/dhpool)` and link the target `dhpool`. The self-test target `dhpool_selftest` comes with it; `-DDHPOOL_BUILD_TESTS=OFF` leaves it out.

**The rule: change dhpool only in Cymbalis.** Fix or extend it there, then copy the folder again at the new commit. Never edit a copy, so that every project runs the same, tested pool.

## Use

```cpp
#include "JobPool.h"

dhpool::JobPool pool;

// Off the audio path (prepareToPlay): the calling thread included, 3 threads.
dhpool::JobPool::Options o;
o.threads = 3;
o.periodMs = 1000.0 * maxBlock / sampleRate; // the workers' time-constraint period: one block
pool.prepare(o);

// Off the audio path, at any time (JUCE: audioWorkgroupContextChanged):
pool.setWorkgroupHook([this](int w) { /* join the workgroup, keep worker w's token */ },
                      [this](int w) { /* leave it: reset worker w's token */ });

// On the audio path, per block: the caller owns the buffers, the order and the mix.
pool.run(count, order, &renderOne, this); // calls renderOne(this, order[k]) for k = 0 .. count - 1
// ... serial work on the caller (a mix, a coupling pass) ...
pool.run(count2, order2, &renderOther, this); // a second run in the same block
```

- **`run(count, order, fn, ctx)`** calls `fn(ctx, order[k])` for every k, or `fn(ctx, k)` with `order` nullptr.
  - **Who runs the jobs:** the calling thread and the workers. The call returns when every job has returned.
  - **Order:** jobs are handed out in the order given, so put the heaviest first.
  - **Small runs:** with `count < 2` or one thread, the caller runs them alone and wakes no worker.
  - **`fn`** is a plain function pointer, so nothing is allocated.
- **Several runs per block.** After its last job a worker spins for `spinMicros` (default 50 µs), so a run that comes soon after, in the same block, finds the workers awake. A worker that has spun that long sleeps until the next run wakes it.
- **The workgroup.**
  - `setWorkgroupHook` publishes a new pair of hooks and bumps a generation.
  - Each worker leaves the hook it joined and joins the new one at its next wake-up, before it takes a job. At its start a worker joins the current hook; at its end it leaves it.
  - Hooks run on worker threads only, never on the calling thread.
  - Empty functions mean no workgroup.
- **Stats** (`stats()`, `wakeDelaysMicros()`, `callerWaitsMicros()`, `workerCpuSeconds()`), read off the audio path with no run in progress:
  - per worker: wakes, wakes from sleep, jobs, workgroup joins;
  - with `timingSamples > 0`: every wake-up's delay, from dispatch to the worker starting;
  - with `timingSamples > 0`: every shared run's caller wait, from the counter running out to the last job still on a worker returning. This is the cost of a worker being slow or preempted mid-job.

## Rules

1. **No lock, allocation or system call on the audio path** but the wake-up signal (a Mach semaphore on macOS, a POSIX semaphore on Linux).
   - `prepare`, `release`, `setWorkgroupHook` and the stats calls are off the audio path.
   - Jobs must follow the same rule.
2. **One caller at a time.** `run()` is not reentrant: never call it from inside a job.
3. **A job must not depend on which thread runs it, or when.**
   - The pool hands each job the caller's floating-point control (flush to zero, rounding). It keeps nothing else per thread.
   - Jobs of one run must not write to the same memory. Results that are summed belong to the caller, in a fixed order, after `run()` returns. That keeps the output bit-identical at any number of threads.
4. **At most `kMaxJobs` (4095) jobs per run.**
5. **The workers' real-time policy:**
   - with `realtime` and `periodMs > 0`, a time-constraint policy on macOS: period and constraint `periodMs`, computation half of it;
   - SCHED_FIFO on Linux where permitted.

   Call `JobPool::makeRealtime` on a thread of your own to give it the same policy.

## How it works

- **The hand-out.** One atomic counter holds the run's generation (40 bits), its job count (12) and the next job (12). A worker takes a job only by a compare-and-swap that still carries the generation it woke for, so a late worker never takes a job of another run. The caller publishes `fn`, `ctx`, `order` and its floating-point control before the counter, and starts no new run until every job is done.
- **Sleeping.** A worker sets its flag, then looks at the counter once more; the caller stores the counter, then exchanges each flag. All four operations are sequentially consistent, so either the worker sees the new run or the caller sees the flag and signals. A worker that saw the run itself after setting its flag takes the caller's signal at once, so no wake-up is lost or left over.
- **Shutdown.** `release()` and the destructor set the stop flag, signal every sleeping worker and join them all. Each worker leaves its workgroup first. Workgroup hooks no worker holds any more are freed then too.

## Self-test

`dhpool_selftest [--quick]` runs dummy jobs: deterministic arithmetic of varied cost that makes denormals, at 1 to 4 threads. It checks:

- every job runs exactly once per run, for counts of 0 to 200 and 4095, in random orders;
- the results are bit-identical at every thread count, with flush to zero on and off. The workers start in the other mode, so only the hand-over gives them the caller's;
- two runs per block with serial passes between them;
- a workgroup hook change reaches every worker and never the calling thread, and every join has its leave;
- prepare / release cycles leave no thread behind.

It exits with status 1 if any check fails. In Cymbalis, CI runs it, and a ThreadSanitizer build of it on Linux reports no data race (`docs/PORT_MAP.md`, "Multi-cymbal rendering (step 2)").

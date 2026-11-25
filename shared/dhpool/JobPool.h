// JobPool.h — dhpool: a real-time job pool for audio plugins (README-pool.md).
//
// Plain C++17, no JUCE, nothing from the plugin that uses it. Cymbalis owns it; other projects
// (Tympanis) take a copy of this folder unchanged, at a recorded commit.
//
// The pool only schedules. run(count, order, fn, ctx) calls fn(ctx, order[k]) for every k in
// 0 .. count - 1, on the calling thread and on worker threads, and returns when every call has
// returned. The caller owns everything else: the events, the buffers, the mix and any serial
// work between runs (a coupling pass, say). It may call run() several times in one block.
//
// The hand-out. One atomic counter hands the jobs out in the order given (heaviest first is the
// caller's to choose). The counter carries the run's generation, so a worker that wakes late
// never takes a job of another run. The calling thread takes jobs too; once the counter is empty
// it waits only for jobs already in progress on workers. With count < 2 or one thread, run()
// calls fn on the calling thread alone and wakes no worker; otherwise it wakes as many workers
// as there are jobs to share, up to threads - 1.
//
// Floating point. Each run hands the caller's floating-point control (flush to zero, rounding)
// to the workers, so a job computes the same bits on any thread.
//
// The workers (threads - 1) are made in prepare(), never on the audio path. With realtime and a
// period they run with a time-constraint policy (macOS: period and constraint one period,
// computation half of it; Linux: SCHED_FIFO where permitted). After its last job of a run a
// worker spins for spinMicros (so a run soon after, in the same block, finds it awake), then
// sleeps on a semaphore of its own (macOS: a Mach semaphore; Linux: a POSIX semaphore), which the
// caller signals with no lock and no allocation.
//
// The audio workgroup. setWorkgroupHook(join, leave), off the audio path and at any time (a
// host may change its workgroup more than once), publishes a new hook and bumps a generation.
// Each worker leaves the hook it joined and joins the new one at its next wake-up, before it
// takes a job; at its start it joins the current hook, at its end it leaves it. The calling
// thread never joins.
//
// Audio path (run): no lock, no allocation, no system call but the wake-up signal.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dhpool
{

constexpr int kVersion = 1; // README-pool.md, "Version"

class JobPool
{
public:
    using Fn = void (*)(void* ctx, int job);
    using Hook = std::function<void(int worker)>;

    static constexpr int kMaxJobs = 4095; // per run
    static constexpr double kDefaultSpinMicros = 50.0;

    struct Options
    {
        int threads = 1;                         // the calling thread included
        double spinMicros = kDefaultSpinMicros;  // a worker's spin before it sleeps
        bool realtime = true;                    // the workers' time-constraint policy, with periodMs
        double periodMs = 0.0;                   // the policy's period: one block (0: no policy)
        int timingSamples = 0;                   // wake-up delays and caller waits kept (0: none, no clock read)
    };

    JobPool();
    ~JobPool();
    JobPool(const JobPool&) = delete;
    JobPool& operator=(const JobPool&) = delete;

    // Stops any previous workers and starts threads - 1 new ones. Allocates; off the audio path.
    void prepare(const Options& options);
    // Stops and joins the workers (each leaves its workgroup first). Also done by the destructor
    // and by prepare(). Off the audio path.
    void release();

    int threads() const noexcept { return 1 + numWorkers_; }

    // Audio path: calls fn(ctx, order[k]) (order nullptr: fn(ctx, k)) for k = 0 .. count - 1
    // (at most kMaxJobs), on this thread and the workers; returns when all have returned.
    // Not reentrant: one caller at a time, and not from inside a job.
    void run(int count, const int* order, Fn fn, void* ctx) noexcept;

    // Off the audio path, at any time (not concurrently with prepare or release): the hooks that
    // join and leave the host's audio workgroup, each called on a worker thread with the
    // worker's index. Empty functions: no workgroup. They stay set across prepare and release.
    void setWorkgroupHook(Hook join, Hook leave);

    // Counts since prepare() or resetStats(); read and reset them off the audio path, with no
    // run() in progress.
    struct WorkerStats
    {
        uint64_t wakes = 0;          // runs the worker took part in
        uint64_t wakesFromSleep = 0; // of them, those it was asleep for
        uint64_t jobs = 0;           // jobs it ran
        uint64_t workgroupJoins = 0; // workgroup hooks it joined (its start included)
        double jobSeconds = 0.0;     // time spent in its jobs (with timingSamples > 0)
        bool realtime = false;       // its time-constraint policy took
    };
    struct Stats
    {
        uint64_t runs = 0;        // run() calls
        uint64_t sharedRuns = 0;  // of them, those the workers were woken for
        uint64_t callerJobs = 0;  // jobs the caller ran in shared runs
        std::vector<WorkerStats> workers;
    };
    Stats stats() const;
    void resetStats() noexcept;
    // With timingSamples > 0: every wake-up's delay (dispatch to the worker starting, all
    // workers), and every shared run's caller wait (from the counter running out to the last
    // job in progress on a worker returning), in microseconds.
    std::vector<float> wakeDelaysMicros() const;
    std::vector<float> callerWaitsMicros() const;
    // The workers' CPU time since they started, spinning included (asks the system).
    double workerCpuSeconds() const;
    // Waits until every worker is asleep, at most timeoutMs (off the audio path).
    bool waitIdle(double timeoutMs) const;

    // The calling thread as a host's audio thread: real-time with periodMs as its period
    // (macOS: time-constraint policy; Linux: SCHED_FIFO where permitted). Not on the audio path.
    static bool makeRealtime(double periodMs) noexcept;

private:
    struct Worker;
    struct HookPair
    {
        Hook join, leave;
    };

    int work(uint64_t gen, Worker* worker) noexcept;
    void workerMain(int index) noexcept;
    void switchHook(Worker& w, int index) noexcept;
    static void signal(Worker& w) noexcept;
    static void wait(Worker& w) noexcept;

    Options options_;
    int numWorkers_ = 0;
    std::vector<std::unique_ptr<Worker>> workers_;

    // The run's hand-out counter: generation (40 bits), jobs (12), the next one (12).
    alignas(128) std::atomic<uint64_t> ticket_ { 0 };
    alignas(128) std::atomic<int> done_ { 0 };
    std::atomic<int64_t> dispatchNs_ { 0 };
    std::atomic<bool> stop_ { false };
    uint64_t gen_ = 0;
    // The run, written before the counter and read by a worker after it took a job.
    Fn fn_ = nullptr;
    void* ctx_ = nullptr;
    const int* order_ = nullptr;
    uint64_t fpMode_ = 0;

    // Workgroup hooks: every pair set is kept (stable addresses) until release() or the next
    // set after it; workers read only the published pointer.
    std::mutex hookMutex_; // setters only; never taken by run() or a worker
    std::vector<std::unique_ptr<HookPair>> hooks_;
    std::atomic<const HookPair*> hook_ { nullptr };
    std::atomic<uint64_t> hookGen_ { 0 };

    // Caller-side counts and caller waits (audio thread only; read off it).
    uint64_t runs_ = 0, sharedRuns_ = 0, callerJobs_ = 0;
    std::vector<float> callerWaitUs_;
    std::atomic<int> callerWaitCount_ { 0 };
};

} // namespace dhpool

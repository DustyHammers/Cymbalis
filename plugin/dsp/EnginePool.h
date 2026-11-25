// EnginePool.h — several FastEngines rendering one block together, on the calling thread and
// on worker threads (docs/PORT_MAP.md, "Multi-cymbal rendering (step 1)"). The plugin does not
// use it yet; cymbalis_bench --kit does.
//
// Per block the caller queues each engine's events (strikes, pedal), then render() renders
// every engine into its own buffer, split at its events as the plugin splits at MIDI events,
// and mixes the buffers in engine order (0, 1, 2 ...). The mix is bit-identical at any number
// of threads: each engine keeps its own state and random sequence, an engine is rendered by
// one thread at a time, the workers take the caller's floating-point mode (flush to zero), and
// the sum is always formed in the same order.
//
// Who renders what. The engines are handed out from one atomic counter, heaviest first:
// those struck in the block (by their number of strikes, then their ringing modes), then
// those still ringing (by their ringing modes: FastEngine::activeModes, every mode during a
// contact), then the silent ones. The calling thread takes engines from the counter too, so it
// never waits for work it could do itself: once the counter is empty it waits only for the
// engines still in progress on the workers. When fewer than two engines need rendering (struck
// or not silent), the caller renders the block alone and no worker is woken; otherwise it wakes
// as many workers as there are engines to share, up to threads − 1.
//
// The workers (threads − 1 of them) are made in prepare(), never on the audio path, with the
// time-constraint policy cymbalis_bench --dense gives its own thread (macOS: period and
// constraint one block, computation half of it; Linux: SCHED_FIFO where permitted). An
// optional hook joins each worker to the host's audio workgroup (JUCE's AudioWorkgroup, a later
// step); it is called once on each worker thread at its start, and its counterpart at its end.
// Between blocks a worker spins for Options::spinMicros, then sleeps on a semaphore of its own
// (macOS: a Mach semaphore; Linux: a POSIX semaphore), which the caller signals without a lock
// or an allocation.
//
// Audio path (strike, pedal, render): no lock, no allocation, no system call but the
// semaphore's signal. prepare() and release() allocate, start and stop the threads.
#pragma once

#include "FastEngine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace cymbalis
{

class EnginePool
{
public:
    // Default spin before a worker sleeps: about twice what waking a sleeping real-time thread
    // costs on macOS, and a small part of the shortest block (667 us at 96 kHz / 64).
    static constexpr double kDefaultSpinMicros = 50.0;
    static constexpr int kMaxEvents = 128; // per engine and block
    static constexpr int kMaxEngines = 64;

    struct Options
    {
        int threads = 1; // the calling thread included
        double spinMicros = kDefaultSpinMicros;
        bool realtime = true; // the workers' time-constraint policy
        // Called on each worker thread at its start (after its policy is set) and at its end:
        // to join and leave the host's audio workgroup. May be empty.
        std::function<void(int worker)> joinWorkgroup, leaveWorkgroup;
        // Wake-up delays kept per worker for wakeDelaysMicros() (0: none, and no clock is read
        // for them on the audio path).
        int wakeSamples = 0;
    };

    EnginePool();
    ~EnginePool();
    EnginePool(const EnginePool&) = delete;
    EnginePool& operator=(const EnginePool&) = delete;

    // The engines (prepared at sampleRate, not owned; at most kMaxEngines) and the largest
    // block. Stops any workers of a previous prepare() and starts threads − 1 new ones.
    // Allocates; never on the audio path.
    void prepare(const std::vector<FastEngine*>& engines, double sampleRate, int maxBlock, const Options& options);
    // Stops and joins the workers. Also done by the destructor and by prepare().
    void release();

    int numEngines() const noexcept { return numEngines_; }
    int threads() const noexcept { return 1 + numWorkers_; }
    int maxBlock() const noexcept { return maxBlock_; }

    // The next render()'s events for one engine, at a sample offset into the block, in order
    // (offsets never decreasing). false: the engine's queue is full (kMaxEvents).
    bool strike(int engine, int offset, int slot, double velocity, double position) noexcept;
    bool pedal(int engine, int offset, double openness) noexcept;

    // Renders every engine for numSamples (at most maxBlock) with its queued events, mixes them
    // in engine order into left and right (overwritten), and clears the queues.
    void render(float* left, float* right, int numSamples) noexcept;

    // One engine's output in the last render().
    const float* engineLeft(int engine) const noexcept { return bufL_[size_t(engine)].data(); }
    const float* engineRight(int engine) const noexcept { return bufR_[size_t(engine)].data(); }

    // Counts since prepare() or resetStats(). Read and reset them off the audio path, with no
    // render() running.
    struct WorkerStats
    {
        uint64_t wakes = 0;          // blocks the worker took part in
        uint64_t wakesFromSleep = 0; // of them, those it was asleep for
        uint64_t jobs = 0;           // engines it rendered
        double renderSeconds = 0.0;  // time spent rendering them (with wakeSamples > 0)
        bool realtime = false;       // its time-constraint policy took
    };
    struct Stats
    {
        uint64_t blocks = 0;        // render() calls
        uint64_t sharedBlocks = 0;  // of them, those the workers were woken for
        uint64_t callerJobs = 0;    // engines the caller rendered in shared blocks
        std::vector<WorkerStats> workers;
    };
    Stats stats() const;
    void resetStats() noexcept;
    // Every wake-up's delay (dispatch to the worker starting), all workers, in microseconds.
    std::vector<float> wakeDelaysMicros() const;
    // The CPU time the workers have used since they started, spinning included (off the audio
    // path; it asks the system).
    double workerCpuSeconds() const;
    // Waits (off the audio path) until every worker is asleep, at most timeoutMs.
    bool waitIdle(double timeoutMs) const;

    // The calling thread as a host's audio thread: real-time, the block as its period (macOS:
    // time-constraint policy; Linux: SCHED_FIFO where permitted). Not on the audio path.
    static bool makeRealtime(double periodMs) noexcept;

private:
    struct Event
    {
        int offset = 0;
        int slot = -1; // -1: the pedal
        double value = 0.0, position = 0.0;
    };
    struct Queue
    {
        std::array<Event, kMaxEvents> events {};
        int count = 0, strikes = 0;
    };
    struct Worker;

    void renderEngine(int engine) noexcept;
    int work(uint32_t gen, Worker* worker) noexcept; // takes engines from the counter until it is empty
    void workerMain(int index) noexcept;
    void signal(Worker& w) noexcept;
    void wait(Worker& w) noexcept;

    std::vector<FastEngine*> engines_;
    int numEngines_ = 0, numWorkers_ = 0, maxBlock_ = 0;
    double periodMs_ = 0.0;
    Options options_;
    std::vector<Queue> queues_;
    std::vector<std::vector<float>> bufL_, bufR_;
    std::vector<int> order_; // the engines in hand-out order (this block)
    std::vector<std::unique_ptr<Worker>> workers_;

    // The block's hand-out counter: generation (32 bits), engines to hand out (16), the next
    // one (16). A worker takes an engine only with the generation it woke for.
    alignas(128) std::atomic<uint64_t> ticket_ { 0 };
    alignas(128) std::atomic<int> done_ { 0 };
    std::atomic<int64_t> dispatchNs_ { 0 };
    std::atomic<bool> stop_ { false };
    uint32_t gen_ = 0;
    int blockSamples_ = 0;  // this block's length (written before the ticket)
    uint64_t fpMode_ = 0;   // the caller's floating-point control (written before the ticket)

    // Caller-side counts (audio thread only, read by stats() off it).
    uint64_t blocks_ = 0, sharedBlocks_ = 0, callerJobs_ = 0;
};

} // namespace cymbalis

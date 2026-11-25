// EnginePool.h — several FastEngines rendering one block together, on the calling thread and
// on worker threads (docs/PORT_MAP.md, "Multi-cymbal rendering", steps 1 and 2). The plugin
// does not use it yet; cymbalis_bench --kit does.
//
// Per block the caller queues each engine's events (strikes, pedal), then render() renders
// every engine into its own buffer, split at its events as the plugin splits at MIDI events,
// and mixes the buffers in engine order (0, 1, 2 ...). The mix is bit-identical at any number
// of threads, and to #74's EnginePool: each engine keeps its own state and random sequence, an
// engine is rendered by one thread at a time, the workers take the caller's floating-point mode
// (flush to zero), and the sum is always formed in the same order.
//
// Since step 2 the scheduling and the threads are dhpool's (shared/dhpool/JobPool.h); this class
// keeps what is Cymbalis's: the events, the buffers, the hand-out order and the mix.
//
// The hand-out order: those struck in the block (by their number of strikes, then their ringing
// modes), then those still ringing (by their ringing modes: FastEngine::activeModes, every mode
// during a contact). They are one dhpool run, which the calling thread works on too. The silent
// ones (about 1 us a block) the caller renders itself after the run. When fewer than two
// engines need rendering (struck or not silent), the caller renders the block alone and no
// worker is woken; otherwise the run wakes as many workers as there are engines to share, up to
// threads − 1.
//
// The workers (threads − 1 of them) are dhpool's: made in prepare(), never on the audio path,
// with the time-constraint policy cymbalis_bench --dense gives its own thread (period and
// constraint one block); between blocks they spin for Options::spinMicros, then sleep. The
// workgroup hook can be set at prepare() or changed at any time off the audio path
// (setWorkgroupHook): each worker leaves the old workgroup and joins the new one at its next
// wake-up.
//
// Audio path (strike, pedal, render): no lock, no allocation, no system call but the
// wake-up signal. prepare() and release() allocate, start and stop the threads.
#pragma once

#include "FastEngine.h"
#include "JobPool.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace cymbalis
{

class EnginePool
{
public:
    // Default spin before a worker sleeps (dhpool's): about twice what waking a sleeping
    // real-time thread costs on macOS, and a small part of the shortest block (667 us at 96 kHz / 64).
    static constexpr double kDefaultSpinMicros = dhpool::JobPool::kDefaultSpinMicros;
    static constexpr int kMaxEvents = 128; // per engine and block
    static constexpr int kMaxEngines = 64;

    struct Options
    {
        int threads = 1; // the calling thread included
        double spinMicros = kDefaultSpinMicros;
        bool realtime = true; // the workers' time-constraint policy
        // Called on each worker thread to join and leave the host's audio workgroup (may be
        // empty); the same as setWorkgroupHook() at prepare().
        std::function<void(int worker)> joinWorkgroup, leaveWorkgroup;
        // Wake-up delays and caller waits kept for wakeDelaysMicros() and callerWaitsMicros()
        // (0: none, and no clock is read for them on the audio path).
        int wakeSamples = 0;
    };

    EnginePool() = default;
    ~EnginePool() = default;
    EnginePool(const EnginePool&) = delete;
    EnginePool& operator=(const EnginePool&) = delete;

    // The engines (prepared at sampleRate, not owned; at most kMaxEngines) and the largest
    // block. Stops any workers of a previous prepare() and starts threads − 1 new ones.
    // Allocates; never on the audio path.
    void prepare(const std::vector<FastEngine*>& engines, double sampleRate, int maxBlock, const Options& options);
    // Stops and joins the workers. Also done by the destructor and by prepare().
    void release() { pool_.release(); }

    // Off the audio path, at any time: the host's audio workgroup hooks (dhpool's).
    void setWorkgroupHook(std::function<void(int worker)> join, std::function<void(int worker)> leave)
    {
        pool_.setWorkgroupHook(std::move(join), std::move(leave));
    }

    int numEngines() const noexcept { return numEngines_; }
    int threads() const noexcept { return pool_.threads(); }
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
        uint64_t callerJobs = 0;    // engines the caller rendered in shared blocks (silent ones included)
        std::vector<WorkerStats> workers;
    };
    Stats stats() const;
    void resetStats() noexcept;
    // With wakeSamples > 0: every wake-up's delay (dispatch to the worker starting, all
    // workers), and every shared block's caller wait (from the counter running out to the last
    // engine in progress on a worker finishing), in microseconds.
    std::vector<float> wakeDelaysMicros() const { return pool_.wakeDelaysMicros(); }
    std::vector<float> callerWaitsMicros() const { return pool_.callerWaitsMicros(); }
    // The CPU time the workers have used since they started, spinning included (off the audio
    // path; it asks the system).
    double workerCpuSeconds() const { return pool_.workerCpuSeconds(); }
    // Waits (off the audio path) until every worker is asleep, at most timeoutMs.
    bool waitIdle(double timeoutMs) const { return pool_.waitIdle(timeoutMs); }

    // The calling thread as a host's audio thread: real-time, the block as its period (macOS:
    // time-constraint policy; Linux: SCHED_FIFO where permitted). Not on the audio path.
    static bool makeRealtime(double periodMs) noexcept { return dhpool::JobPool::makeRealtime(periodMs); }

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

    void renderEngine(int engine) noexcept;
    static void renderJob(void* self, int engine) noexcept;

    std::vector<FastEngine*> engines_;
    int numEngines_ = 0, maxBlock_ = 0;
    std::vector<Queue> queues_;
    std::vector<std::vector<float>> bufL_, bufR_;
    std::vector<int> order_; // the engines in hand-out order (this block)
    int blockSamples_ = 0;   // this block's length (written before the run)

    // Caller-side counts (audio thread only, read by stats() off it).
    uint64_t blocks_ = 0, sharedBlocks_ = 0, silentJobs_ = 0;

    // Last, so its workers stop before the buffers they render into go.
    dhpool::JobPool pool_;
};

} // namespace cymbalis

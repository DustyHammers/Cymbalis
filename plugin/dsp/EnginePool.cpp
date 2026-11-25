// EnginePool.cpp — see EnginePool.h and docs/PORT_MAP.md, "Multi-cymbal rendering (step 1)".
#include "EnginePool.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#else
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <time.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace cymbalis
{
namespace
{
inline void cpuPause() noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#endif
}

inline int64_t nowNs() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The floating-point control that changes results (flush to zero, rounding), without the
// sticky exception flags.
inline uint64_t fpControl() noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
    return uint64_t(_mm_getcsr() & ~0x3Fu);
#elif defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
#else
    return 0;
#endif
}

inline void setFpControl(uint64_t m) noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr((_mm_getcsr() & 0x3Fu) | unsigned(m));
#elif defined(__aarch64__)
    asm volatile("msr fpcr, %0" : : "r"(m));
#else
    (void) m;
#endif
}

inline uint32_t genOf(uint64_t ticket) noexcept { return uint32_t(ticket >> 32); }
} // namespace

struct EnginePool::Worker
{
    alignas(128) std::atomic<int> sleeping { 0 }; // 1: asleep or about to be; the waker clears it
    std::thread thread;
    std::atomic<bool> started { false }, realtime { false };
#if defined(__APPLE__)
    semaphore_t sem = 0;
    std::atomic<unsigned> port { 0 }; // the thread's Mach port (CPU time)
#else
    sem_t sem {};
    clockid_t clock {};
    std::atomic<bool> hasClock { false };
#endif
    // Stats: written by the worker, read off the audio path.
    std::atomic<uint64_t> wakes { 0 }, wakesFromSleep { 0 }, jobs { 0 }, renderNs { 0 };
    std::vector<float> wakeUs;
    std::atomic<int> wakeCount { 0 };
};

EnginePool::EnginePool() = default;

EnginePool::~EnginePool()
{
    release();
}

bool EnginePool::makeRealtime(double periodMs) noexcept
{
#if defined(__APPLE__)
    mach_timebase_info_data_t base {};
    mach_timebase_info(&base);
    const auto ticks = [&](double ms) { return uint32_t(ms * 1.0e6 * double(base.denom) / double(base.numer)); };
    thread_time_constraint_policy_data_t policy {};
    policy.period = ticks(periodMs);
    policy.computation = ticks(0.5 * periodMs);
    policy.constraint = ticks(periodMs);
    policy.preemptible = 1;
    return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                             thread_policy_t(&policy), THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
#elif defined(__linux__)
    (void) periodMs;
    sched_param param {};
    param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
#else
    (void) periodMs;
    return false;
#endif
}

void EnginePool::prepare(const std::vector<FastEngine*>& engines, double sampleRate, int maxBlock, const Options& options)
{
    release();
    numEngines_ = int(std::min<size_t>(engines.size(), kMaxEngines));
    engines_.assign(engines.begin(), engines.begin() + numEngines_);
    maxBlock_ = std::max(1, maxBlock);
    periodMs_ = 1000.0 * maxBlock_ / sampleRate;
    options_ = options;
    queues_.assign(size_t(numEngines_), Queue {});
    bufL_.assign(size_t(numEngines_), std::vector<float>(size_t(maxBlock_)));
    bufR_.assign(size_t(numEngines_), std::vector<float>(size_t(maxBlock_)));
    order_.assign(size_t(numEngines_), 0);
    blocks_ = sharedBlocks_ = callerJobs_ = 0;

    numWorkers_ = std::max(0, options.threads - 1);
    stop_.store(false);
    ticket_.store(uint64_t(gen_) << 32);
    workers_.clear();
    for (int i = 0; i < numWorkers_; ++i)
    {
        auto w = std::make_unique<Worker>();
#if defined(__APPLE__)
        semaphore_create(mach_task_self(), &w->sem, SYNC_POLICY_FIFO, 0);
#else
        sem_init(&w->sem, 0, 0);
#endif
        w->wakeUs.assign(size_t(std::max(0, options.wakeSamples)), 0.0f);
        workers_.push_back(std::move(w));
    }
    for (int i = 0; i < numWorkers_; ++i)
        workers_[size_t(i)]->thread = std::thread(&EnginePool::workerMain, this, i);
    for (auto& w : workers_)
        while (!w->started.load(std::memory_order_acquire))
            std::this_thread::yield();
}

void EnginePool::release()
{
    if (workers_.empty())
        return;
    stop_.store(true);
    for (auto& w : workers_)
        if (w->sleeping.exchange(0) != 0)
            signal(*w);
    for (auto& w : workers_)
        if (w->thread.joinable())
            w->thread.join();
    for (auto& w : workers_)
    {
#if defined(__APPLE__)
        semaphore_destroy(mach_task_self(), w->sem);
#else
        sem_destroy(&w->sem);
#endif
    }
    workers_.clear();
    numWorkers_ = 0;
}

void EnginePool::signal(Worker& w) noexcept
{
#if defined(__APPLE__)
    semaphore_signal(w.sem);
#else
    sem_post(&w.sem);
#endif
}

void EnginePool::wait(Worker& w) noexcept
{
#if defined(__APPLE__)
    while (semaphore_wait(w.sem) == KERN_ABORTED)
    {
    }
#else
    while (sem_wait(&w.sem) != 0 && errno == EINTR)
    {
    }
#endif
}

bool EnginePool::strike(int engine, int offset, int slot, double velocity, double position) noexcept
{
    if (engine < 0 || engine >= numEngines_)
        return false;
    Queue& q = queues_[size_t(engine)];
    if (q.count >= kMaxEvents)
        return false;
    q.events[size_t(q.count++)] = { offset, slot, velocity, position };
    ++q.strikes;
    return true;
}

bool EnginePool::pedal(int engine, int offset, double openness) noexcept
{
    if (engine < 0 || engine >= numEngines_)
        return false;
    Queue& q = queues_[size_t(engine)];
    if (q.count >= kMaxEvents)
        return false;
    q.events[size_t(q.count++)] = { offset, -1, openness, 0.0 };
    return true;
}

void EnginePool::renderEngine(int engine) noexcept
{
    FastEngine& e = *engines_[size_t(engine)];
    const Queue& q = queues_[size_t(engine)];
    float* l = bufL_[size_t(engine)].data();
    float* r = bufR_[size_t(engine)].data();
    const int n = blockSamples_;
    int pos = 0;
    for (int k = 0; k < q.count; ++k)
    {
        const Event& ev = q.events[size_t(k)];
        const int at = std::min(std::max(ev.offset, pos), n);
        if (at > pos)
        {
            e.process(l + pos, r + pos, at - pos);
            pos = at;
        }
        if (ev.slot < 0)
            e.setPedal(ev.value);
        else
            e.strike(ev.slot, ev.value, ev.position);
    }
    if (pos < n)
        e.process(l + pos, r + pos, n - pos);
}

int EnginePool::work(uint32_t gen, Worker* worker) noexcept
{
    int jobs = 0;
    uint64_t v = ticket_.load(std::memory_order_acquire);
    for (;;)
    {
        if (genOf(v) != gen)
            return jobs;
        const int count = int((v >> 16) & 0xFFFFu), next = int(v & 0xFFFFu);
        if (next >= count)
            return jobs;
        if (!ticket_.compare_exchange_weak(v, v + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            continue; // v holds the counter's new value
        // The engine is this thread's until done_ counts it; the caller publishes no new block
        // before then, so fpMode_ and blockSamples_ are this block's.
        if (worker != nullptr && fpControl() != fpMode_)
            setFpControl(fpMode_);
        if (worker != nullptr && !worker->wakeUs.empty())
        {
            const int64_t t0 = nowNs();
            renderEngine(order_[size_t(next)]);
            worker->renderNs.fetch_add(uint64_t(nowNs() - t0), std::memory_order_relaxed);
        }
        else
            renderEngine(order_[size_t(next)]);
        ++jobs;
        done_.fetch_add(1, std::memory_order_release);
        v = ticket_.load(std::memory_order_acquire);
    }
}

void EnginePool::render(float* left, float* right, int numSamples) noexcept
{
    const int n = std::min(std::max(numSamples, 0), maxBlock_);
    blockSamples_ = n;
    ++blocks_;

    // Hand-out order: struck engines (by strikes, then ringing modes), ringing ones (by
    // ringing modes), silent ones; ties in engine order.
    int need = 0;
    int64_t key[kMaxEngines];
    for (int e = 0; e < numEngines_; ++e)
    {
        const Queue& q = queues_[size_t(e)];
        const FastEngine& eng = *engines_[size_t(e)];
        int64_t k = -1;
        if (q.strikes > 0)
            k = (int64_t(q.strikes) << 40) + (int64_t(1) << 32) + eng.activeModes();
        else if (!eng.isSilent())
            k = eng.activeModes();
        need += k >= 0 ? 1 : 0;
        key[e] = k;
        int j = e;
        for (; j > 0 && key[order_[size_t(j - 1)]] < k; --j)
            order_[size_t(j)] = order_[size_t(j - 1)];
        order_[size_t(j)] = e;
    }

    if (need < 2 || numWorkers_ == 0)
    {
        for (int e = 0; e < numEngines_; ++e)
            renderEngine(e);
    }
    else
    {
        ++sharedBlocks_;
        fpMode_ = fpControl();
        done_.store(0, std::memory_order_relaxed);
        if (options_.wakeSamples > 0)
            dispatchNs_.store(nowNs(), std::memory_order_relaxed);
        ++gen_;
        ticket_.store((uint64_t(gen_) << 32) | (uint64_t(numEngines_) << 16)); // seq_cst: see workerMain
        const int toWake = std::min(numWorkers_, need - 1);
        for (int i = 0; i < toWake; ++i)
        {
            Worker& w = *workers_[size_t(i)];
            if (w.sleeping.exchange(0) != 0)
                signal(w);
        }
        callerJobs_ += uint64_t(work(gen_, nullptr));
        while (done_.load(std::memory_order_acquire) < numEngines_)
            cpuPause();
    }

    // The mix, in engine order.
    if (numEngines_ == 0)
    {
        std::fill(left, left + n, 0.0f);
        std::fill(right, right + n, 0.0f);
    }
    else
    {
        std::copy(bufL_[0].data(), bufL_[0].data() + n, left);
        std::copy(bufR_[0].data(), bufR_[0].data() + n, right);
        for (int e = 1; e < numEngines_; ++e)
        {
            const float* l = bufL_[size_t(e)].data();
            const float* r = bufR_[size_t(e)].data();
            for (int i = 0; i < n; ++i)
            {
                left[i] += l[i];
                right[i] += r[i];
            }
        }
    }
    for (auto& q : queues_)
        q.count = q.strikes = 0;
}

void EnginePool::workerMain(int index) noexcept
{
    Worker& w = *workers_[size_t(index)];
    if (options_.realtime)
        w.realtime.store(makeRealtime(periodMs_));
#if defined(__APPLE__)
    w.port.store(pthread_mach_thread_np(pthread_self()));
#else
    w.hasClock.store(pthread_getcpuclockid(pthread_self(), &w.clock) == 0);
#endif
    if (options_.joinWorkgroup)
        options_.joinWorkgroup(index);
    const int64_t spinNs = int64_t(options_.spinMicros * 1000.0);
    uint32_t seen = genOf(ticket_.load(std::memory_order_acquire));
    w.started.store(true, std::memory_order_release);

    for (;;)
    {
        // Spin for the next block, then sleep. Sleeping: the flag first, then one more look at
        // the counter (both sequentially consistent, as the caller's store of the counter and
        // its exchange of the flag), so either this worker sees the new block or the caller
        // sees the flag and signals.
        bool fromSleep = false;
        uint64_t v = ticket_.load(std::memory_order_acquire);
        const int64_t spinEnd = nowNs() + spinNs;
        for (int i = 1; genOf(v) == seen && !stop_.load(std::memory_order_relaxed); ++i)
        {
            if ((i & 31) == 0 && nowNs() >= spinEnd)
            {
                w.sleeping.store(1);
                v = ticket_.load();
                if (genOf(v) != seen || stop_.load())
                {
                    if (w.sleeping.exchange(0) == 0)
                        wait(w); // the caller took the flag: take its signal
                    break;
                }
                wait(w); // the caller (or release) clears the flag, then signals
                fromSleep = true;
            }
            else
                cpuPause();
            v = ticket_.load(std::memory_order_acquire);
        }
        if (stop_.load(std::memory_order_acquire))
            break;
        seen = genOf(v);
        w.wakes.fetch_add(1, std::memory_order_relaxed);
        if (fromSleep)
            w.wakesFromSleep.fetch_add(1, std::memory_order_relaxed);
        if (!w.wakeUs.empty())
        {
            const int c = w.wakeCount.load(std::memory_order_relaxed);
            if (c < int(w.wakeUs.size()))
            {
                w.wakeUs[size_t(c)] = float(1e-3 * double(nowNs() - dispatchNs_.load(std::memory_order_relaxed)));
                w.wakeCount.store(c + 1, std::memory_order_release);
            }
        }
        w.jobs.fetch_add(uint64_t(work(seen, &w)), std::memory_order_relaxed);
    }
    if (options_.leaveWorkgroup)
        options_.leaveWorkgroup(index);
}

EnginePool::Stats EnginePool::stats() const
{
    Stats s;
    s.blocks = blocks_;
    s.sharedBlocks = sharedBlocks_;
    s.callerJobs = callerJobs_;
    for (const auto& w : workers_)
    {
        WorkerStats ws;
        ws.wakes = w->wakes.load();
        ws.wakesFromSleep = w->wakesFromSleep.load();
        ws.jobs = w->jobs.load();
        ws.renderSeconds = 1e-9 * double(w->renderNs.load());
        ws.realtime = w->realtime.load();
        s.workers.push_back(ws);
    }
    return s;
}

void EnginePool::resetStats() noexcept
{
    blocks_ = sharedBlocks_ = callerJobs_ = 0;
    for (auto& w : workers_)
    {
        w->wakes.store(0);
        w->wakesFromSleep.store(0);
        w->jobs.store(0);
        w->renderNs.store(0);
        w->wakeCount.store(0);
    }
}

std::vector<float> EnginePool::wakeDelaysMicros() const
{
    std::vector<float> all;
    for (const auto& w : workers_)
    {
        const int c = w->wakeCount.load(std::memory_order_acquire);
        all.insert(all.end(), w->wakeUs.begin(), w->wakeUs.begin() + c);
    }
    return all;
}

double EnginePool::workerCpuSeconds() const
{
    double total = 0.0;
    for (const auto& w : workers_)
    {
#if defined(__APPLE__)
        thread_basic_info_data_t info {};
        mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
        if (thread_info(thread_act_t(w->port.load()), THREAD_BASIC_INFO, thread_info_t(&info), &count) == KERN_SUCCESS)
            total += double(info.user_time.seconds) + 1e-6 * double(info.user_time.microseconds)
                     + double(info.system_time.seconds) + 1e-6 * double(info.system_time.microseconds);
#else
        timespec ts {};
        if (w->hasClock.load() && clock_gettime(w->clock, &ts) == 0)
            total += double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
#endif
    }
    return total;
}

bool EnginePool::waitIdle(double timeoutMs) const
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::microseconds(int64_t(timeoutMs * 1000.0));
    for (;;)
    {
        bool idle = true;
        for (const auto& w : workers_)
            idle = idle && w->sleeping.load() != 0;
        if (idle)
            return true;
        if (std::chrono::steady_clock::now() >= end)
            return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

} // namespace cymbalis

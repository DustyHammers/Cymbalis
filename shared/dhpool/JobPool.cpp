// JobPool.cpp — dhpool (JobPool.h, README-pool.md).
#include "JobPool.h"

#include <algorithm>
#include <cerrno>
#include <chrono>

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

namespace dhpool
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

// The counter: generation (40 bits), jobs (12), the next job (12).
constexpr uint64_t kGenMask = (uint64_t(1) << 40) - 1;
inline uint64_t pack(uint64_t gen, int count, int next) noexcept
{
    return ((gen & kGenMask) << 24) | (uint64_t(count) << 12) | uint64_t(next);
}
inline uint64_t genOf(uint64_t t) noexcept { return t >> 24; }
inline int countOf(uint64_t t) noexcept { return int((t >> 12) & 0xFFFu); }
inline int nextOf(uint64_t t) noexcept { return int(t & 0xFFFu); }
} // namespace

struct JobPool::Worker
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
    // The workgroup hook it joined, and the hook generation it has seen (worker only).
    const HookPair* joined = nullptr;
    uint64_t hookGen = 0;
    // Stats: written by the worker, read off the audio path.
    std::atomic<uint64_t> wakes { 0 }, wakesFromSleep { 0 }, jobs { 0 }, jobNs { 0 }, joins { 0 };
    std::vector<float> wakeUs;
    std::atomic<int> wakeCount { 0 };
};

JobPool::JobPool() = default;

JobPool::~JobPool()
{
    release();
}

bool JobPool::makeRealtime(double periodMs) noexcept
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

void JobPool::prepare(const Options& options)
{
    release();
    options_ = options;
    numWorkers_ = std::max(0, options.threads - 1);
    runs_ = sharedRuns_ = callerJobs_ = 0;
    callerWaitUs_.assign(size_t(std::max(0, options.timingSamples)), 0.0f);
    callerWaitCount_.store(0);
    stop_.store(false);
    ticket_.store(pack(gen_, 0, 0));
    for (int i = 0; i < numWorkers_; ++i)
    {
        auto w = std::make_unique<Worker>();
#if defined(__APPLE__)
        semaphore_create(mach_task_self(), &w->sem, SYNC_POLICY_FIFO, 0);
#else
        sem_init(&w->sem, 0, 0);
#endif
        w->wakeUs.assign(size_t(std::max(0, options.timingSamples)), 0.0f);
        workers_.push_back(std::move(w));
    }
    for (int i = 0; i < numWorkers_; ++i)
        workers_[size_t(i)]->thread = std::thread(&JobPool::workerMain, this, i);
    for (auto& w : workers_)
        while (!w->started.load(std::memory_order_acquire))
            std::this_thread::yield();
}

void JobPool::release()
{
    if (!workers_.empty())
    {
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
    }
    numWorkers_ = 0;
    // No worker holds a hook now: keep only the current one.
    std::lock_guard<std::mutex> lock(hookMutex_);
    const HookPair* current = hook_.load();
    hooks_.erase(std::remove_if(hooks_.begin(), hooks_.end(), [&](const auto& h) { return h.get() != current; }), hooks_.end());
}

void JobPool::setWorkgroupHook(Hook join, Hook leave)
{
    std::lock_guard<std::mutex> lock(hookMutex_);
    hooks_.push_back(std::make_unique<HookPair>(HookPair { std::move(join), std::move(leave) }));
    hook_.store(hooks_.back().get(), std::memory_order_release);
    hookGen_.fetch_add(1, std::memory_order_acq_rel);
}

void JobPool::signal(Worker& w) noexcept
{
#if defined(__APPLE__)
    semaphore_signal(w.sem);
#else
    sem_post(&w.sem);
#endif
}

void JobPool::wait(Worker& w) noexcept
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

int JobPool::work(uint64_t gen, Worker* worker) noexcept
{
    int jobs = 0;
    uint64_t v = ticket_.load(std::memory_order_acquire);
    for (;;)
    {
        if (genOf(v) != gen)
            return jobs;
        const int next = nextOf(v);
        if (next >= countOf(v))
            return jobs;
        if (!ticket_.compare_exchange_weak(v, v + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            continue; // v holds the counter's new value
        // The job is this thread's until done_ counts it; the caller starts no new run before
        // then, so fn_, ctx_, order_ and fpMode_ are this run's.
        const int job = order_ != nullptr ? order_[next] : next;
        if (worker != nullptr)
        {
            if (fpControl() != fpMode_)
                setFpControl(fpMode_);
            if (!worker->wakeUs.empty())
            {
                const int64_t t0 = nowNs();
                fn_(ctx_, job);
                worker->jobNs.fetch_add(uint64_t(nowNs() - t0), std::memory_order_relaxed);
            }
            else
                fn_(ctx_, job);
        }
        else
            fn_(ctx_, job);
        ++jobs;
        done_.fetch_add(1, std::memory_order_release);
        v = ticket_.load(std::memory_order_acquire);
    }
}

void JobPool::run(int count, const int* order, Fn fn, void* ctx) noexcept
{
    ++runs_;
    count = std::min(count, kMaxJobs);
    if (count <= 0)
        return;
    if (count < 2 || numWorkers_ == 0)
    {
        for (int k = 0; k < count; ++k)
            fn(ctx, order != nullptr ? order[k] : k);
        return;
    }
    ++sharedRuns_;
    fn_ = fn;
    ctx_ = ctx;
    order_ = order;
    fpMode_ = fpControl();
    done_.store(0, std::memory_order_relaxed);
    const bool timing = !callerWaitUs_.empty();
    if (timing)
        dispatchNs_.store(nowNs(), std::memory_order_relaxed);
    ++gen_;
    ticket_.store(pack(gen_, count, 0)); // seq_cst: see workerMain
    const int toWake = std::min(numWorkers_, count - 1);
    for (int i = 0; i < toWake; ++i)
    {
        Worker& w = *workers_[size_t(i)];
        if (w.sleeping.exchange(0) != 0)
            signal(w);
    }
    callerJobs_ += uint64_t(work(gen_ & kGenMask, nullptr));
    // The counter is empty: wait for the jobs still in progress on workers.
    const int64_t t0 = timing ? nowNs() : 0;
    while (done_.load(std::memory_order_acquire) < count)
        cpuPause();
    if (timing)
    {
        const int c = callerWaitCount_.load(std::memory_order_relaxed);
        if (c < int(callerWaitUs_.size()))
        {
            callerWaitUs_[size_t(c)] = float(1e-3 * double(nowNs() - t0));
            callerWaitCount_.store(c + 1, std::memory_order_release);
        }
    }
}

void JobPool::switchHook(Worker& w, int index) noexcept
{
    w.hookGen = hookGen_.load(std::memory_order_acquire);
    const HookPair* h = hook_.load(std::memory_order_acquire);
    if (w.joined != nullptr && w.joined->leave)
        w.joined->leave(index);
    w.joined = h;
    if (h != nullptr && h->join)
    {
        h->join(index);
        w.joins.fetch_add(1, std::memory_order_relaxed);
    }
}

void JobPool::workerMain(int index) noexcept
{
    Worker& w = *workers_[size_t(index)];
    if (options_.realtime && options_.periodMs > 0.0)
        w.realtime.store(makeRealtime(options_.periodMs));
#if defined(__APPLE__)
    w.port.store(pthread_mach_thread_np(pthread_self()));
#else
    w.hasClock.store(pthread_getcpuclockid(pthread_self(), &w.clock) == 0);
#endif
    switchHook(w, index); // the current workgroup, if any
    const int64_t spinNs = int64_t(options_.spinMicros * 1000.0);
    uint64_t seen = genOf(ticket_.load(std::memory_order_acquire));
    w.started.store(true, std::memory_order_release);

    for (;;)
    {
        // Spin for the next run, then sleep. Sleeping: the flag first, then one more look at the
        // counter (both sequentially consistent, as the caller's store of the counter and its
        // exchange of the flag), so either this worker sees the new run or the caller sees the
        // flag and signals. A signal the caller sends after this worker saw the run by itself is
        // taken at once, so none is left over.
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
        // A new workgroup: leave the old one and join it before taking a job.
        if (hookGen_.load(std::memory_order_acquire) != w.hookGen)
            switchHook(w, index);
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
    if (w.joined != nullptr && w.joined->leave)
        w.joined->leave(index);
    w.joined = nullptr;
}

JobPool::Stats JobPool::stats() const
{
    Stats s;
    s.runs = runs_;
    s.sharedRuns = sharedRuns_;
    s.callerJobs = callerJobs_;
    for (const auto& w : workers_)
    {
        WorkerStats ws;
        ws.wakes = w->wakes.load();
        ws.wakesFromSleep = w->wakesFromSleep.load();
        ws.jobs = w->jobs.load();
        ws.workgroupJoins = w->joins.load();
        ws.jobSeconds = 1e-9 * double(w->jobNs.load());
        ws.realtime = w->realtime.load();
        s.workers.push_back(ws);
    }
    return s;
}

void JobPool::resetStats() noexcept
{
    runs_ = sharedRuns_ = callerJobs_ = 0;
    callerWaitCount_.store(0);
    for (auto& w : workers_)
    {
        w->wakes.store(0);
        w->wakesFromSleep.store(0);
        w->jobs.store(0);
        w->jobNs.store(0);
        w->joins.store(0);
        w->wakeCount.store(0);
    }
}

std::vector<float> JobPool::wakeDelaysMicros() const
{
    std::vector<float> all;
    for (const auto& w : workers_)
    {
        const int c = w->wakeCount.load(std::memory_order_acquire);
        all.insert(all.end(), w->wakeUs.begin(), w->wakeUs.begin() + c);
    }
    return all;
}

std::vector<float> JobPool::callerWaitsMicros() const
{
    const int c = callerWaitCount_.load(std::memory_order_acquire);
    return std::vector<float>(callerWaitUs_.begin(), callerWaitUs_.begin() + c);
}

double JobPool::workerCpuSeconds() const
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

bool JobPool::waitIdle(double timeoutMs) const
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

} // namespace dhpool

// selftest.cpp — dhpool's self-test: dummy jobs of deterministic arithmetic and varied cost, at
// 1 to 4 threads (README-pool.md, "Self-test").
//
//   dhpool_selftest [--quick]
//
// Checks: every job runs exactly once per run, whatever the count and order; the results are
// bit-identical at every thread count, with flush to zero on and off (the jobs make denormals,
// so a worker without the caller's floating-point mode would differ); several runs per block
// with serial work between them; a workgroup hook change reaches every worker, never the calling
// thread, and every join is matched by a leave; prepare / release cycles leave no thread behind.
// Exit status 1 if any check fails.
#include "JobPool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <dirent.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace
{
int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

void setFlushToZero(bool on)
{
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned csr = _mm_getcsr();
    _mm_setcsr(on ? (csr | 0x8040u) : (csr & ~0x8040u));
#elif defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    const uint64_t fz = uint64_t(1) << 24;
    fpcr = on ? (fpcr | fz) : (fpcr & ~fz);
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#else
    (void) on;
#endif
}

int liveThreads()
{
#if defined(__APPLE__)
    thread_act_array_t list = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &list, &count) != KERN_SUCCESS)
        return -1;
    for (mach_msg_type_number_t i = 0; i < count; ++i)
        mach_port_deallocate(mach_task_self(), list[i]);
    vm_deallocate(mach_task_self(), vm_address_t(list), vm_size_t(count * sizeof(thread_act_t)));
    return int(count);
#elif defined(__linux__)
    int n = 0;
    if (DIR* d = opendir("/proc/self/task"))
    {
        while (const dirent* e = readdir(d))
            n += e->d_name[0] != '.' ? 1 : 0;
        closedir(d);
    }
    return n;
#else
    return -1;
#endif
}

struct Lcg
{
    uint64_t s;
    uint32_t next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return uint32_t(s >> 33);
    }
};

// A job's arithmetic: its cost varies with the job (200 to 3800 steps), and it makes a denormal
// that flush to zero removes.
double compute(int job, uint64_t salt, double input)
{
    double x = 1.0 + 1e-3 * job + 1e-6 * double(salt % 97) + input;
    const int steps = 200 + (job * 37 % 13) * 300;
    for (int i = 0; i < steps; ++i)
        x = x * 0.9999993 + 1e-7 * double(i & 7);
    volatile double tiny = 1e-300;
    const double d = tiny * (1e-10 * x); // about 1e-310: a denormal, or 0 with flush to zero
    return x + d * 1e300;
}

uint64_t bitsOf(double v)
{
    uint64_t b;
    std::memcpy(&b, &v, 8);
    return b;
}

uint64_t mix(uint64_t h, uint64_t v)
{
    return (h ^ v) * 0x100000001B3ull;
}

struct Jobs
{
    std::vector<std::atomic<int>> calls;
    std::vector<double> results, inputs;
    uint64_t salt = 0;
    explicit Jobs(size_t n) : calls(n), results(n), inputs(n) {}
};

void jobFn(void* ctx, int job)
{
    auto& j = *static_cast<Jobs*>(ctx);
    j.calls[size_t(job)].fetch_add(1, std::memory_order_relaxed);
    j.results[size_t(job)] = compute(job, j.salt, j.inputs[size_t(job)]);
}

dhpool::JobPool::Options options(int threads, bool realtime = false)
{
    dhpool::JobPool::Options o;
    o.threads = threads;
    o.realtime = realtime;
    o.periodMs = 2.9;
    o.timingSamples = 1 << 14;
    return o;
}

// Exactly once, and the digest of every result: runs of random counts (0 to 200, sometimes the
// maximum) in random orders.
uint64_t exactlyOnce(int threads, bool ftz, int runs)
{
    // The workers start in the other mode (a new thread takes its creator's), so only the
    // pool's hand-over gives them the caller's.
    setFlushToZero(!ftz);
    dhpool::JobPool pool;
    pool.prepare(options(threads));
    setFlushToZero(ftz);
    Jobs jobs(dhpool::JobPool::kMaxJobs);
    std::vector<int> order(dhpool::JobPool::kMaxJobs);
    Lcg rnd { 12345 };
    uint64_t digest = 0xCBF29CE484222325ull;
    for (int r = 0; r < runs; ++r)
    {
        const int count = r % 97 == 50 ? dhpool::JobPool::kMaxJobs : int(rnd.next() % 201);
        for (int k = 0; k < count; ++k)
            order[size_t(k)] = k;
        for (int k = count - 1; k > 0; --k)
            std::swap(order[size_t(k)], order[size_t(rnd.next() % uint32_t(k + 1))]);
        jobs.salt = uint64_t(r);
        for (int k = 0; k < count; ++k)
            jobs.inputs[size_t(k)] = 1e-4 * double(r % 13);
        pool.run(count, r % 5 == 0 ? nullptr : order.data(), &jobFn, &jobs);
        bool once = true;
        for (int k = 0; k < count; ++k)
        {
            once = once && jobs.calls[size_t(k)].load() == 1;
            jobs.calls[size_t(k)].store(0);
            digest = mix(digest, bitsOf(jobs.results[size_t(k)]));
        }
        if (!once)
        {
            check(false, std::to_string(threads) + " threads: run " + std::to_string(r) + " (" + std::to_string(count)
                             + " jobs) did not run every job exactly once");
            break;
        }
    }
    const auto s = pool.stats();
    uint64_t workerJobs = 0;
    for (const auto& w : s.workers)
        workerJobs += w.jobs;
    std::printf("  %d threads, flush to zero %s: %d runs, %llu shared, %llu jobs on workers, %llu on the caller\n",
                threads, ftz ? "on " : "off", runs, (unsigned long long) s.sharedRuns, (unsigned long long) workerJobs,
                (unsigned long long) s.callerJobs);
    setFlushToZero(true);
    return digest;
}

// Several runs per block: run A, a serial pass on the caller that folds A's results in order,
// run B whose jobs take that fold, a serial pass again. The digest of every block.
struct BlockResult
{
    uint64_t digest = 0;
    uint64_t wakesFromSleep = 0, wakes = 0, blocks = 0;
};

BlockResult severalRuns(int threads, int blocks)
{
    dhpool::JobPool pool;
    pool.prepare(options(threads));
    constexpr int kJobs = 24;
    Jobs a(kJobs), b(kJobs);
    uint64_t digest = 0xCBF29CE484222325ull;
    double carry = 0.0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        a.salt = b.salt = uint64_t(blk);
        for (int k = 0; k < kJobs; ++k)
            a.inputs[size_t(k)] = carry;
        pool.run(kJobs, nullptr, &jobFn, &a);
        double fold = 0.0;
        for (int k = 0; k < kJobs; ++k)
            fold = fold * 0.5 + a.results[size_t(k)];
        for (int k = 0; k < kJobs; ++k)
            b.inputs[size_t(k)] = 1e-6 * fold;
        pool.run(kJobs, nullptr, &jobFn, &b);
        carry = 0.0;
        for (int k = 0; k < kJobs; ++k)
            carry = carry * 0.25 + 1e-9 * b.results[size_t(k)];
        digest = mix(mix(digest, bitsOf(fold)), bitsOf(carry));
        for (int k = 0; k < kJobs; ++k)
        {
            check(a.calls[size_t(k)].exchange(0) == 1 && b.calls[size_t(k)].exchange(0) == 1,
                  "several runs per block: a job did not run exactly once");
        }
        // The next block comes a block later, as from a host (only every 8th, to keep it short).
        if (blk % 8 == 7)
            std::this_thread::sleep_for(std::chrono::microseconds(1500));
    }
    BlockResult r;
    r.digest = digest;
    r.blocks = uint64_t(blocks);
    for (const auto& w : pool.stats().workers)
    {
        r.wakes += w.wakes;
        r.wakesFromSleep += w.wakesFromSleep;
    }
    return r;
}

// The workgroup hook: per worker and hook, joins and leaves, and the threads they came from.
struct HookLog
{
    std::array<std::array<std::atomic<int>, 8>, 4> joins {}, leaves {};
    std::atomic<int> onCaller { 0 };
    std::thread::id caller;
    void record(std::array<std::array<std::atomic<int>, 8>, 4>& table, int hook, int worker)
    {
        if (std::this_thread::get_id() == caller)
            onCaller.fetch_add(1);
        if (worker >= 0 && worker < 8)
            table[size_t(hook)][size_t(worker)].fetch_add(1);
    }
};

void hookChange(int threads)
{
    HookLog log;
    log.caller = std::this_thread::get_id();
    dhpool::JobPool pool;
    const auto set = [&](int hook) {
        pool.setWorkgroupHook([&log, hook](int w) { log.record(log.joins, hook, w); },
                              [&log, hook](int w) { log.record(log.leaves, hook, w); });
    };
    set(0); // before prepare: each worker joins it at its start
    pool.prepare(options(threads));
    const int workers = threads - 1;
    Jobs jobs(64);
    const auto runUntil = [&](int hook) {
        // Runs (each wakes every worker) until every worker has joined `hook`, at most 2 s.
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (;;)
        {
            pool.run(threads * 4, nullptr, &jobFn, &jobs);
            pool.waitIdle(50.0);
            bool all = true;
            for (int w = 0; w < workers; ++w)
                all = all && log.joins[size_t(hook)][size_t(w)].load() > 0;
            if (all || std::chrono::steady_clock::now() > end)
                return all;
        }
    };
    check(runUntil(0), std::to_string(threads) + " threads: a worker did not join the first hook at its start");
    set(1);
    check(runUntil(1), std::to_string(threads) + " threads: a worker did not join the second hook");
    set(2);
    check(runUntil(2), std::to_string(threads) + " threads: a worker did not join the third hook");
    pool.setWorkgroupHook({}, {}); // no workgroup: each worker leaves hook 2 at its next wake-up
    for (int i = 0; i < 50; ++i)
    {
        pool.run(threads * 4, nullptr, &jobFn, &jobs);
        pool.waitIdle(50.0);
    }
    pool.release();
    bool matched = true;
    for (int h = 0; h < 3; ++h)
        for (int w = 0; w < workers; ++w)
            matched = matched && log.joins[size_t(h)][size_t(w)].load() == 1 && log.leaves[size_t(h)][size_t(w)].load() == 1;
    check(matched, std::to_string(threads) + " threads: not every worker joined and left each hook exactly once");
    check(log.onCaller.load() == 0, std::to_string(threads) + " threads: a hook ran on the calling thread");
    for (auto& c : jobs.calls)
        c.store(0);
    std::printf("  %d threads: three hooks and none, each joined and left once by each of %d workers%s\n", threads,
                workers, log.onCaller.load() == 0 ? ", never on the caller" : "");
}

void cycles(int count)
{
    const int before = liveThreads();
    Lcg rnd { 777 };
    Jobs jobs(64);
    for (int c = 0; c < count; ++c)
    {
        dhpool::JobPool pool;
        const int threads = 1 + int(rnd.next() % 4);
        pool.prepare(options(threads, c % 3 == 0));
        for (int r = 0; r < 20; ++r)
            pool.run(int(rnd.next() % 30), nullptr, &jobFn, &jobs);
        if (c % 2 == 0)
        {
            pool.release();
            pool.prepare(options(1 + int(rnd.next() % 4)));
            pool.run(16, nullptr, &jobFn, &jobs);
        }
        // the destructor releases
    }
    const int after = liveThreads();
    check(before == after, "prepare / release cycles: " + std::to_string(before) + " threads before, " + std::to_string(after)
                               + " after");
    std::printf("  %d pools prepared and released (some twice): threads before %d, after %d\n", count, before, after);
}
} // namespace

int main(int argc, char** argv)
{
    const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
    const int runs = quick ? 300 : 3000, blocks = quick ? 200 : 2000;
    setFlushToZero(true);
    std::printf("dhpool %d self-test%s\n\nExactly once and bit identity (%d runs of 0 to 200 jobs, some of %d):\n",
                dhpool::kVersion, quick ? " (quick)" : "", runs, dhpool::JobPool::kMaxJobs);
    for (const bool ftz : {true, false})
    {
        uint64_t ref = 0;
        for (int t = 1; t <= 4; ++t)
        {
            const uint64_t d = exactlyOnce(t, ftz, runs);
            if (t == 1)
                ref = d;
            check(d == ref, std::to_string(t) + " threads, flush to zero " + (ftz ? "on" : "off")
                                + ": results differ from one thread's");
        }
    }
    std::printf("\nSeveral runs per block (two runs and two serial passes per block, %d blocks):\n", blocks);
    uint64_t ref = 0;
    for (int t = 1; t <= 4; ++t)
    {
        const BlockResult r = severalRuns(t, blocks);
        if (t == 1)
            ref = r.digest;
        check(r.digest == ref, std::to_string(t) + " threads: several runs per block differ from one thread's");
        std::printf("  %d threads: %s; worker wake-ups %llu, of them from sleep %llu (2 runs per block)\n", t,
                    r.digest == ref ? "bit-identical" : "DIFFERENT", (unsigned long long) r.wakes,
                    (unsigned long long) r.wakesFromSleep);
    }
    std::printf("\nWorkgroup hook changes:\n");
    for (int t = 2; t <= 4; ++t)
        hookChange(t);
    std::printf("\nPrepare / release:\n");
    cycles(quick ? 20 : 60);
    std::printf("\n%s\n", failures == 0 ? "All checks passed." : "SOME CHECKS FAILED.");
    return failures == 0 ? 0 : 1;
}

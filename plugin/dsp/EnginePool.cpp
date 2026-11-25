// EnginePool.cpp — see EnginePool.h and docs/PORT_MAP.md, "Multi-cymbal rendering".
#include "EnginePool.h"

#include <algorithm>

namespace cymbalis
{

void EnginePool::prepare(const std::vector<FastEngine*>& engines, double sampleRate, int maxBlock, const Options& options)
{
    pool_.release();
    numEngines_ = int(std::min<size_t>(engines.size(), kMaxEngines));
    engines_.assign(engines.begin(), engines.begin() + numEngines_);
    maxBlock_ = std::max(1, maxBlock);
    queues_.assign(size_t(numEngines_), Queue {});
    bufL_.assign(size_t(numEngines_), std::vector<float>(size_t(maxBlock_)));
    bufR_.assign(size_t(numEngines_), std::vector<float>(size_t(maxBlock_)));
    order_.assign(size_t(numEngines_), 0);
    blocks_ = sharedBlocks_ = silentJobs_ = 0;
    if (options.joinWorkgroup || options.leaveWorkgroup)
        pool_.setWorkgroupHook(options.joinWorkgroup, options.leaveWorkgroup);
    dhpool::JobPool::Options po;
    po.threads = options.threads;
    po.spinMicros = options.spinMicros;
    po.realtime = options.realtime;
    po.periodMs = 1000.0 * maxBlock_ / sampleRate;
    po.timingSamples = options.wakeSamples;
    pool_.prepare(po);
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

void EnginePool::renderJob(void* self, int engine) noexcept
{
    static_cast<EnginePool*>(self)->renderEngine(engine);
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

    if (need < 2 || pool_.threads() < 2)
    {
        for (int e = 0; e < numEngines_; ++e)
            renderEngine(e);
    }
    else
    {
        // The engines that need rendering, heaviest first, as one run; then the silent ones.
        ++sharedBlocks_;
        pool_.run(need, order_.data(), &EnginePool::renderJob, this);
        for (int k = need; k < numEngines_; ++k)
            renderEngine(order_[size_t(k)]);
        silentJobs_ += uint64_t(numEngines_ - need);
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

EnginePool::Stats EnginePool::stats() const
{
    const auto p = pool_.stats();
    Stats s;
    s.blocks = blocks_;
    s.sharedBlocks = sharedBlocks_;
    s.callerJobs = p.callerJobs + silentJobs_;
    for (const auto& w : p.workers)
        s.workers.push_back({ w.wakes, w.wakesFromSleep, w.jobs, w.jobSeconds, w.realtime });
    return s;
}

void EnginePool::resetStats() noexcept
{
    blocks_ = sharedBlocks_ = silentJobs_ = 0;
    pool_.resetStats();
}

} // namespace cymbalis

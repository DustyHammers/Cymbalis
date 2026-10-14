// Pcg64.h — numpy's PCG64 bit generator plus the two Generator methods the
// V19 engine uses: uniform() and choice() on a two-element array.
//
// Seeding goes through numpy's SeedSequence, which is not ported. The bake
// stores each stream's post-seeding (state, inc) and the generator starts from
// those values. Bake::load checks raw outputs, uniform() and choice() against
// numpy's own results.
#pragma once

#include <cstdint>

namespace cymbalis
{

class Pcg64
{
public:
    using u128 = unsigned __int128;

    Pcg64() = default;
    Pcg64(u128 state, u128 inc) : state_(state), inc_(inc) {}

    // pcg_setseq_128_xsl_rr_64: advance the LCG, then output from the new state.
    uint64_t nextU64() noexcept
    {
        state_ = state_ * kMultiplier + inc_;
        const uint64_t hi = static_cast<uint64_t>(state_ >> 64);
        const uint64_t lo = static_cast<uint64_t>(state_);
        const uint64_t x = hi ^ lo;
        const unsigned rot = static_cast<unsigned>(state_ >> 122);
        return (x >> rot) | (x << ((64u - rot) & 63u));
    }

    // numpy pcg64_next32: low half of a fresh 64-bit output first, high half buffered.
    uint32_t nextU32() noexcept
    {
        if (hasU32_)
        {
            hasU32_ = false;
            return u32_;
        }
        const uint64_t v = nextU64();
        hasU32_ = true;
        u32_ = static_cast<uint32_t>(v >> 32);
        return static_cast<uint32_t>(v & 0xffffffffu);
    }

    // numpy next_double: 53 random bits scaled to [0, 1).
    double nextDouble() noexcept
    {
        return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
    }

    // numpy Generator.uniform(low, high): random_uniform(low, high - low) = low + range * next_double.
    // `range` must be computed by the caller as (high - low) in double, exactly as numpy does.
    double uniform(double low, double range) noexcept { return low + range * nextDouble(); }

    // numpy Generator.choice(a, size) for a two-element array (integers(0, 2)):
    // buffered bounded Lemire on 32-bit draws, rng = 1, never rejects; index = (x * 2) >> 32.
    int choiceIndex2() noexcept
    {
        const uint64_t m = static_cast<uint64_t>(nextU32()) * 2u;
        return static_cast<int>(m >> 32);
    }

    static u128 make128(uint64_t hi, uint64_t lo) noexcept { return (static_cast<u128>(hi) << 64) | lo; }

private:
    static constexpr u128 kMultiplier =
        (static_cast<u128>(0x2360ED051FC65DA4ULL) << 64) | static_cast<u128>(0x4385DF649FCCF645ULL);

    u128 state_ = 0;
    u128 inc_ = 0;
    bool hasU32_ = false;
    uint32_t u32_ = 0;
};

} // namespace cymbalis

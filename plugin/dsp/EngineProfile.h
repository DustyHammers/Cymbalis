// EngineProfile.h — timing of FastEngine's parts, for cymbalis_bench's profile build only.
//
// FastEngine.cpp marks its parts with CYM_PROF_BEGIN(part) ... CYM_PROF_END(part) and counts
// events with CYM_PROF_COUNT(event). They compile to nothing unless CYMBALIS_ENGINE_PROFILE
// is defined, which only the library cymbalis_fast_profile does (tools/cymbalis_bench): the
// plugin's engine has none of it. The totals are global, for one engine on one thread.
#pragma once

#if CYMBALIS_ENGINE_PROFILE

#include <chrono>
#include <cstdint>

namespace cymbalis::profile
{

enum Part : int
{
    // strike()
    strike,
    strikeRebase,      // the ringing components brought to the strike (rebaseComponents)
    strikeCymbalWide,  // LF/MF scales, wander phase and contact factor there (cymbalWideAtStrike)
    strikePhases,      // the strike's random phases
    strikeVelocity,    // velocity law: participation tables or hertz law, bell blend
    strikePosition,    // position weights, edge level boost
    strikeAlloc,       // a component: a free one, or a merge (allocateComponent)
    strikeRehit,       // re-hit damping ramps (startRehit)
    strikeContact,     // contact setup (and a full contact pool's flush)
    strikeNpz,         // NPZ edge coupling and its phases
    strikeCoupling,    // per mode: coupling, excitation gains, brightness, phase, contact or injection
    strikeDecay,       // wash_damp and the component's own decay per mode
    strikeEnvelope,    // NPZ band envelope (startEnvelope)
    strikeOnsetShift,  // onset shift and its velocity layers
    strikeRamp,        // onset ramp: peak times, velocity tilt, ramp coefficients
    strikeAttack,      // attack band gains and their velocity layers
    strikeOnsetPeaks,  // peak readout for the Tuning window (tuning build)
    strikeAccum,       // LF/MF accumulators and the wander rate
    strikeStick,       // stick click (startStick)
    strikeFreeStart,   // a free phase started by a strike without a contact phase
    merge,             // mergeComponents (inside strikeAlloc, or the cascade's)
    // process()
    contactSample,     // contact phase, per sample
    freeStart,         // startFreePhase at the end of the contact phase
    freeStartRotate,   //   older components stepped over the contact
    freeStartPending,  //   the new components' contact response
    freeStartDecay,    //   the new components' own decay, frame restarts
    freeStartModes,    //   ringing modes: threshold, wander phase, oscillator anchor
    controlBlock,      // beginControlBlock
    blockCommit,       //   cascade / damping gains into the state (commitBlockGains)
    blockRenorm,       //   renormOscillators
    blockDrop,         //   dropQuietModes
    blockWander,       //   pending wander, LF/MF scales, wander rate
    blockOscillators,  //   shared oscillators: LFO steps, wander phase, anchors
    blockSum,          //   components summed (sumComponents)
    blockBands,        //   cascade and loud-band measurement and transfer (bandBlock)
    blockGains,        //   LF/MF gains and per-sample increments
    kernel,            // the free ring, per block
    kinks,             // onset ramps ending inside a block (renderKinks)
    stickRender,       // stick clicks (renderStick)
    highPass,          // right-channel high-pass and output gain
    kParts
};

enum Event : int
{
    poolFull,          // a strike found every component in use
    mergeSettled,      // ... and merged the two settled ones with the closest decay
    mergeOldest,       // ... and merged the two oldest (envelopes dropped)
    reuseOldest,       // ... every component in its contact: the oldest reused
    contactFlush,      // a strike found every contact running: the oldest flushed
    kEvents
};

struct Totals
{
    uint64_t ns[kParts] {};
    uint64_t calls[kParts] {};
    uint64_t maxNs[kParts] {}; // the longest single call
    uint64_t events[kEvents] {};
};

extern Totals totals;
extern const char* const partNames[kParts];
extern const char* const eventNames[kEvents];

inline uint64_t now() noexcept
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline void add(Part p, uint64_t t0) noexcept
{
    const uint64_t dt = now() - t0;
    totals.ns[p] += dt;
    ++totals.calls[p];
    totals.maxNs[p] = dt > totals.maxNs[p] ? dt : totals.maxNs[p];
}

} // namespace cymbalis::profile

#define CYM_PROF_BEGIN(part) const uint64_t cymProf_##part = ::cymbalis::profile::now()
#define CYM_PROF_END(part) ::cymbalis::profile::add(::cymbalis::profile::part, cymProf_##part)
#define CYM_PROF_COUNT(event) (++::cymbalis::profile::totals.events[::cymbalis::profile::event])

#else

#define CYM_PROF_BEGIN(part) ((void) 0)
#define CYM_PROF_END(part) ((void) 0)
#define CYM_PROF_COUNT(event) ((void) 0)

#endif

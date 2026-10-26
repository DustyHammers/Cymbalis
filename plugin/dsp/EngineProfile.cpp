// EngineProfile.cpp — the totals of EngineProfile.h (cymbalis_fast_profile only).
#include "EngineProfile.h"

#if CYMBALIS_ENGINE_PROFILE

namespace cymbalis::profile
{

Totals totals;

const char* const partNames[kParts] = {
    "strike()",
    "  rebase: ringing components to the strike",
    "  cymbal-wide state at the strike",
    "  random phases",
    "  velocity law",
    "  position weights",
    "  component allocation / merge",
    "  re-hit damping",
    "  contact setup",
    "  NPZ edge coupling",
    "  coupling per mode (gains, brightness, cos/sin)",
    "  own decay per mode",
    "  NPZ band envelope",
    "  onset shift",
    "  onset ramp",
    "  attack band gains",
    "  onset peak readout (tuning)",
    "  LF/MF accumulators, wander rate",
    "  stick click",
    "  free phase start (no contact)",
    "merge of two components",
    "contact phase sample",
    "free phase start (contact end)",
    "  older components over the contact",
    "  new components' contact response",
    "  new components' own decay",
    "  ringing modes: threshold, wander, anchors",
    "control point",
    "  cascade/damping gains into the state",
    "  oscillator renormalisation",
    "  quiet mode drop",
    "  pending wander, LF/MF scales, wander rate",
    "  shared oscillators (LFO, wander, anchors)",
    "  components summed",
    "  cascade and loud-band measurement",
    "  LF/MF gains, increments",
    "free ring (kernel)",
    "onset ramp ends inside a block",
    "stick click render",
    "high-pass and output gain",
};

const char* const eventNames[kEvents] = {
    "strikes into a full component pool",
    "  merged the two settled components with the closest decay",
    "  merged the two oldest (envelopes dropped)",
    "  reused the oldest (every component in its contact)",
    "strikes into a full contact pool (oldest flushed)",
};

} // namespace cymbalis::profile

#endif

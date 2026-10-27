#include "TuningFields.h"

#include <cmath>


namespace cymbalis::tuning
{

const char* groupName (Group g)
{
    switch (g)
    {
        case Group::cymbal: return "CYMBAL";
        case Group::stereo: return "STEREO";
        case Group::wander: return "WANDER";
        case Group::mf: return "MF BUILD-UP";
        case Group::lf: return "LF BUILD-UP";
        case Group::edgeEnvelope: return "EDGE ENVELOPE";
        case Group::cascade: return "ENERGY CASCADE";
        case Group::loudDamping: return "LOUD-BAND DAMPING";
        case Group::rehitDamping: return "RE-HIT DAMPING";
    }
    return "";
}

bool applies (Needs needs, const BakeData& b)
{
    switch (needs)
    {
        case Needs::always: return true;
        case Needs::npzEdge: return b.edgeNpz.present && b.edgeNpz.useVelw;
        case Needs::envelope: return b.edgeNpz.present; // the tuning can switch the envelope on
        case Needs::noVelocityTables:
            for (const auto& s : b.slots)
                if (! s.hasBowVp)
                    return true;
            return false;
    }
    return false;
}

juce::String scopeNames (Scope scope, const BakeData& b)
{
    juce::StringArray names;
    for (const auto& s : b.slots)
    {
        const double x = s.position;
        const bool in = scope == Scope::edge   ? x <= 0.25
                        : scope == Scope::bow  ? x > 0.25 && x < 0.75
                        : scope == Scope::bell ? x >= 0.75
                        : scope == Scope::hertz ? x > 0.25 && ! s.hasBowVp
                                                : false;
        if (in)
            names.add (juce::String (s.id).toUpperCase());
    }
    return names.joinIntoString (", ");
}

const std::vector<Field>& cymbalFields()
{
    using C = CymbalTuning;
    static const std::vector<Field> fields {
        { "output_gain_db", "Output gain", "dB", Group::cymbal, Needs::always, -24.0, 12.0, 1, &C::outputGainDb, false },
        { "edge_velocity_remap_exponent", "Edge capture curve", "", Group::cymbal, Needs::npzEdge, 0.25, 2.0, 2, &C::edgeRemapExponent, false, Scope::edge },
        { "tau_ref_ms", "Bow brightness vs velocity (tau_ref)", "ms", Group::cymbal, Needs::noVelocityTables, 0.05, 8.0, 2, &C::tauRefMs, false, Scope::hertz },
        { "ring_length", "Ring length (x t60_scale)", "x", Group::cymbal, Needs::always, 0.5, 2.0, 2, &C::ringLength, true },
        { "edge_ring_keep_ms", "Kept onset (ring length acts after it)", "ms", Group::cymbal, Needs::envelope, 0.0, 300.0, 0, &C::edgeRingKeepMs, false, Scope::edge },

        { "stereo_width", "Overall width (x every band's width)", "%", Group::stereo, Needs::always, 0.0, kWidthMax, 0, &C::stereoWidth, false },
        { "right_highpass_hz", "Right-channel high-pass frequency", "Hz", Group::stereo, Needs::always, kHighPassMinHz, kHighPassMaxHz, 0, &C::highPassHz, false },

        { "wander_depth", "Wander depth", "", Group::wander, Needs::always, 0.0, 4.0, 2, &C::wanderDepth, false },
        { "wander_rate_hz", "Wander rate", "Hz", Group::wander, Needs::always, 0.05, 10.0, 2, &C::wanderRateHz, false },
        { "edge_wander_depth", "Edge wander depth", "", Group::wander, Needs::always, 0.0, 4.0, 2, &C::edgeWanderDepth, false, Scope::edge },
        { "edge_wander_rate", "Edge wander rate", "", Group::wander, Needs::always, 0.0, 4.0, 2, &C::edgeWanderRate, false, Scope::edge },

        { "mf_sens_depth", "Depth", "", Group::mf, Needs::always, 0.0, 2.0, 3, &C::mfDepth, false },
        { "mf_sens_tau", "Tau", "s", Group::mf, Needs::always, 0.05, 20.0, 2, &C::mfTau, false },
        { "mf_min_scale", "Min scale", "", Group::mf, Needs::always, 0.0, 1.0, 2, &C::mfMin, false },
        { "mf_max_scale", "Max scale (above 1 = boost)", "", Group::mf, Needs::always, 0.1, 3.0, 2, &C::mfMax, false },
        { "mf_bow_weight", "Bow weight", "", Group::mf, Needs::always, 0.0, 2.0, 2, &C::mfWBow, false, Scope::bow },
        { "mf_edge_weight", "Edge weight", "", Group::mf, Needs::always, 0.0, 2.0, 2, &C::mfWEdge, false, Scope::edge },
        { "mf_bell_weight", "Bell weight", "", Group::mf, Needs::always, 0.0, 2.0, 2, &C::mfWBell, false, Scope::bell },
        { "mf_lo_hz", "Low edge", "Hz", Group::mf, Needs::always, 50.0, 8000.0, 0, &C::mfLoHz, false },
        { "mf_hi_hz", "High edge", "Hz", Group::mf, Needs::always, 200.0, 16000.0, 0, &C::mfHiHz, false },

        { "lf_sens_depth", "Depth", "", Group::lf, Needs::always, 0.0, 2.0, 3, &C::lfDepth, false },
        { "lf_sens_tau", "Tau", "s", Group::lf, Needs::always, 0.05, 20.0, 2, &C::lfTau, false },
        { "lf_min_scale", "Min scale", "", Group::lf, Needs::always, 0.0, 1.0, 2, &C::lfMin, false },
        { "lf_max_scale", "Max scale (above 1 = boost)", "", Group::lf, Needs::always, 0.1, 3.0, 2, &C::lfMax, false },
        { "lf_cutoff_hz", "Cutoff", "Hz", Group::lf, Needs::always, 100.0, 8000.0, 0, &C::lfCutoffHz, false },
        { "lf_bow_weight", "Bow weight", "", Group::lf, Needs::always, 0.0, 2.0, 2, &C::lfWBow, false, Scope::bow },
        { "lf_edge_weight", "Edge weight", "", Group::lf, Needs::always, 0.0, 2.0, 2, &C::lfWEdge, false, Scope::edge },
        { "lf_bell_weight", "Bell weight", "", Group::lf, Needs::always, 0.0, 2.0, 2, &C::lfWBell, false, Scope::bell },
        { "lf_wander_rate_boost", "Wander-rate boost", "", Group::lf, Needs::always, 0.0, 2.0, 2, &C::lfWanderRateBoost, false },

        { "edge_band_env_onset_s", "Window", "s", Group::edgeEnvelope, Needs::envelope, 0.1, 30.0, 2, &C::edgeEnvWindowS, false, Scope::edge },

        { "cascade_amount", "Amount (0 = off)", "", Group::cascade, Needs::always, 0.0, 1.0, 2, &C::cascadeAmount, false },
        { "cascade_threshold_db", "Threshold (0 dB = a velocity-1.0 BOW strike)", "dB", Group::cascade, Needs::always, -40.0, 0.0, 1, &C::cascadeThresholdDb, false },
        { "cascade_speed_ms", "Speed (attack)", "ms", Group::cascade, Needs::always, 5.0, 1000.0, 0, &C::cascadeSpeedMs, false },
        { "cascade_release_ms", "Release", "ms", Group::cascade, Needs::always, 10.0, 3000.0, 0, &C::cascadeReleaseMs, false },
        { "cascade_spread", "Spread", "bands", Group::cascade, Needs::always, 1.0, 6.0, 0, &C::cascadeSpread, false, Scope::all, 1.0 },
        { "cascade_low_band", "Lowest band", "", Group::cascade, Needs::always, 0.0, 21.0, 0, &C::cascadeLowBand, false, Scope::all, 1.0, true },
        { "cascade_high_band", "Highest band", "", Group::cascade, Needs::always, 0.0, 21.0, 0, &C::cascadeHighBand, false, Scope::all, 1.0, true },

        { "loud_damp_amount", "Amount (0 = off)", "", Group::loudDamping, Needs::always, 0.0, 4.0, 2, &C::loudDampAmount, false },
        { "loud_damp_threshold_db", "Threshold (0 dB = a velocity-1.0 BOW strike)", "dB", Group::loudDamping, Needs::always, -40.0, 0.0, 1, &C::loudDampThresholdDb, false },
        { "loud_damp_tilt", "Tilt (- more on low bands, + on high)", "", Group::loudDamping, Needs::always, -1.0, 1.0, 2, &C::loudDampTilt, false },

        { "rehit_damp_amount", "Amount (0 = off)", "", Group::rehitDamping, Needs::always, 0.0, 1.0, 2, &C::rehitDampAmount, false },
        { "rehit_damp_time_ms", "Time", "ms", Group::rehitDamping, Needs::always, 1.0, 50.0, 1, &C::rehitDampTimeMs, false },
    };
    return fields;
}

const std::vector<ArticulationField>& articulationFields()
{
    using A = ArticulationTuning;
    static const std::vector<ArticulationField> fields {
        { kLevelKey, "Level at velocity 1.0", "dB", -40.0, 40.0, 2, &A::levelDb },
        { kVel06Key, "Offset at velocity 0.6", "dB", -24.0, 24.0, 1, &A::vel06Db },
        { kVel03Key, "Offset at velocity 0.3", "dB", -24.0, 24.0, 1, &A::vel03Db },
        { kBrightnessKey, "Brightness vs velocity (tilt at 0.3, pivot 1 kHz)", "dB/oct", kBrightnessMin, kBrightnessMax, 2,
          &A::brightnessDbOct },
        { kAttackTimeKey, "ATTACK TIME", "ms", kAttackTimeMinMs, kAttackTimeMaxMs, 0, &A::attackMs, true },
        { kStickGainKey, "STICK CLICK gain (0 = none)", "", 0.0, kStickGainMax, 2, &A::stickGain },
        { kStickLoKey, "STICK CLICK band low", "Hz", kStickLoMinHz, kStickLoMaxHz, 0, &A::stickLoHz },
        { kStickHiKey, "STICK CLICK band high", "Hz", kStickHiMinHz, kStickHiMaxHz, 0, &A::stickHiHz },
        { kStickLenKey, "STICK CLICK length", "ms", kStickLenMinMs, kStickLenMaxMs, 1, &A::stickLenMs },
        { kStickDecayKey, "STICK CLICK decay", "ms", kStickDecayMinMs, kStickDecayMaxMs, 1, &A::stickDecayMs },
    };
    return fields;
}

const std::vector<BandField>& bandFields()
{
    static const std::vector<BandField> fields {
        { { kBandGainsKey, "GAIN", "dB", kBandGainMinDb, kBandGainMaxDb, 0.0, 1, false }, &ArticulationTuning::bandDb },
        { { kAttackGainsKey, "ATTACK GAIN", "dB", kAttackGainMinDb, kAttackGainMaxDb, 0.0, 1, false }, &ArticulationTuning::attackDb,
          &ArticulationTuning::attackDbSoft, kAttackGainsSoftKey },
        { { kOnsetDelayKey, "ONSET DELAY", "ms", kOnsetDelayMinMs, kOnsetDelayMaxMs, 0.0, 1, false }, &ArticulationTuning::delayMs,
          &ArticulationTuning::delayMsSoft, kOnsetDelaySoftKey },
        { { kOnsetSpeedKey, "ONSET SPEED", "x", kOnsetSpeedMin, kOnsetSpeedMax, 1.0, 2, true }, &ArticulationTuning::speed,
          &ArticulationTuning::speedSoft, kOnsetSpeedSoftKey },
    };
    return fields;
}

const std::vector<CymbalBandField>& cymbalBandFields()
{
    static const std::vector<CymbalBandField> fields {
        { { kRingBandsKey, "RING LENGTH PER BAND", "x", kRingBandMin, kRingBandMax, 1.0, 2, true }, Group::cymbal, &CymbalTuning::ringBand },
        { { kPanBandsKey, "PAN", "", -kPanMax, kPanMax, 0.0, 0, false, "CENTRE", true }, Group::stereo, &CymbalTuning::panBands },
        { { kWidthBandsKey, "WIDTH", "%", 0.0, kWidthMax, 100.0, 0, false }, Group::stereo, &CymbalTuning::widthBands },
    };
    return fields;
}

const NovaShoulderProfile* novaShoulderProfile (const BakeData& b, const juce::String& presetId, size_t slot)
{
    if (presetId == "nova_ride" || slot >= b.slots.size() || b.slots[slot].copyOf.empty())
        return nullptr;
    const auto& id = b.slots[slot].id;
    if (id == "bell_shoulder")
        return &kNovaShoulderBell;
    if (id == "bow_shoulder" || id == "center_shoulder")
        return &kNovaShoulderBow;
    return nullptr;
}

namespace
{
// 22 numbers within the spec's range from a JSON array into `out`; an error message, or empty.
juce::String readBands (const juce::var& value, const BandSpec& f, const juce::String& name,
                        std::array<double, kExcitationBands>& out)
{
    const auto* bands = value.getArray();
    if (bands == nullptr || bands->size() != kExcitationBands)
        return name + " needs " + juce::String (kExcitationBands) + " values.";
    for (int j = 0; j < kExcitationBands; ++j)
    {
        const auto& item = (*bands)[j];
        const double g = (double) item;
        const bool number = item.isDouble() || item.isInt() || item.isInt64();
        if (! number || ! std::isfinite (g) || g < f.lo - 1e-9 || g > f.hi + 1e-9)
            return name + " values must lie in " + juce::String (f.lo) + " to " + juce::String (f.hi) + " " + f.unit + ".";
        out[(size_t) j] = g;
    }
    return {};
}

juce::String idOf (const BakeData& b, size_t s) { return b.slots[s].id; }

bool sameRehitWeights (const BakeData& b, const CymbalTuning& x, const CymbalTuning& y)
{
    for (size_t s = 0; s < b.slots.size(); ++s)
        if (! juce::exactlyEqual (x.rehitWeight[s], y.rehitWeight[s]))
            return false;
    return true;
}

juce::var cymbalObject (const BakeData& b, const Tuning& t, bool onlyChanged)
{
    auto* o = new juce::DynamicObject();
    for (const auto& f : cymbalFields())
    {
        if (! applies (f.needs, b))
            continue;
        const double v = t.cymbal.*(f.member);
        if (! onlyChanged || ! juce::exactlyEqual (v, b.tuning.cymbal.*(f.member)))
            o->setProperty (f.key, v);
    }
    if (applies (Needs::envelope, b)
        && (! onlyChanged || t.cymbal.frameDecayRestart != b.tuning.cymbal.frameDecayRestart))
        o->setProperty (kFrameDecayRestartKey, t.cymbal.frameDecayRestart);
    if (applies (Needs::envelope, b) && (! onlyChanged || t.cymbal.edgeEnvelope != b.tuning.cymbal.edgeEnvelope))
        o->setProperty (kEdgeEnvelopeKey, t.cymbal.edgeEnvelope);
    if (! onlyChanged || t.cymbal.highPassOn != b.tuning.cymbal.highPassOn)
        o->setProperty (kHighPassKey, t.cymbal.highPassOn);
    // The per-band values: all 22 when any differs.
    for (const auto& f : cymbalBandFields())
        if (! onlyChanged || t.cymbal.*(f.member) != b.tuning.cymbal.*(f.member))
        {
            juce::Array<juce::var> bands;
            for (double g : t.cymbal.*(f.member))
                bands.add (g);
            o->setProperty (f.spec.key, bands);
        }
    // Re-hit damping weights: every articulation's, by id, when any differs.
    if (! onlyChanged || ! sameRehitWeights (b, t.cymbal, b.tuning.cymbal))
    {
        auto* w = new juce::DynamicObject();
        for (size_t s = 0; s < b.slots.size(); ++s)
            w->setProperty (idOf (b, s), t.cymbal.rehitWeight[s]);
        o->setProperty (kRehitWeightsKey, juce::var (w));
    }
    return juce::var (o);
}

juce::var articulationObject (const BakeData& b, const Tuning& t, bool onlyChanged)
{
    auto* o = new juce::DynamicObject();
    for (size_t s = 0; s < b.slots.size(); ++s)
    {
        auto* a = new juce::DynamicObject();
        bool any = false;
        for (const auto& f : articulationFields())
        {
            const double v = t.articulations[s].*(f.member);
            if (! onlyChanged || ! juce::exactlyEqual (v, b.tuning.articulations[s].*(f.member)))
            {
                a->setProperty (f.key, v);
                any = true;
            }
        }
        // The per-band values (gains, attack gains, onset delays, onset speeds): all 22 when any
        // differs, both layers of a layered value when either differs.
        for (const auto& f : bandFields())
        {
            const auto& x = t.articulations[s];
            const auto& baked = b.tuning.articulations[s];
            const bool changed = x.*(f.member) != baked.*(f.member)
                                 || (f.softMember != nullptr && x.*(f.softMember) != baked.*(f.softMember));
            if (onlyChanged && ! changed)
                continue;
            auto write = [&] (const char* key, const std::array<double, kExcitationBands>& values) {
                juce::Array<juce::var> bands;
                for (double g : values)
                    bands.add (g);
                a->setProperty (key, bands);
            };
            write (f.spec.key, x.*(f.member));
            if (f.softMember != nullptr)
                write (f.softKey, x.*(f.softMember));
            any = true;
        }
        if (any)
        {
            a->setProperty ("entry", juce::String (b.slots[s].name));
            o->setProperty (idOf (b, s), juce::var (a));
        }
        else
            delete a;
    }
    return juce::var (o);
}
} // namespace

juce::String toJson (const BakeData& b, const juce::String& presetId, const Tuning& t, const juce::String& producer)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", kFormat);
    root->setProperty ("version", kVersion);
    root->setProperty ("preset", presetId);
    root->setProperty ("bake_format_version", b.formatVersion);
    root->setProperty ("date", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("producer", producer);
    root->setProperty ("cymbal", cymbalObject (b, t, true));
    root->setProperty ("articulations", articulationObject (b, t, true));
    auto* eff = new juce::DynamicObject();
    eff->setProperty ("cymbal", cymbalObject (b, t, false));
    eff->setProperty ("articulations", articulationObject (b, t, false));
    root->setProperty ("effective", juce::var (eff));
    return juce::JSON::toString (juce::var (root), false, 6) + "\n";
}

juce::String fromJson (const juce::String& text, const BakeData& b, const juce::String& presetId, Tuning& out)
{
    juce::var root;
    const auto parsed = juce::JSON::parse (text, root);
    if (parsed.failed() || ! root.isObject())
        return "This is not a tuning file (" + parsed.getErrorMessage() + ").";
    if (root["format"].toString() != kFormat)
        return "This is not a Cymbalis tuning file.";
    if ((int) root["version"] != kVersion)
        return "This tuning file has version " + root["version"].toString() + "; this build reads version "
               + juce::String (kVersion) + ".";
    if (root["preset"].toString() != presetId)
        return "This tuning file is for " + root["preset"].toString() + ", but the current preset is " + presetId
               + ". Choose that preset first.";
    if ((int) root["bake_format_version"] != b.formatVersion)
        return "This tuning file was made for bake format " + root["bake_format_version"].toString()
               + "; this preset's bake has format " + juce::String (b.formatVersion) + ".";
    Tuning t = b.tuning;
    if (auto* c = root["cymbal"].getDynamicObject())
        for (const auto& p : c->getProperties())
        {
            const auto key = p.name.toString();
            if (key == kFrameDecayRestartKey)
            {
                t.cymbal.frameDecayRestart = (bool) p.value;
                continue;
            }
            if (key == kEdgeEnvelopeKey)
            {
                if (! applies (Needs::envelope, b))
                    return juce::String (kEdgeEnvelopeKey) + ": this preset has no NPZ edge envelope.";
                t.cymbal.edgeEnvelope = (bool) p.value;
                continue;
            }
            if (key == kHighPassKey)
            {
                t.cymbal.highPassOn = (bool) p.value;
                continue;
            }
            if (key == kRehitWeightsKey)
            {
                auto* w = p.value.getDynamicObject();
                if (w == nullptr)
                    return juce::String (kRehitWeightsKey) + " must name articulations and their weights.";
                for (const auto& q : w->getProperties())
                {
                    int slot = -1;
                    for (size_t s = 0; s < b.slots.size(); ++s)
                        if (idOf (b, s) == q.name.toString())
                            slot = (int) s;
                    if (slot < 0)
                        return "This preset has no articulation \"" + q.name.toString() + "\" (" + kRehitWeightsKey + ").";
                    const double x = (double) q.value;
                    if (! std::isfinite (x) || x < 0.0)
                        return juce::String (kRehitWeightsKey) + " values must be 0 or more.";
                    t.cymbal.rehitWeight[(size_t) slot] = x;
                }
                continue;
            }
            bool known = false;
            for (const auto& f : cymbalBandFields())
                if (key == f.spec.key)
                {
                    const auto error = readBands (p.value, f.spec, key, t.cymbal.*(f.member));
                    if (error.isNotEmpty())
                        return error;
                    known = true;
                }
            if (known)
                continue;
            for (const auto& f : cymbalFields())
                if (key == f.key)
                {
                    t.cymbal.*(f.member) = (double) p.value;
                    known = true;
                }
            if (! known)
                return "Unknown value \"" + key + "\" in the tuning file.";
        }
    if (auto* a = root["articulations"].getDynamicObject())
        for (const auto& p : a->getProperties())
        {
            int slot = -1;
            for (size_t s = 0; s < b.slots.size(); ++s)
                if (idOf (b, s) == p.name.toString())
                    slot = (int) s;
            if (slot < 0)
                return "This preset has no articulation \"" + p.name.toString() + "\".";
            if (auto* v = p.value.getDynamicObject())
            {
                // A layered value without its SOFT layer (a file from before the layers) is the
                // same at every velocity: both layers take it.
                for (const auto& f : bandFields())
                    if (f.softMember != nullptr && v->hasProperty (f.spec.key) && ! v->hasProperty (f.softKey))
                    {
                        const auto error = readBands (v->getProperty (f.spec.key), f.spec, p.name.toString() + "." + f.spec.key,
                                                      t.articulations[(size_t) slot].*(f.softMember));
                        if (error.isNotEmpty())
                            return error;
                    }
                for (const auto& q : v->getProperties())
                {
                    if (q.name.toString() == "entry")
                        continue;
                    bool known = false;
                    for (const auto& f : bandFields())
                    {
                        const bool hard = q.name.toString() == f.spec.key;
                        const bool soft = f.softKey != nullptr && q.name.toString() == f.softKey;
                        if (! hard && ! soft)
                            continue;
                        const auto error = readBands (q.value, f.spec, p.name.toString() + "." + q.name.toString(),
                                                      t.articulations[(size_t) slot].*(hard ? f.member : f.softMember));
                        if (error.isNotEmpty())
                            return error;
                        known = true;
                    }
                    if (known)
                        continue;
                    for (const auto& f : articulationFields())
                        if (q.name.toString() == f.key)
                        {
                            t.articulations[(size_t) slot].*(f.member) = (double) q.value;
                            known = true;
                        }
                    if (! known)
                        return "Unknown value \"" + q.name.toString() + "\" for " + p.name.toString() + ".";
                }
            }
        }
    if (! (t.cymbal.ringLength > 0.0) || ! (t.cymbal.edgeRemapExponent > 0.0) || ! (t.cymbal.wanderRateHz > 0.0)
        || ! (t.cymbal.tauRefMs > 0.0 || ! applies (Needs::noVelocityTables, b)))
        return "The tuning file has a ring length, edge capture curve, wander rate or tau_ref that is not above 0.";
    if (! (t.cymbal.stereoWidth >= 0.0 && t.cymbal.stereoWidth <= kWidthMax))
        return "The tuning file's stereo_width must lie in 0 to 200 %.";
    if (! (t.cymbal.highPassHz >= kHighPassMinHz && t.cymbal.highPassHz <= kHighPassMaxHz))
        return "The tuning file's right_highpass_hz must lie in 20 to 300 Hz.";
    out = t;
    return {};
}

bool isModified (const BakeData& b, const Tuning& t)
{
    for (const auto& f : cymbalFields())
        if (! juce::exactlyEqual (t.cymbal.*(f.member), b.tuning.cymbal.*(f.member)))
            return true;
    if (t.cymbal.frameDecayRestart != b.tuning.cymbal.frameDecayRestart || t.cymbal.edgeEnvelope != b.tuning.cymbal.edgeEnvelope
        || t.cymbal.highPassOn != b.tuning.cymbal.highPassOn)
        return true;
    if (! sameRehitWeights (b, t.cymbal, b.tuning.cymbal))
        return true;
    for (const auto& f : cymbalBandFields())
        if (t.cymbal.*(f.member) != b.tuning.cymbal.*(f.member))
            return true;
    for (size_t s = 0; s < b.slots.size(); ++s)
    {
        for (const auto& f : articulationFields())
            if (! juce::exactlyEqual (t.articulations[s].*(f.member), b.tuning.articulations[s].*(f.member)))
                return true;
        for (const auto& f : bandFields())
            if (t.articulations[s].*(f.member) != b.tuning.articulations[s].*(f.member)
                || (f.softMember != nullptr && t.articulations[s].*(f.softMember) != b.tuning.articulations[s].*(f.softMember)))
                return true;
    }
    return false;
}

} // namespace cymbalis::tuning

#include "TuningFields.h"


namespace cymbalis::tuning
{

const char* groupName (Group g)
{
    switch (g)
    {
        case Group::cymbal: return "CYMBAL";
        case Group::wander: return "WANDER";
        case Group::mf: return "MF BUILD-UP";
        case Group::lf: return "LF BUILD-UP";
        case Group::edgeEnvelope: return "EDGE ENVELOPE";
    }
    return "";
}

bool applies (Needs needs, const BakeData& b)
{
    switch (needs)
    {
        case Needs::always: return true;
        case Needs::npzEdge: return b.edgeNpz.present && b.edgeNpz.useVelw;
        case Needs::envelope: return b.edgeNpz.present && b.edgeNpz.envelopeActive;
        case Needs::noVelocityTables:
            for (const auto& s : b.slots)
                if (! s.hasBowVp)
                    return true;
            return false;
    }
    return false;
}

const std::vector<Field>& cymbalFields()
{
    using C = CymbalTuning;
    static const std::vector<Field> fields {
        { "output_gain_db", "Output gain", "dB", Group::cymbal, Needs::always, -24.0, 12.0, 1, &C::outputGainDb, false },
        { "edge_velocity_remap_exponent", "Edge capture curve", "", Group::cymbal, Needs::npzEdge, 0.25, 2.0, 2, &C::edgeRemapExponent, false },
        { "tau_ref_ms", "Bow brightness vs velocity (tau_ref)", "ms", Group::cymbal, Needs::noVelocityTables, 0.05, 8.0, 2, &C::tauRefMs, false },
        { "ring_length", "Ring length (x t60_scale)", "x", Group::cymbal, Needs::always, 0.5, 2.0, 2, &C::ringLength, true },

        { "wander_depth", "Wander depth", "", Group::wander, Needs::always, 0.0, 4.0, 2, &C::wanderDepth, false },
        { "wander_rate_hz", "Wander rate", "Hz", Group::wander, Needs::always, 0.05, 10.0, 2, &C::wanderRateHz, false },
        { "edge_wander_depth", "Edge wander depth", "", Group::wander, Needs::always, 0.0, 4.0, 2, &C::edgeWanderDepth, false },
        { "edge_wander_rate", "Edge wander rate", "", Group::wander, Needs::always, 0.0, 4.0, 2, &C::edgeWanderRate, false },

        { "mf_sens_depth", "Depth", "", Group::mf, Needs::always, 0.0, 2.0, 3, &C::mfDepth, false },
        { "mf_sens_tau", "Tau", "s", Group::mf, Needs::always, 0.05, 20.0, 2, &C::mfTau, false },
        { "mf_min_scale", "Min scale", "", Group::mf, Needs::always, 0.0, 1.0, 2, &C::mfMin, false },
        { "mf_max_scale", "Max scale (above 1 = boost)", "", Group::mf, Needs::always, 0.1, 3.0, 2, &C::mfMax, false },
        { "mf_bow_weight", "Bow weight", "", Group::mf, Needs::always, 0.0, 2.0, 2, &C::mfWBow, false },
        { "mf_edge_weight", "Edge weight", "", Group::mf, Needs::always, 0.0, 2.0, 2, &C::mfWEdge, false },
        { "mf_bell_weight", "Bell weight", "", Group::mf, Needs::always, 0.0, 2.0, 2, &C::mfWBell, false },
        { "mf_lo_hz", "Low edge", "Hz", Group::mf, Needs::always, 50.0, 8000.0, 0, &C::mfLoHz, false },
        { "mf_hi_hz", "High edge", "Hz", Group::mf, Needs::always, 200.0, 16000.0, 0, &C::mfHiHz, false },

        { "lf_sens_depth", "Depth", "", Group::lf, Needs::always, 0.0, 2.0, 3, &C::lfDepth, false },
        { "lf_sens_tau", "Tau", "s", Group::lf, Needs::always, 0.05, 20.0, 2, &C::lfTau, false },
        { "lf_min_scale", "Min scale", "", Group::lf, Needs::always, 0.0, 1.0, 2, &C::lfMin, false },
        { "lf_max_scale", "Max scale (above 1 = boost)", "", Group::lf, Needs::always, 0.1, 3.0, 2, &C::lfMax, false },
        { "lf_cutoff_hz", "Cutoff", "Hz", Group::lf, Needs::always, 100.0, 8000.0, 0, &C::lfCutoffHz, false },
        { "lf_bow_weight", "Bow weight", "", Group::lf, Needs::always, 0.0, 2.0, 2, &C::lfWBow, false },
        { "lf_edge_weight", "Edge weight", "", Group::lf, Needs::always, 0.0, 2.0, 2, &C::lfWEdge, false },
        { "lf_bell_weight", "Bell weight", "", Group::lf, Needs::always, 0.0, 2.0, 2, &C::lfWBell, false },
        { "lf_wander_rate_boost", "Wander-rate boost", "", Group::lf, Needs::always, 0.0, 2.0, 2, &C::lfWanderRateBoost, false },

        { "edge_band_env_onset_s", "Window", "s", Group::edgeEnvelope, Needs::envelope, 0.1, 30.0, 2, &C::edgeEnvWindowS, false },
    };
    return fields;
}

const std::vector<ArticulationField>& articulationFields()
{
    using A = ArticulationTuning;
    static const std::vector<ArticulationField> fields {
        { kLevelKey, "Level at velocity 1.0", -40.0, 40.0, 2, &A::levelDb },
        { kVel06Key, "Offset at velocity 0.6", -24.0, 24.0, 1, &A::vel06Db },
        { kVel03Key, "Offset at velocity 0.3", -24.0, 24.0, 1, &A::vel03Db },
    };
    return fields;
}

namespace
{
juce::String idOf (const BakeData& b, size_t s) { return articulationId (b.slots[s].name, b.slots[s].overlay); }

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
            bool known = false;
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
                for (const auto& q : v->getProperties())
                {
                    if (q.name.toString() == "entry")
                        continue;
                    bool known = false;
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
    if (! (t.cymbal.ringLength > 0.0) || ! (t.cymbal.edgeRemapExponent > 0.0) || ! (t.cymbal.wanderRateHz > 0.0)
        || ! (t.cymbal.tauRefMs > 0.0 || ! applies (Needs::noVelocityTables, b)))
        return "The tuning file has a ring length, edge capture curve, wander rate or tau_ref that is not above 0.";
    out = t;
    return {};
}

bool isModified (const BakeData& b, const Tuning& t)
{
    for (const auto& f : cymbalFields())
        if (! juce::exactlyEqual (t.cymbal.*(f.member), b.tuning.cymbal.*(f.member)))
            return true;
    if (t.cymbal.frameDecayRestart != b.tuning.cymbal.frameDecayRestart)
        return true;
    for (size_t s = 0; s < b.slots.size(); ++s)
        for (const auto& f : articulationFields())
            if (! juce::exactlyEqual (t.articulations[s].*(f.member), b.tuning.articulations[s].*(f.member)))
                return true;
    return false;
}

} // namespace cymbalis::tuning

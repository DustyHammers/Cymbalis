#include "Bake.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cymbalis
{

namespace
{
constexpr int kFormatVersion = 1;

std::string fmt(const char* f, double a)
{
    char buf[128];
    std::snprintf(buf, sizeof buf, f, a);
    return buf;
}

std::string hex64(uint64_t v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

uint64_t fnv1a64(const std::vector<double>& values)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (double d : values)
    {
        uint64_t bits;
        std::memcpy(&bits, &d, sizeof bits);
        for (int b = 0; b < 8; ++b)
        {
            h ^= (bits >> (8 * b)) & 0xffu;
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}

uint64_t parseHexU64(const std::string& s)
{
    if (s.size() < 3 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X'))
        throw BakeError("bad hex value '" + s + "'");
    uint64_t v = 0;
    for (size_t i = 2; i < s.size(); ++i)
    {
        const char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = unsigned(c - '0');
        else if (c >= 'a' && c <= 'f') d = unsigned(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = unsigned(c - 'A' + 10);
        else throw BakeError("bad hex value '" + s + "'");
        if (i - 2 >= 16) throw BakeError("hex value too long '" + s + "'");
        v = (v << 4) | d;
    }
    return v;
}

Pcg64::u128 parseHexU128(const std::string& s)
{
    if (s.size() != 34 || s[0] != '0' || s[1] != 'x')
        throw BakeError("expected a 128-bit hex value, got '" + s + "'");
    const uint64_t hi = parseHexU64("0x" + s.substr(2, 16));
    const uint64_t lo = parseHexU64("0x" + s.substr(18, 16));
    return Pcg64::make128(hi, lo);
}

// Reads {"shape": [...], "fnv1a64": "0x...", "data": [...]} and verifies the checksum.
std::vector<double> readArray(const json::Value& v, const std::string& name, std::vector<size_t> expectShape,
                              std::vector<std::string>& report)
{
    const auto& shape = v.at("shape").array();
    if (shape.size() != expectShape.size())
        throw BakeError(name + ": unexpected rank");
    size_t count = 1;
    for (size_t i = 0; i < shape.size(); ++i)
    {
        const auto dim = static_cast<size_t>(shape[i].number());
        if (expectShape[i] != 0 && dim != expectShape[i])
            throw BakeError(name + ": unexpected shape");
        count *= dim;
    }
    const auto& data = v.at("data").array();
    if (data.size() != count)
        throw BakeError(name + ": data length does not match shape");
    std::vector<double> out;
    out.reserve(count);
    for (const auto& x : data)
        out.push_back(x.number());
    const uint64_t want = parseHexU64(v.at("fnv1a64").string());
    const uint64_t got = fnv1a64(out);
    if (want != got)
        throw BakeError(name + ": checksum mismatch after parsing (baked " + hex64(want) + ", parsed " + hex64(got) +
                        ") - values were not read bit-exactly");
    report.push_back("checksum ok  " + name + " [" + std::to_string(count) + "]");
    return out;
}

VelocityTable readVelocityTable(const json::Value& v, const std::string& name, size_t numModes,
                                std::vector<std::string>& report)
{
    VelocityTable t;
    t.norms = readArray(v.at("vel_norms"), name + ".vel_norms", {0}, report);
    t.numVel = static_cast<int>(t.norms.size());
    if (t.numVel < 2)
        throw BakeError(name + ": need at least two velocity columns");
    t.curves = readArray(v.at("curves"), name + ".curves", {numModes, size_t(t.numVel)}, report);
    return t;
}

double eff(const json::Value& effective, const char* key)
{
    const json::Value& v = effective.at(key);
    if (v.isBool()) return v.boolean() ? 1.0 : 0.0;
    return v.number();
}

// A key that only bakes of presets using that mechanism have.
double effOpt(const json::Value& effective, const char* key, double fallback)
{
    const json::Value* v = effective.find(key);
    if (v == nullptr || v->isNull()) return fallback;
    if (v->isBool()) return v->boolean() ? 1.0 : 0.0;
    return v->number();
}

std::vector<size_t> shapeOf(const json::Value& v)
{
    std::vector<size_t> out;
    for (const auto& d : v.at("shape").array())
        out.push_back(static_cast<size_t>(d.number()));
    return out;
}

// The NPZ edge block written by tools/bake_preset.py (602 and Bettis).
void readEdgeNpz(const json::Value& z, size_t numModes, EdgeNpz& out, std::vector<std::string>& rep)
{
    out.present = true;
    out.velocities = readArray(z.at("velocities"), "edge_npz.velocities", {0}, rep);
    const size_t nv = out.velocities.size();
    if (nv < 2)
        throw BakeError("edge_npz: need at least two velocities");
    for (size_t i = 1; i < nv; ++i)
        if (!(out.velocities[i] > out.velocities[i - 1]))
            throw BakeError("edge_npz: velocities must be ascending");
    out.velw = readArray(z.at("velw"), "edge_npz.velw", {nv, numModes}, rep);
    const auto shape = shapeOf(z.at("envelopes"));
    if (shape.size() != 3 || shape[0] != nv || shape[1] < 1 || shape[2] < 1)
        throw BakeError("edge_npz.envelopes: expected velocities x frames x bands");
    out.numFrames = static_cast<int>(shape[1]);
    out.numBands = static_cast<int>(shape[2]);
    out.envelopes = readArray(z.at("envelopes"), "edge_npz.envelopes", {nv, shape[1], shape[2]}, rep);
    const auto band = readArray(z.at("band_index"), "edge_npz.band_index", {numModes}, rep);
    out.bandIndex.resize(numModes);
    for (size_t i = 0; i < numModes; ++i)
    {
        out.bandIndex[i] = static_cast<int>(band[i]);
        if (out.bandIndex[i] < 0 || out.bandIndex[i] >= out.numBands)
            throw BakeError("edge_npz.band_index out of range");
    }
    out.frameDurS = z.at("frame_dur_s").number();
    out.useVelw = z.at("use_velw").boolean();
    out.normalize = z.at("velw_normalize").boolean();
    out.levelVelGainPower = z.at("level_vel_gain_power").number();
    if (!z.at("rms_correction").isNull())
        out.rmsCorrection = readArray(z.at("rms_correction"), "edge_npz.rms_correction", {shape[2]}, rep);
    out.envelopeActive = z.at("envelope_active").boolean();
    out.onsetS = z.at("onset_s").number();
    out.frameDecayPeriodS = z.at("frame_decay_period_s").number();
    if (!(out.frameDurS > 0.0) || !(out.frameDecayPeriodS > 0.0) || !(out.onsetS >= 0.0))
        throw BakeError("edge_npz: frame lengths must be positive and onset_s >= 0");
    rep.push_back("edge npz     " + std::to_string(nv) + " velocities, " + std::to_string(out.numFrames) + " frames x " +
                  std::to_string(out.numBands) + " bands of " + fmt("%.3f s", out.frameDurS) +
                  (out.envelopeActive ? fmt(", band envelope for %.1f s after an edge strike", out.onsetS)
                                      : std::string(", no band envelope")));
}

// Mechanisms present in the Python engine but not implemented here (PORT_MAP stages 3+).
std::vector<std::string> unsupportedMechanisms(const json::Value& e)
{
    std::vector<std::string> out;
    if (e.at("articulation").string() != "stick") out.push_back("brush articulation");
    if (!e.at("vel_participation").boolean() && e.find("tau_ref_ms") == nullptr)
        out.push_back("hertz_vel_scale (no velocity participation table, and the bake has no tau_ref_ms)");
    if (eff(e, "contact_tau_ms") > 0.0) out.push_back("contact sinc stick profile (contact_tau_ms)");
    if (eff(e, "tip_contact_ms") > 0.0 && !e.at("permode_contact").boolean())
        out.push_back("Gaussian contact force (tip_contact_ms > 0 without permode_contact)");
    if (eff(e, "release_gain") > 0.0 && eff(e, "tip_contact_ms") > 0.0 && !e.at("permode_contact").boolean())
        out.push_back("contact release pulse");
    if (eff(e, "rate_jitter") > 0.0) out.push_back("wander rate jitter (draws extra random numbers)");
    if (eff(e, "nl_amount") > 1e-6) out.push_back("NL redistribution (nl_amount > 0; off in the plugin)");
    if (eff(e, "hf_bloom_modes") > 0) out.push_back("HF bloom bank");
    if (eff(e, "sparkle_clusters") > 0 && eff(e, "sparkle_depth") > 0.0) out.push_back("sparkle");
    if (eff(e, "atk_noise") > 0.0) out.push_back("attack noise burst");
    // An edge band envelope without NPZ data does nothing in Python (_eb_active is never
    // set, lines 574 and 610), so only the NPZ case needs the edge_npz block.
    return out;
}

double clamp01(double x) { return std::min(std::max(x, 0.0), 1.0); }

// numpy's pairwise summation (pairwise_sum in loops_utils.h), as used by add.reduce
// along a contiguous axis. A row sum is the additive identity (0) plus this sum.
double numpyPairwiseSum(const double* a, size_t n)
{
    if (n < 8)
    {
        double res = 0.0;
        for (size_t i = 0; i < n; ++i)
            res += a[i];
        return res;
    }
    if (n <= 128)
    {
        double r[8];
        for (int j = 0; j < 8; ++j)
            r[j] = a[j];
        size_t i = 8;
        for (; i < n - (n % 8); i += 8)
            for (int j = 0; j < 8; ++j)
                r[j] += a[i + size_t(j)];
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i)
            res += a[i];
        return res;
    }
    size_t n2 = n / 2;
    n2 -= n2 % 8;
    return numpyPairwiseSum(a, n2) + numpyPairwiseSum(a + n2, n - n2);
}

// _apply_strike lines 1073-1091: per-mode Gaussian force envelopes, DC-normalised per mode.
std::vector<double> computeContactForce(const std::vector<double>& freq, double position, const EngineParams& p,
                                        int& lenOut)
{
    const size_t n = freq.size();
    const double pos = clamp01(position);
    const double edgeFrac = clamp01(1.0 - pos / 0.5);
    const double tau0 = p.tipTau0 + edgeFrac * (p.shldTau0 - p.tipTau0);
    const double tauM = p.tipTauMax + edgeFrac * (p.shldTauMax - p.tipTauMax);
    const double fMax = *std::max_element(freq.begin(), freq.end());
    std::vector<double> tau(n), sig(n);
    double sigMax = -1.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double fNorm = std::sqrt(freq[i] / (fMax + 1e-9));
        tau[i] = tauM - (tauM - tau0) * fNorm;
        sig[i] = std::max(tau[i] * p.sigmaRatio, 1.0);
        sigMax = std::max(sigMax, sig[i]);
    }
    const int len = static_cast<int>(tauM + 3.0 * sigMax) + 1;
    std::vector<double> f(n * size_t(len));
    for (size_t i = 0; i < n; ++i)
    {
        double* row = f.data() + i * size_t(len);
        for (int k = 0; k < len; ++k)
        {
            const double x = (static_cast<double>(k) - tau[i]) / sig[i];
            row[k] = std::exp(-0.5 * (x * x));
        }
        const double denom = (0.0 + numpyPairwiseSum(row, size_t(len))) + 1e-9;
        for (int k = 0; k < len; ++k)
            row[k] = row[k] / denom;
    }
    lenOut = len;
    return f;
}

Pcg64 readStream(const json::Value& s, const std::string& name, std::vector<std::string>& report)
{
    Pcg64 g(parseHexU128(s.at("state").string()), parseHexU128(s.at("inc").string()));
    Pcg64 check = g;
    const auto& raw = s.at("first_raw_u64").array();
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const uint64_t want = parseHexU64(raw[i].string());
        const uint64_t got = check.nextU64();
        if (want != got)
            throw BakeError("PCG64 stream '" + name + "': raw output " + std::to_string(i) + " is " + hex64(got) +
                            ", numpy gave " + hex64(want));
    }
    report.push_back("pcg64 ok     " + name + ": first " + std::to_string(raw.size()) + " raw outputs match numpy");
    return g;
}

// numpy uniform() is `low + range * u`. Depending on how numpy was compiled, that
// expression may or may not be fused into one FMA. Both forms are numpy's uniform();
// the check accepts either, as long as one form matches every element.
enum class UniformForm { Unfused, Fused };

const char* formName(UniformForm f) { return f == UniformForm::Unfused ? "low + range*u" : "fma(range, u, low)"; }

double uniformAs(UniformForm f, double low, double range, double u)
{
    return f == UniformForm::Unfused ? low + range * u : std::fma(range, u, low);
}

// Regenerates `uniform(low, high, n)` (optionally times a factor / a +-1 choice) and
// compares it with the baked numpy array. Returns the matching form.
template <typename Gen>
UniformForm verifyAgainstNumpy(const std::vector<double>& baked, const char* name, Gen&& generate)
{
    for (UniformForm f : {UniformForm::Unfused, UniformForm::Fused})
    {
        const std::vector<double> mine = generate(f);
        if (mine.size() == baked.size() && std::memcmp(mine.data(), baked.data(), mine.size() * sizeof(double)) == 0)
            return f;
    }
    // Report the first mismatch for the unfused form.
    const std::vector<double> mine = generate(UniformForm::Unfused);
    for (size_t i = 0; i < mine.size() && i < baked.size(); ++i)
        if (mine[i] != baked[i])
            throw BakeError(std::string("PCG64/numpy mismatch in ") + name + " at index " + std::to_string(i) + ": " +
                            fmt("%.17g", mine[i]) + " vs numpy " + fmt("%.17g", baked[i]));
    throw BakeError(std::string("PCG64/numpy mismatch in ") + name + " (length)");
}
} // namespace

int BakeData::slotIndex(const std::string& name) const
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i].name == name) return static_cast<int>(i);
    return -1;
}

namespace
{
std::shared_ptr<const BakeData> buildBake(const json::Value& root, const std::string& path)
{
    auto bake = std::make_shared<BakeData>();
    BakeData& b = *bake;
    auto& rep = b.report;
    b.path = path;

    try
    {
        if (root.at("format").string() != "cymbalis-bake")
            throw BakeError("not a cymbalis bake file");
        b.formatVersion = static_cast<int>(root.at("format_version").number());
        if (b.formatVersion != kFormatVersion)
            throw BakeError("unsupported bake format_version " + std::to_string(b.formatVersion));

        b.sampleRate = root.at("sample_rate").number();
        b.numModes = static_cast<int>(root.at("n_modes").number());
        const size_t n = static_cast<size_t>(b.numModes);
        const auto& c = root.at("constants");
        b.log1000 = c.at("log1000").number();
        b.twoPi = c.at("two_pi").number();
        b.ampSkipThreshold = c.at("amp_skip_threshold").number();
        b.overrides = root.at("overrides");

        const auto& sh = root.at("shared");
        b.freqHz = readArray(sh.at("freq_hz"), "shared.freq_hz", {n}, rep);
        b.t60S = readArray(sh.at("t60_s"), "shared.t60_s", {n}, rep);
        b.driftHz = readArray(sh.at("drift_hz"), "shared.drift_hz", {n}, rep);
        b.excNorm = readArray(sh.at("exc_norm"), "shared.exc_norm", {n}, rep);
        b.edgeWeight = readArray(sh.at("edge_weight"), "shared.edge_weight", {n}, rep);
        b.bellWeight = readArray(sh.at("bell_weight"), "shared.bell_weight", {n}, rep);
        b.pickL = readArray(sh.at("pick_l"), "shared.pick_l", {n}, rep);
        b.pickR = readArray(sh.at("pick_r"), "shared.pick_r", {n}, rep);
        b.lfoRateHz = readArray(sh.at("lfo_rate_hz"), "shared.lfo_rate_hz", {n}, rep);
        b.lfoPhase = readArray(sh.at("lfo_phase"), "shared.lfo_phase", {n}, rep);
        // Arrays for later stages: parsed and checksummed now so a bad bake fails early.
        for (const char* key : {"mode_id", "nl_lf_mask", "nl_mf_mask", "nl_hf_mask", "lf_sigmoid", "mf_bandpass",
                                "pk_t_bow_s", "pk_t_edge_s", "vel_tilt_mode_band", "bow_onset_mode_band"})
        {
            const json::Value& v = sh.at(key);
            if (!v.isNull()) readArray(v, std::string("shared.") + key, {n}, rep);
        }
        for (const char* key : {"vel_tilt_velocities", "vel_tilt_reference"})
        {
            const json::Value& v = sh.at(key);
            if (!v.isNull()) readArray(v, std::string("shared.") + key, {0}, rep);
        }
        if (const json::Value& v = sh.at("bow_onset_band_edges_hz"); !v.isNull())
            b.bowOnsetBandEdges = readArray(v, "shared.bow_onset_band_edges_hz", {0}, rep);
        if (!sh.at("vel_tilt_fractions").isNull())
            readArray(sh.at("vel_tilt_fractions"), "shared.vel_tilt_fractions", {0, 0}, rep);

        b.modeStepRe = readArray(sh.at("mode_step_re"), "shared.mode_step_re", {n}, rep);
        b.modeStepIm = readArray(sh.at("mode_step_im"), "shared.mode_step_im", {n}, rep);

        const auto sos = readArray(root.at("hp_right").at("sos"), "hp_right.sos", {1, 6}, rep);
        for (int i = 0; i < 6; ++i)
            b.hpSos[i] = sos[size_t(i)];
        if (b.hpSos[3] != 1.0)
            throw BakeError("hp_right.sos: a0 must be 1");

        // ---- engine-wide parameters: every slot must agree (only overlays differ per slot)
        const auto& slotArr = root.at("slots").array();
        if (slotArr.empty())
            throw BakeError("bake has no slots");
        const json::Value& e0 = slotArr.at(0).at("effective");
        for (const auto& s : slotArr)
            for (const auto& kv : e0.object())
            {
                if (kv.first == "bow_onset_gains_active" || kv.first == "stick_gain") continue; // overlay-specific
                const json::Value* other = s.at("effective").find(kv.first);
                if (other == nullptr || !json::equal(kv.second, *other))
                    throw BakeError("engine-wide parameter '" + kv.first + "' differs between slots " +
                                    slotArr.at(0).at("name").string() + " and " + s.at("name").string());
            }
        EngineParams& ep = b.params;
        ep.contactPermode = eff(e0, "tip_contact_ms") > 0.0 && e0.at("permode_contact").boolean();
        ep.tipTau0 = eff(e0, "tip_tau0");
        ep.tipTauMax = eff(e0, "tip_tau_max");
        ep.shldTau0 = eff(e0, "shld_tau0");
        ep.shldTauMax = eff(e0, "shld_tau_max");
        ep.sigmaRatio = eff(e0, "sigma_ratio");
        ep.wanderDepth = eff(e0, "wander_depth");
        ep.wanderRateHz = eff(e0, "wander_rate_hz");
        ep.edgeWanderDepth = eff(e0, "edge_wander_depth");
        ep.edgeWanderRate = eff(e0, "edge_wander_rate");
        ep.lfDepth = eff(e0, "lf_sens_depth");
        ep.lfTau = eff(e0, "lf_sens_tau");
        ep.lfMin = eff(e0, "lf_min_scale");
        ep.lfMax = eff(e0, "lf_max_scale");
        ep.lfWEdge = eff(e0, "lf_edge_weight");
        ep.lfWBow = eff(e0, "lf_bow_weight");
        ep.lfWBell = eff(e0, "lf_bell_weight");
        ep.lfWanderRateBoost = eff(e0, "lf_wander_rate_boost");
        ep.lfCutoffHz = eff(e0, "lf_cutoff_hz");
        ep.velLenTilt = eff(e0, "vel_len_tilt");
        ep.velLenTiltRef = eff(e0, "vel_len_tilt_ref");
        ep.mfDepth = eff(e0, "mf_sens_depth");
        ep.mfTau = eff(e0, "mf_sens_tau");
        ep.mfMin = eff(e0, "mf_min_scale");
        ep.mfMax = eff(e0, "mf_max_scale");
        ep.mfWEdge = eff(e0, "mf_edge_weight");
        ep.mfWBow = eff(e0, "mf_bow_weight");
        ep.mfWBell = eff(e0, "mf_bell_weight");
        ep.mfLoHz = eff(e0, "mf_lo_hz");
        ep.mfHiHz = eff(e0, "mf_hi_hz");
        ep.bandEnvOnset = e0.at("band_env_onset").boolean();
        ep.birthFrac = eff(e0, "band_env_birth_frac");
        ep.velTilt = e0.at("vel_tilt").boolean();
        if (ep.bandEnvOnset)
        {
            b.pkTBow = readArray(sh.at("pk_t_bow_s"), "shared.pk_t_bow_s (used)", {n}, rep);
            if (const json::Value* byVel = sh.find("pk_t_edge_by_vel_s"); byVel != nullptr && !byVel->isNull())
            {
                b.pkTEdgeVelocities = readArray(sh.at("pk_t_edge_velocities"), "shared.pk_t_edge_velocities (used)",
                                                {0}, rep);
                const size_t nv = b.pkTEdgeVelocities.size();
                if (nv < 1)
                    throw BakeError("shared.pk_t_edge_velocities is empty");
                b.pkTEdgeByVel = readArray(*byVel, "shared.pk_t_edge_by_vel_s (used)", {nv, n}, rep);
                rep.push_back("edge pk_t    velocity-bracketed at " + std::to_string(nv) + " velocities");
            }
            else
                b.pkTEdge = readArray(sh.at("pk_t_edge_s"), "shared.pk_t_edge_s (used)", {n}, rep);
        }
        if (ep.velTilt)
        {
            b.velTiltVelocities = readArray(sh.at("vel_tilt_velocities"), "shared.vel_tilt_velocities (used)", {0}, rep);
            b.velTiltReference = readArray(sh.at("vel_tilt_reference"), "shared.vel_tilt_reference (used)", {0}, rep);
            b.velTiltBands = static_cast<int>(b.velTiltReference.size());
            b.velTiltFractions = readArray(sh.at("vel_tilt_fractions"), "shared.vel_tilt_fractions (used)",
                                           {b.velTiltVelocities.size(), size_t(b.velTiltBands)}, rep);
            const auto band = readArray(sh.at("vel_tilt_mode_band"), "shared.vel_tilt_mode_band (used)", {n}, rep);
            b.velTiltModeBand.resize(n);
            for (size_t i = 0; i < n; ++i)
            {
                b.velTiltModeBand[i] = static_cast<int>(band[i]);
                if (b.velTiltModeBand[i] < 0 || b.velTiltModeBand[i] >= b.velTiltBands)
                    throw BakeError("vel_tilt_mode_band out of range");
            }
        }
        ep.tauRefMs = effOpt(e0, "tau_ref_ms", 0.0);
        if (e0.at("edge_npz").boolean())
        {
            const json::Value* z = root.find("edge_npz");
            if (z == nullptr || z->isNull())
                throw BakeError("the preset uses NPZ edge data but the bake has no edge_npz block");
            readEdgeNpz(*z, n, b.edgeNpz, rep);
            // Python builds the envelope only with an edge band envelope file (line 691); a
            // plugin bake can switch it on itself, and says why (Swish).
            const json::Value* setBy = z->find("envelope_set_by");
            if (b.edgeNpz.envelopeActive != e0.at("edge_band_env").boolean() && (setBy == nullptr || setBy->isNull()))
                throw BakeError("edge_npz.envelope_active does not match edge_band_env");
            if (setBy != nullptr && ! setBy->isNull())
                rep.push_back("edge env     envelope_active set by the bake: " + setBy->string());
        }
        else if (e0.at("edge_band_env").boolean())
            rep.push_back("edge env     edge band envelope without NPZ data: no effect, as in Python");
        if (ep.mfDepth > 0.0)
        {
            if (sh.at("mf_bandpass").isNull())
                throw BakeError("mf_sens_depth > 0 but the bake has no mf_bandpass");
            b.mfBandpass = readArray(sh.at("mf_bandpass"), "shared.mf_bandpass (used)", {n}, rep);
        }
        if (ep.lfDepth > 0.0)
        {
            if (sh.at("lf_sigmoid").isNull())
                throw BakeError("lf_sens_depth > 0 but the bake has no lf_sigmoid");
            b.lfSigmoid = readArray(sh.at("lf_sigmoid"), "shared.lf_sigmoid (used)", {n}, rep);
        }
        rep.push_back("params ok    engine-wide parameters identical across slots");

        // ---- slots
        for (const auto& s : slotArr)
        {
            SlotData slot;
            slot.name = s.at("name").string();
            slot.overlay = s.at("overlay").isNull() ? std::string() : s.at("overlay").string();
            // Optional (plugin bake): an explicit articulation id, for an entry that copies
            // another's data ("copy_of"), such as the 602's CENTER (a copy of BOW).
            const json::Value* idKey = s.find("articulation");
            slot.id = idKey != nullptr && ! idKey->isNull() ? idKey->string() : articulationId(slot.name, slot.overlay);
            if (slot.id.empty())
                throw BakeError("slot " + slot.name + ": empty articulation id");
            for (const auto& other : b.slots)
                if (other.id == slot.id)
                    throw BakeError("slots " + other.name + " and " + slot.name + " have the same articulation id '" +
                                    slot.id + "'");
            if (const json::Value* copyOf = s.find("copy_of"); copyOf != nullptr && ! copyOf->isNull())
            {
                slot.copyOf = copyOf->string();
                rep.push_back("copy         slot " + slot.name + " (" + slot.id + "): a copy of " + slot.copyOf + "'s data");
            }
            slot.position = s.at("position").number();
            slot.calibScalar = s.at("calib_scalar").number();
            // Optional (plugin bake): the level trim of this slot in dB, measured with
            // tools/cymbalis_trims and stored by tools/set_level_trims.py; editable by hand.
            if (const auto* trim = s.find("level_trim"); trim != nullptr && !trim->isNull())
            {
                const double db = trim->at("gain_db").number();
                if (!std::isfinite(db) || std::fabs(db) > 60.0)
                    throw BakeError("slot " + slot.name + ": level_trim.gain_db must be a finite value within 60 dB");
                slot.levelTrim = std::pow(10.0, db / 20.0);
                rep.push_back("level trim   slot " + slot.name + ": " + fmt("%+.2f dB", db));
            }
            // Optional (plugin bake, docs/TUNING.md): this articulation's excitation gain per
            // one-third-octave band, written by tools/apply_tuning.py.
            if (const auto* bands = s.find("excitation_band_gains_db"); bands != nullptr && ! bands->isNull())
            {
                const auto& edges = bands->at("band_edges_hz").array();
                const auto& gains = bands->at("gains_db").array();
                if (edges.size() != size_t(kExcitationBands + 1) || gains.size() != size_t(kExcitationBands))
                    throw BakeError("slot " + slot.name + ": excitation_band_gains_db needs " +
                                    std::to_string(kExcitationBands + 1) + " band edges and " +
                                    std::to_string(kExcitationBands) + " gains");
                for (int j = 0; j <= kExcitationBands; ++j)
                    if (std::fabs(edges[size_t(j)].number() - excitationBandEdge(j)) > 0.5)
                        throw BakeError("slot " + slot.name + ": excitation_band_gains_db band edges differ from the "
                                        "engine's one-third-octave bands");
                for (int j = 0; j < kExcitationBands; ++j)
                {
                    const double g = gains[size_t(j)].number();
                    if (! std::isfinite(g) || std::fabs(g) > 60.0)
                        throw BakeError("slot " + slot.name + ": excitation band gains must be finite, within 60 dB");
                    slot.articulation.bandDb[size_t(j)] = g;
                }
                rep.push_back("band gains   slot " + slot.name + ": excitation gain per one-third-octave band");
            }
            // Optional (plugin bake, docs/TUNING.md): this articulation's onset shift per
            // one-third-octave band (delay and speed), written by tools/apply_tuning.py: the
            // HARD layer (delay_ms, speed) and the SOFT layer (delay_ms_soft, speed_soft; the
            // HARD values where the bake has none).
            if (const auto* shift = s.find("onset_shift"); shift != nullptr && ! shift->isNull())
            {
                const auto& edges = shift->at("band_edges_hz").array();
                const auto* softD = shift->find("delay_ms_soft");
                const auto* softV = shift->find("speed_soft");
                const auto& delays = shift->at("delay_ms").array();
                const auto& speeds = shift->at("speed").array();
                const auto& delaysSoft = softD != nullptr && ! softD->isNull() ? softD->array() : delays;
                const auto& speedsSoft = softV != nullptr && ! softV->isNull() ? softV->array() : speeds;
                if (edges.size() != size_t(kExcitationBands + 1) || delays.size() != size_t(kExcitationBands) ||
                    speeds.size() != size_t(kExcitationBands) || delaysSoft.size() != size_t(kExcitationBands) ||
                    speedsSoft.size() != size_t(kExcitationBands))
                    throw BakeError("slot " + slot.name + ": onset_shift needs " + std::to_string(kExcitationBands + 1) +
                                    " band edges, " + std::to_string(kExcitationBands) + " delays and " +
                                    std::to_string(kExcitationBands) + " speeds per layer");
                for (int j = 0; j <= kExcitationBands; ++j)
                    if (std::fabs(edges[size_t(j)].number() - excitationBandEdge(j)) > 0.5)
                        throw BakeError("slot " + slot.name + ": onset_shift band edges differ from the engine's "
                                        "one-third-octave bands");
                for (int j = 0; j < kExcitationBands; ++j)
                {
                    const double d = delays[size_t(j)].number(), v = speeds[size_t(j)].number();
                    const double ds = delaysSoft[size_t(j)].number(), vs = speedsSoft[size_t(j)].number();
                    if (! std::isfinite(d) || std::fabs(d) > 1000.0 || ! std::isfinite(ds) || std::fabs(ds) > 1000.0)
                        throw BakeError("slot " + slot.name + ": onset delays must be finite, within 1000 ms");
                    if (! std::isfinite(v) || v < 1.0 / 16.0 || v > 16.0 || ! std::isfinite(vs) || vs < 1.0 / 16.0 ||
                        vs > 16.0)
                        throw BakeError("slot " + slot.name + ": onset speeds must lie in 1/16 to 16");
                    slot.articulation.delayMs[size_t(j)] = d;
                    slot.articulation.speed[size_t(j)] = v;
                    slot.articulation.delayMsSoft[size_t(j)] = ds;
                    slot.articulation.speedSoft[size_t(j)] = vs;
                }
                rep.push_back("onset shift  slot " + slot.name + ": onset delay and speed per one-third-octave band" +
                              (hasShiftLayers(slot.articulation) ? ", SOFT and HARD layers" : ""));
            }
            if (s.at("bow_vp").isNull())
            {
                if (s.at("effective").at("vel_participation").boolean())
                    throw BakeError("slot " + slot.name + ": velocity participation is on but bow_vp is missing");
                slot.hasBowVp = false;
                if (!(ep.tauRefMs > 0.0))
                    throw BakeError("slot " + slot.name + ": hertz_vel_scale needs tau_ref_ms > 0");
                rep.push_back("velocity     slot " + slot.name + fmt(": hertz_vel_scale, tau_ref %.3f ms", ep.tauRefMs));
            }
            else
                slot.bowVp = readVelocityTable(s.at("bow_vp"), "slot " + slot.name + ".bow_vp", n, rep);
            if (!s.at("bell_vp").isNull())
            {
                slot.hasBellVp = true;
                slot.bellVp = readVelocityTable(s.at("bell_vp"), "slot " + slot.name + ".bell_vp", n, rep);
            }
            const json::Value& e = s.at("effective");
            slot.onsetGainsActive = e.at("bow_onset_gains_active").boolean();
            if (slot.onsetGainsActive)
            {
                slot.onsetGains = readArray(s.at("onset_gains"), "slot " + slot.name + ".onset_gains", {0}, rep);
                if (b.bowOnsetModeBand.empty())
                {
                    const auto band = readArray(sh.at("bow_onset_mode_band"), "shared.bow_onset_mode_band (used)",
                                                {n}, rep);
                    b.bowOnsetModeBand.resize(n);
                    for (size_t i = 0; i < n; ++i)
                        b.bowOnsetModeBand[i] = static_cast<int>(band[i]);
                }
                for (int band : b.bowOnsetModeBand)
                    if (band < 0 || band >= static_cast<int>(slot.onsetGains.size()))
                        throw BakeError("slot " + slot.name + ": onset band index out of range");
            }
            slot.onsetTauS = s.at("onset_tau_s").number();
            // Attack band gains (docs/TUNING.md): the onset gains in one-third-octave bands,
            // each band the onset band that contains it, with the onset time. Python's time is
            // bow_onset_tau_ms x 0.001 (line 435), so the whole milliseconds give it back exactly.
            slot.articulation.attackMs = std::round(slot.onsetTauS * 1e9) / 1e6;
            if (slot.onsetGainsActive)
            {
                if (b.bowOnsetBandEdges.size() != slot.onsetGains.size() + 1)
                    throw BakeError("slot " + slot.name + ": the onset gains need " +
                                    std::to_string(slot.onsetGains.size() + 1) + " band edges");
                attackFromOnsetGains(slot.onsetGains.data(), static_cast<int>(slot.onsetGains.size()),
                                     b.bowOnsetBandEdges.data(), slot.articulation.attackDb);
            }
            slot.articulation.attackDbSoft = slot.articulation.attackDb; // one layer for every velocity
            // Optional (plugin bake, docs/TUNING.md): the attack band gains and time set by
            // tools/apply_tuning.py, instead of those from the onset gains.
            if (const auto* attack = s.find("attack_band_gains"); attack != nullptr && ! attack->isNull())
            {
                const auto& edges = attack->at("band_edges_hz").array();
                const auto& gains = attack->at("gains_db").array();
                const auto* soft = attack->find("gains_db_soft"); // the SOFT layer (else the HARD one)
                const auto& gainsSoft = soft != nullptr && ! soft->isNull() ? soft->array() : gains;
                if (edges.size() != size_t(kExcitationBands + 1) || gains.size() != size_t(kExcitationBands) ||
                    gainsSoft.size() != size_t(kExcitationBands))
                    throw BakeError("slot " + slot.name + ": attack_band_gains needs " +
                                    std::to_string(kExcitationBands + 1) + " band edges and " +
                                    std::to_string(kExcitationBands) + " gains per layer");
                for (int j = 0; j <= kExcitationBands; ++j)
                    if (std::fabs(edges[size_t(j)].number() - excitationBandEdge(j)) > 0.5)
                        throw BakeError("slot " + slot.name + ": attack_band_gains band edges differ from the "
                                        "engine's one-third-octave bands");
                for (int j = 0; j < kExcitationBands; ++j)
                {
                    const double g = gains[size_t(j)].number(), gs = gainsSoft[size_t(j)].number();
                    if (! std::isfinite(g) || std::fabs(g) > 60.0 || ! std::isfinite(gs) || std::fabs(gs) > 60.0)
                        throw BakeError("slot " + slot.name + ": attack band gains must be finite, within 60 dB");
                    slot.articulation.attackDb[size_t(j)] = g;
                    slot.articulation.attackDbSoft[size_t(j)] = gs;
                }
                const double ms = attack->at("time_ms").number();
                if (! std::isfinite(ms) || ms < 1.0 || ms > 10000.0)
                    throw BakeError("slot " + slot.name + ": attack_band_gains.time_ms must lie in 1 to 10000 ms");
                slot.articulation.attackMs = ms;
                slot.attackTuned = true;
                rep.push_back("attack       slot " + slot.name + ": attack gain per one-third-octave band and time");
            }
            slot.velGainPower = e.at("vel_gain_power").number();
            slot.edgeLevelBoost = e.at("edge_level_boost").number();
            // Stick click (_add_transient lines 1194-1211): its gain is the articulation's
            // stick_gain; the rest Python reads with p.get and these defaults.
            {
                const json::Value& prm = s.at("params");
                auto get = [&](const char* key, double dflt) {
                    const json::Value* v = prm.find(key);
                    const double x = v != nullptr && ! v->isNull() ? v->number() : dflt;
                    if (! std::isfinite(x))
                        throw BakeError("slot " + slot.name + ": " + key + " is not finite");
                    return x;
                };
                ArticulationTuning& a = slot.articulation;
                a.stickGain = eff(e, "stick_gain");
                a.stickLoHz = get("stick_bp_lo", 2500.0);
                a.stickHiHz = get("stick_bp_hi", 9000.0);
                a.stickLenMs = get("stick_len_ms", 8.0);
                a.stickDecayMs = get("stick_decay_ms", 3.0);
                slot.stickBowWeight = get("stick_bow_weight", 1.0);
                slot.stickBellWeight = get("stick_bell_weight", 1.0);
                slot.stickEdgeWeight = get("stick_edge_weight", 1.0);
                if (a.stickGain > 0.0)
                    rep.push_back("stick click  slot " + slot.name + fmt(": gain %.3g, ", a.stickGain) +
                                  fmt("%.0f-", a.stickLoHz) + fmt("%.0f Hz, ", a.stickHiHz) +
                                  fmt("%.3g ms, ", a.stickLenMs) + fmt("decay %.3g ms", a.stickDecayMs));
            }

            const auto missing = unsupportedMechanisms(e);
            if (!missing.empty())
            {
                std::string msg = "slot " + slot.name + " needs mechanisms this engine does not implement yet:";
                for (const auto& m : missing)
                    msg += "\n  - " + m;
                throw BakeError(msg);
            }
            rep.push_back("stage gate   slot " + slot.name + ": all active mechanisms are implemented");

            if (ep.contactPermode)
            {
                slot.contactForce = computeContactForce(b.freqHz, slot.position, ep, slot.contactLen);
                const json::Value& py = s.at("contact_force_python");
                if (py.isNull())
                    throw BakeError("slot " + slot.name + ": contact is on but the bake has no Python force matrix");
                const auto pyForce = readArray(py, "slot " + slot.name + ".contact_force_python",
                                               {n, size_t(slot.contactLen)}, rep);
                double maxRel = 0.0;
                for (size_t i = 0; i < pyForce.size(); ++i)
                    if (pyForce[i] != slot.contactForce[i])
                        maxRel = std::max(maxRel, std::fabs(pyForce[i] - slot.contactForce[i]) /
                                                      std::max(std::fabs(pyForce[i]), 1e-300));
                rep.push_back("contact      slot " + slot.name + ": " + std::to_string(slot.contactLen) +
                              "-sample force matrix " +
                              (maxRel == 0.0 ? std::string("bit-identical to Python")
                                             : fmt("matches Python to %.2e relative", maxRel)));
            }
            b.slots.push_back(std::move(slot));
        }

        // ---- RNG streams (stage 0: exact PCG64, uniform() and choice())
        const auto& rng = root.at("rng");
        const auto& streams = rng.at("streams");
        Pcg64 pick = readStream(streams.at("pick"), "pick", rep);
        Pcg64 wander = readStream(streams.at("wander"), "wander", rep);
        b.renderStream = readStream(streams.at("render"), "render", rep);
        readStream(streams.at("calibration"), "calibration", rep);

        // __init__ lines 205-207: uniform(0.6, 1.0, n) * choice([-1., 1.], n), for L then R.
        {
            const double low = 0.6, range = 1.0 - 0.6;
            auto pickPair = [&](UniformForm f) {
                Pcg64 g = pick;
                std::vector<double> out(2 * n);
                for (int side = 0; side < 2; ++side)
                {
                    std::vector<double> mag(n);
                    for (size_t i = 0; i < n; ++i)
                        mag[i] = uniformAs(f, low, range, g.nextDouble());
                    for (size_t i = 0; i < n; ++i)
                        out[side * n + i] = mag[i] * (g.choiceIndex2() == 0 ? -1.0 : 1.0);
                }
                return out;
            };
            std::vector<double> baked(b.pickL);
            baked.insert(baked.end(), b.pickR.begin(), b.pickR.end());
            const UniformForm f = verifyAgainstNumpy(baked, "pick_l/pick_r", pickPair);
            rep.push_back(std::string("numpy rng ok pick_l/pick_r regenerated bit-exactly (uniform as ") + formName(f) +
                          ", choice via 32-bit Lemire)");
        }
        // __init__ lines 209-211: uniform(0.7, 1.3, n) * wander_rate_hz, then uniform(0, 2pi, n).
        {
            const double rateHz = root.at("slots").array().at(0).at("effective").at("wander_rate_hz").number();
            auto lfo = [&](UniformForm f) {
                Pcg64 g = wander;
                std::vector<double> out(2 * n);
                const double range1 = 1.3 - 0.7;
                for (size_t i = 0; i < n; ++i)
                    out[i] = uniformAs(f, 0.7, range1, g.nextDouble()) * rateHz;
                const double range2 = b.twoPi - 0.0;
                for (size_t i = 0; i < n; ++i)
                    out[n + i] = uniformAs(f, 0.0, range2, g.nextDouble());
                return out;
            };
            std::vector<double> baked(b.lfoRateHz);
            baked.insert(baked.end(), b.lfoPhase.begin(), b.lfoPhase.end());
            const UniformForm f = verifyAgainstNumpy(baked, "lfo_rate_hz/lfo_phase", lfo);
            rep.push_back(std::string("numpy rng ok lfo_rate_hz/lfo_phase regenerated bit-exactly (uniform as ") +
                          formName(f) + ")");
        }
        // render() line 1035: per strike, uniform(0, 2pi, n). Check the head of the stream.
        {
            const auto& u = rng.at("render_phase_uniform");
            b.phaseLow = u.at("low").number();
            b.phaseHigh = u.at("high").number();
            if (static_cast<size_t>(u.at("per_strike").number()) != n)
                throw BakeError("render_phase_uniform.per_strike != n_modes");
            Pcg64 g = b.renderStream;
            const double range = b.phaseHigh - b.phaseLow;
            const auto& head = rng.at("render_phase_head").array();
            for (size_t i = 0; i < head.size(); ++i)
            {
                const double mine = g.uniform(b.phaseLow, range);
                if (mine != head[i].number())
                    throw BakeError("render phase " + std::to_string(i) + ": " + fmt("%.17g", mine) + " vs numpy " +
                                    fmt("%.17g", head[i].number()));
            }
            rep.push_back("numpy rng ok render phases: first " + std::to_string(head.size()) +
                          " uniform(0, 2pi) values match numpy");
        }
    }
    catch (const json::ParseError& e)
    {
        throw BakeError(path + ": " + e.what());
    }
    catch (const std::out_of_range& e)
    {
        throw BakeError(path + ": malformed bake (" + e.what() + ")");
    }

    // ---- plugin tuning (docs/TUNING.md): the bake's own values, then its tuning block
    {
        if (b.slots.size() > size_t(kMaxArticulations))
            throw BakeError(path + ": more slots than the tuning model holds");
        Tuning& u = b.untuned;
        const EngineParams& ep = b.params;
        CymbalTuning& c = u.cymbal;
        c.tauRefMs = ep.tauRefMs;
        c.wanderDepth = ep.wanderDepth;
        c.wanderRateHz = ep.wanderRateHz;
        c.edgeWanderDepth = ep.edgeWanderDepth;
        c.edgeWanderRate = ep.edgeWanderRate;
        c.mfDepth = ep.mfDepth;
        c.mfTau = ep.mfTau;
        c.mfMin = ep.mfMin;
        c.mfMax = ep.mfMax;
        c.mfWBow = ep.mfWBow;
        c.mfWEdge = ep.mfWEdge;
        c.mfWBell = ep.mfWBell;
        c.mfLoHz = ep.mfLoHz;
        c.mfHiHz = ep.mfHiHz;
        c.lfDepth = ep.lfDepth;
        c.lfTau = ep.lfTau;
        c.lfMin = ep.lfMin;
        c.lfMax = ep.lfMax;
        c.lfCutoffHz = ep.lfCutoffHz;
        c.lfWBow = ep.lfWBow;
        c.lfWEdge = ep.lfWEdge;
        c.lfWBell = ep.lfWBell;
        c.lfWanderRateBoost = ep.lfWanderRateBoost;
        c.edgeEnvWindowS = b.edgeNpz.onsetS;
        c.edgeEnvelope = b.edgeNpz.present && b.edgeNpz.envelopeActive;
        u.numArticulations = static_cast<int>(b.slots.size());
        for (size_t s = 0; s < b.slots.size(); ++s)
        {
            u.articulations[s].levelDb = 20.0 * std::log10(b.slots[s].levelTrim);
            u.articulations[s].bandDb = b.slots[s].articulation.bandDb;
            u.articulations[s].delayMs = b.slots[s].articulation.delayMs;
            u.articulations[s].speed = b.slots[s].articulation.speed;
            u.articulations[s].attackDb = b.slots[s].articulation.attackDb;
            u.articulations[s].delayMsSoft = b.slots[s].articulation.delayMsSoft;
            u.articulations[s].speedSoft = b.slots[s].articulation.speedSoft;
            u.articulations[s].attackDbSoft = b.slots[s].articulation.attackDbSoft;
            u.articulations[s].attackMs = b.slots[s].articulation.attackMs;
            u.articulations[s].stickGain = b.slots[s].articulation.stickGain;
            u.articulations[s].stickLoHz = b.slots[s].articulation.stickLoHz;
            u.articulations[s].stickHiHz = b.slots[s].articulation.stickHiHz;
            u.articulations[s].stickLenMs = b.slots[s].articulation.stickLenMs;
            u.articulations[s].stickDecayMs = b.slots[s].articulation.stickDecayMs;
        }
        b.pythonT60S = b.t60S;
        b.pythonLfoRateHz = b.lfoRateHz;
        b.tuning = u;
        try
        {
            if (const json::Value* block = root.find("tuning"); block != nullptr && ! block->isNull())
            {
                applyTuning(b, parseTuningBlock(*block, b, u));
                b.hasTuningBlock = true;
                rep.push_back("tuning       the bake's tuning block is applied");
            }
        }
        catch (const std::exception& e)
        {
            throw BakeError(path + ": " + e.what());
        }
        for (const auto& s : b.slots)
            if (! s.hasBowVp && ! (b.params.tauRefMs > 0.0))
                throw BakeError(path + ": slot " + s.name + ": hertz_vel_scale needs tau_ref_ms > 0");
    }

    return bake;
}
} // namespace

std::shared_ptr<const BakeData> loadBake(const std::string& path)
{
    json::Value root;
    try
    {
        root = json::parseFile(path);
    }
    catch (const json::ParseError& e)
    {
        throw BakeError(e.what());
    }
    return buildBake(root, path);
}

std::shared_ptr<const BakeData> loadBakeFromText(const std::string& text, const std::string& name)
{
    json::Value root;
    try
    {
        root = json::parse(text);
    }
    catch (const json::ParseError& e)
    {
        throw BakeError(name + ": " + e.what());
    }
    return buildBake(root, name);
}

std::shared_ptr<const BakeData> loadBakeFromJson(const json::Value& root, const std::string& name)
{
    return buildBake(root, name);
}

} // namespace cymbalis

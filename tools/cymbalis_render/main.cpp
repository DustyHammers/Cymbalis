// cymbalis_render — render a test sequence with the C++ engine to a float WAV.
//
//   cymbalis_render --bake BAKE.json --sequence SEQ.json [--block-size 512]
//                   [--out OUT.f32.wav] [--out64 OUT.f64.wav] [--info INFO.json] [--quiet]
//                   [--python-contact-truncation] [--bench]
//
// --python-contact-truncation selects the Python-matching contact path: a new strike cuts
// short a contact that is still running. Without it, the live-engine behaviour applies and
// every strike plays in full. The two only differ for strikes less than one contact length
// apart (EngineConfig).
//
// --bench renders without writing any file and reports the time spent in Engine::process
// alone ("render_seconds") and the real-time factor render_seconds / audio seconds.
//
// The sequence's strikes go to the engine block by block, each at its sample
// offset within its block, the way a host delivers MIDI. Without --out, the
// file goes to renders/<name>_cpp_bs<N>_<UTC timestamp>.f32.wav.
#include "Bake.h"
#include "Engine.h"
#include "Json.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

void putU16(std::ofstream& f, uint16_t v)
{
    const unsigned char b[2] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>(v >> 8)};
    f.write(reinterpret_cast<const char*>(b), 2);
}

void putU32(std::ofstream& f, uint32_t v)
{
    const unsigned char b[4] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>((v >> 8) & 0xff),
                                static_cast<unsigned char>((v >> 16) & 0xff), static_cast<unsigned char>(v >> 24)};
    f.write(reinterpret_cast<const char*>(b), 4);
}

template <typename T>
void putLE(std::ofstream& f, T v)
{
    unsigned char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (size_t i = 0; i < sizeof(T) / 2; ++i) std::swap(b[i], b[sizeof(T) - 1 - i]);
#endif
    f.write(reinterpret_cast<const char*>(b), sizeof(T));
}

// IEEE-float WAV (format tag 3), 32- or 64-bit, interleaved stereo.
void writeFloatWav(const std::string& path, const std::vector<double>& L, const std::vector<double>& R, int sr,
                   int bits)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    const uint32_t frames = static_cast<uint32_t>(L.size());
    const uint16_t channels = 2;
    const uint16_t bytesPerSample = static_cast<uint16_t>(bits / 8);
    const uint32_t dataBytes = frames * channels * bytesPerSample;
    f.write("RIFF", 4);
    putU32(f, 4 + (8 + 18) + (8 + 4) + (8 + dataBytes));
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    putU32(f, 18);
    putU16(f, 3); // WAVE_FORMAT_IEEE_FLOAT
    putU16(f, channels);
    putU32(f, static_cast<uint32_t>(sr));
    putU32(f, static_cast<uint32_t>(sr) * channels * bytesPerSample);
    putU16(f, static_cast<uint16_t>(channels * bytesPerSample));
    putU16(f, static_cast<uint16_t>(bits));
    putU16(f, 0); // cbSize
    f.write("fact", 4);
    putU32(f, 4);
    putU32(f, frames);
    f.write("data", 4);
    putU32(f, dataBytes);
    for (size_t i = 0; i < L.size(); ++i)
    {
        if (bits == 32)
        {
            putLE(f, static_cast<float>(L[i]));
            putLE(f, static_cast<float>(R[i]));
        }
        else
        {
            putLE(f, L[i]);
            putLE(f, R[i]);
        }
    }
    if (!f) throw std::runtime_error("write failed: " + path);
}

std::string utcStamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d%02d%02dT%02d%02d%02d%06lldZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long long>(us));
    return buf;
}

std::string jsonEscape(const std::string& s)
{
    std::string o;
    for (char c : s)
    {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

int usage()
{
    std::fprintf(stderr,
                 "usage: cymbalis_render --bake BAKE.json --sequence SEQ.json [--block-size N]\n"
                 "                       [--out OUT.f32.wav] [--out64 OUT.f64.wav] [--info INFO.json] [--quiet]\n"
                 "                       [--python-contact-truncation] [--bench]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    std::string bakePath, seqPath, out32, out64, infoPath;
    int blockSize = 512;
    bool quiet = false;
    bool bench = false;
    cymbalis::EngineConfig config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
            return argv[++i];
        };
        try
        {
            if (a == "--bake") bakePath = next();
            else if (a == "--sequence") seqPath = next();
            else if (a == "--block-size") blockSize = std::stoi(next());
            else if (a == "--out") out32 = next();
            else if (a == "--out64") out64 = next();
            else if (a == "--info") infoPath = next();
            else if (a == "--quiet") quiet = true;
            else if (a == "--python-contact-truncation") config.truncateContactsLikePython = true;
            else if (a == "--bench") bench = true;
            else return usage();
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "error: %s\n", e.what());
            return usage();
        }
    }
    if (bakePath.empty() || seqPath.empty() || blockSize <= 0) return usage();

    try
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto bake = cymbalis::loadBake(bakePath);
        if (!quiet)
        {
            std::fprintf(stderr, "bake %s: %d modes, %zu slots, %.0f Hz\n", bakePath.c_str(), bake->numModes,
                         bake->slots.size(), bake->sampleRate);
            for (const auto& line : bake->report)
                std::fprintf(stderr, "  %s\n", line.c_str());
        }

        const auto seq = cymbalis::json::parseFile(seqPath);
        if (seq.at("format_version").number() != 1) throw std::runtime_error("unsupported sequence format_version");
        const std::string name = seq.at("name").string();
        const std::string slotName = seq.at("slot").string();
        if (!cymbalis::json::equal(seq.at("overrides"), bake->overrides))
            throw std::runtime_error("sequence overrides " + cymbalis::json::toString(seq.at("overrides")) +
                                     " differ from the bake's " + cymbalis::json::toString(bake->overrides));

        cymbalis::Engine engine(bake, config);
        const int slot = engine.slotIndex(slotName);
        if (slot < 0) throw std::runtime_error("slot '" + slotName + "' is not in the bake");

        const double sr = bake->sampleRate;
        const int64_t total = static_cast<int64_t>(seq.at("duration_s").number() * sr); // Python int(total_dur*SR)
        struct Hit { int64_t sample; double velocity; };
        std::vector<Hit> hits;
        for (const auto& s : seq.at("strikes").array())
        {
            const double t = s.array().at(0).number();
            const double x = t * sr;
            const auto n = static_cast<int64_t>(std::llround(x));
            if (std::fabs(x - static_cast<double>(n)) > 1e-6)
                throw std::runtime_error("strike time " + std::to_string(t) + " s is not on a whole sample");
            hits.push_back({n, s.array().at(1).number()});
        }

        std::vector<double> L(static_cast<size_t>(total)), R(static_cast<size_t>(total));
        size_t h = 0;
        double renderSeconds = 0.0; // time inside Engine::process only
        for (int64_t start = 0; start < total; start += blockSize)
        {
            const int n = static_cast<int>(std::min<int64_t>(blockSize, total - start));
            for (; h < hits.size() && hits[h].sample < start + n; ++h)
                if (hits[h].sample >= start)
                    engine.strike(slot, hits[h].velocity, hits[h].sample - start);
            const auto p0 = std::chrono::steady_clock::now();
            engine.process(L.data() + start, R.data() + start, n);
            renderSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - p0).count();
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double audioSeconds = static_cast<double>(total) / sr;

        if (!bench)
        {
            if (out32.empty())
                out32 = "renders/" + name + "_cpp_bs" + std::to_string(blockSize) + "_" + utcStamp() + ".f32.wav";
            writeFloatWav(out32, L, R, static_cast<int>(sr), 32);
            if (!out64.empty()) writeFloatWav(out64, L, R, static_cast<int>(sr), 64);
        }

        double peak = 0.0;
        for (size_t i = 0; i < L.size(); ++i)
            peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));

        char nums[768];
        std::snprintf(nums, sizeof nums,
                      "\"block_size\": %d, \"samples\": %lld, \"sample_rate\": %d, \"strikes\": %llu, "
                      "\"peak\": %.17g, \"seconds\": %.2f, \"audio_seconds\": %.6f, \"render_seconds\": %.4f, "
                      "\"realtime_factor\": %.4f",
                      blockSize, static_cast<long long>(total), static_cast<int>(sr),
                      static_cast<unsigned long long>(engine.strikesApplied()), peak, seconds, audioSeconds,
                      renderSeconds, audioSeconds > 0.0 ? renderSeconds / audioSeconds : 0.0);
        const std::string outJson = bench ? std::string("null") : "\"" + jsonEscape(out32) + "\"";
        const std::string out64Json = (bench || out64.empty()) ? std::string("null") : "\"" + jsonEscape(out64) + "\"";
        const std::string info = "{\"sequence\": \"" + jsonEscape(name) + "\", \"slot\": \"" + jsonEscape(slotName) +
                                 "\", \"engine\": \"cpp\", " + nums + ", \"out\": " + outJson +
                                 ", \"out64\": " + out64Json + "}";
        if (!infoPath.empty())
        {
            std::ofstream f(infoPath);
            f << info << "\n";
        }
        std::printf("%s\n", info.c_str());
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "cymbalis_render: %s\n", e.what());
        return 1;
    }
}

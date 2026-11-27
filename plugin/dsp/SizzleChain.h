#pragma once

// The sizzle chain (docs/PORT_MAP.md, "Sizzle chain"; approved by Kris on 2026-11-26): a bead
// chain resting on a ride. It lives inside each cymbal's own FastEngine (no shared state: one
// parallel job in the kit) and has its own seeded random generator, so nothing else's draws move.
//
//  - The drive: per strike, the plate's displacement (its modes under 1 kHz, each amplitude
//    weighted 300 Hz / f, root sum of squares, from their amplitudes and decay rates), normalised
//    so that 0.5 s after the strike it is W(position) x (v / 0.9) ^ VELOCITY EFFECT; the total
//    D = sqrt(sum of the strikes' squares) and the gate g = clamp((20 log10 D - STOPS AT) / 6, 0, 1),
//    every kBlock samples.
//  - The swing: u(t), the plate's displacement waveform from its kSwingModes largest low modes
//    (weighted 300 Hz / f), over its own RMS and clipped to +-3; r(t), white noise through a
//    2nd-order 25 Hz low-pass, mean 1 and standard deviation 0.7, clipped at 0; the envelope
//    env = D g max(0, 1 + SWING u r) / sqrt(E[max(0, 1 + SWING u r)^2]): the swing factor of mean
//    square 1, so that the bands' power at D g = 1 is their calibration whatever SWING is (the
//    drawing's envelope, envelope(), takes the factor as it is).
//  - The sizzle: one white noise x env, the same for both channels, into a resonator on each of
//    the preset's modes from 3.5 to 16 kHz (t60 the mode's chain-damped t60, at most 0.6 s), each
//    with a fixed Rayleigh weight and the engine's own left and right gains for the mode, scaled
//    so that at env = 1 each one-third-octave band's power (mono mean of L and R, before the
//    output gain, which the sizzle passes through as the cymbal does) is C_b x ref_ms (the bake's,
//    tools/cymbalis_trims --chain-ref) x SIZZLE LEVEL.
//  - The damping: with the chain on, a strike's modes take t60 / (1 + DAMPING (f / 4 kHz) ^ 1.5)
//    (FastEngine applies dampingFactor() to each new strike's decay; ringing strikes keep theirs).
//  - Switching: the sizzle fades in or out over 50 ms; the drive is tracked on or off.
//
// Allocates only in build(); everything else is real-time safe.

#include "ChainSettings.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cymbalis
{

struct BakeData;

class SizzleChain
{
public:
    static constexpr int kBlock = 32;      // samples per drive and gate update
    static constexpr int kMaxStrikes = 32; // strikes whose drive is tracked (then the weakest is replaced)
    static constexpr int kSwingModes = 16; // the swing waveform's modes
    static constexpr double kFadeS = 0.05;
    static constexpr double kMaxT60S = 0.6;

    // From the bake: the modes that drive the chain (under 1 kHz), those that ring (3.5 to 16 kHz),
    // the swing's modes and the seeded weights. `refMs`: the bake's ref_ms (0: not measured, no
    // sizzle). Allocates.
    void build (const BakeData& b, std::uint64_t seed, double refMs);
    bool built() const noexcept { return built_; }

    // The sample rate, each mode's decay rate (-ln 1000 / T60: the engine's) and its left and
    // right gains.
    void prepare (double sampleRate, const double* negLambda, const double* gainL, const double* gainR) noexcept;
    // New settings: the level, colour, gate, swing and damping from the next block; the drive
    // weights on the ringing strikes too.
    void setSettings (const ChainSettings& s) noexcept;
    const ChainSettings& settings() const noexcept { return settings_; }
    void setDecay (const double* negLambda) noexcept;           // the ring lengths changed
    void setGains (const double* gainL, const double* gainR) noexcept; // the stereo gains changed

    // On: the sizzle fades in over 50 ms (the drive is always tracked) and new strikes are damped;
    // off: it fades out.
    void setOn (bool on) noexcept { on_ = on; }
    bool on() const noexcept { return on_; }
    // Each mode's t60 divisor with the chain on, 1 + DAMPING (f / 4 kHz) ^ 1.5 (numModes values).
    const double* dampingFactor() const noexcept { return damp_.data(); }

    // A strike: its position (0 edge, 0.5 bow, 1 bell) and velocity, its coupling (re, im) of
    // every mode and each mode's amplitude decay rate (1/s, > 0).
    void strike (double position, double velocity, const float* re, const float* im, const double* rate) noexcept;

    // Adds the sizzle into left and right (while on or fading), and moves the drive on.
    void process (float* left, float* right, int n) noexcept;

    // The envelope env (1 = a full hit's drive 0.5 s after it, gate open, no swing), its mean
    // over the last process(); the drive D now and the gate g now.
    float envelope() const noexcept { return lastEnv_; }
    double drive() const noexcept { return driveNow_; }
    double gate() const noexcept { return gateNow_; }
    // Nothing left to add: off and faded out, or the gate shut and the resonators rung out.
    bool isSilent() const noexcept;

    // For checks: the resonators ringing (3.5 to 16 kHz) and the swing's modes.
    int numResonators() const noexcept { return numRes_; }
    const std::vector<int>& swingModes() const noexcept { return swingIdx_; }

private:
    bool built_ = false;
    int n_ = 0; // the bake's modes
    double sr_ = 0.0;
    double refMs_ = 0.0;
    ChainSettings settings_;
    double swingNorm_ = 1.0, swingNormFor_ = 0.0; // 1 / the swing factor's RMS, for SWING swingNormFor_
    bool on_ = false;
    double fade_ = 0.0, fadeStep_ = 0.0;
    std::vector<double> freq_, damp_, excNorm_;
    // The drive: the low modes (index, 300 Hz / f), and per tracked strike each low mode's
    // (weighted amplitude)^2 / its displacement^2 at 0.5 s, decaying, its decay over one block,
    // and the strike's position and velocity (for its weight, recomputed when the settings change).
    std::vector<int> loIdx_;
    std::vector<float> loW_;
    int nLo_ = 0, nLoPad_ = 0;
    std::vector<float> recC_, recM_; // kMaxStrikes x nLoPad_
    std::array<double, kMaxStrikes> recSum_ {}, recPos_ {}, recVel_ {}, recWeight_ {};
    std::array<bool, kMaxStrikes> recUsed_ {};
    std::vector<double> rateScratch_;
    double driveNow_ = 0.0, gateNow_ = 0.0;
    // The swing: its modes, their complex amplitudes now (weighted, with the strikes' phases and
    // drive), their rotation per sample and their decay per sample (the last strike's).
    std::vector<int> swingIdx_;
    std::array<double, kSwingModes> swRe_ {}, swIm_ {}, swRotRe_ {}, swRotIm_ {}, swDec_ {};
    int nSwing_ = 0;
    // r(t): its low-pass state and coefficients, and its scale to standard deviation 0.7.
    double lpB0_ = 0.0, lpB1_ = 0.0, lpB2_ = 0.0, lpA1_ = 0.0, lpA2_ = 0.0, lpZ1_ = 0.0, lpZ2_ = 0.0, lpScale_ = 0.0;
    // The resonators: their modes, a fixed Rayleigh weight each, their coefficients and state
    // (padded to 4), and their output gains to left and right.
    std::vector<int> resIdx_;
    std::vector<double> resWeight_;
    int numRes_ = 0, numResPad_ = 0;
    std::vector<float> a1_, a2_, y1_, y2_, cl_, cr_;
    std::vector<double> gainL_, gainR_, negLambda_;
    bool resonating_ = false; // the resonators' state is not all zero
    int quietBlocks_ = 0;     // blocks since the input was last above nothing
    // Its random generator (xorshift128+, seeded per preset).
    std::uint64_t s0_ = 0, s1_ = 0;
    float lastEnv_ = 0.0f;
    int blockLeft_ = 0; // samples left in the current block
    double env0_ = 0.0, env1_ = 0.0; // D g at the current block's ends

    double nextUniform() noexcept; // -sqrt(3) .. sqrt(3): unit variance
    void computeResonators() noexcept;
    void computeWeights() noexcept;
    double strikeWeight (double position, double velocity) const noexcept;
    void advanceBlock() noexcept;
};

} // namespace cymbalis

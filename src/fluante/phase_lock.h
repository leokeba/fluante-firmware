#pragma once

// The strobe locked to NowTP's network time (docs/DESIGN.md section 5.4):
// its periods begin at whole periods of network time, so Fluantes at the
// same frequency flash together. Once a second it measures where the
// strobe's period began, in network time, and steers the timer's divider (a
// trim, the counter running on) with a PI loop slow enough that NowTP's
// single bad rounds are filtered rather than followed. The trim is
// fractional, dithered over the device's ticks between whole divider steps:
// at 1 kHz a step is 200 ppm. The first
// lock, and a lock lost for good (the network time jumped: a new reference),
// restart the period once at the right instant instead: aligned at once,
// steered from there. Platform-free: the caller samples the timer and the
// clock, and applies the action.

#include <stdint.h>

namespace fluante {

class PhaseLock {
public:
    enum class State : uint8_t {
        Off,        // not asked: the timer runs untrimmed
        Waiting,    // no network time yet, or the strobe not begun: the trim held
        Acquiring,  // steering toward the network's periods
        Locked,     // within kLockedUs (or a count) for kLockedSamples samples in a row
    };

    static constexpr uint32_t kIntervalMs = 1000;
    // The loop's time constant, and its integral's: critically damped. A
    // crystal's offset (±20 ppm, about 2 steps at 60 Hz) is learned by the
    // integral.
    static constexpr double kTauS = 10;
    static constexpr double kIntegralTauS = 4 * kTauS;
    // A first lock further off than this restarts the period.
    static constexpr double kJumpUs = 200;
    // Locked, a sample further off than this (or an eighth of the period) is
    // set aside; kSlips in a row and the lock starts over.
    static constexpr double kSlipUs = 1000;
    static constexpr int kSlips = 3;
    static constexpr double kLockedUs = 100;
    static constexpr int kLockedSamples = 5;
    // The trim's range, in divider steps: about ±0.08 % at 60 Hz, far more
    // than the crystals need.
    static constexpr int32_t kMaxTrim = 64;
    // A restart is scheduled at the first period boundary this far ahead.
    static constexpr int64_t kLeadUs = 30000;

    // The timer as planned (the same on every Fluante asked the same
    // frequency) and as it runs.
    struct Timer {
        double periodUs = 0;   // planned, untrimmed: the target in network time
        uint32_t divider = 0;  // planned, in 1/256 steps
        double stepUs = 0;     // one count of the counter, trimmed, local µs
    };
    // The counter and the local clock read together, and network time minus
    // local time then.
    struct Sample {
        uint32_t count = 0;
        int64_t localUs = 0;
        int64_t offsetUs = 0;
    };
    // A restart of the timer's period at `restartAtUs` (local), when
    // `restart`.
    struct Action {
        bool restart = false;
        int64_t restartAtUs = 0;
    };

    bool enabled() const { return enabled_; }
    // Off holds no trim: step() gives 0.
    void setEnabled(bool on);

    // Every tick: true when a sample is due.
    bool due(uint32_t nowMs);
    // Every tick: the divider's steps from the plan for the next tick, whole,
    // averaging trim().
    int32_t step();
    // Instead of update(), when there's no network time or the strobe isn't
    // begun: the trim held, the next sample a first lock.
    void wait();
    Action update(const Timer& t, const Sample& s);

    State state() const { return state_; }
    // The last sample's: when the strobe's period began, after the network's
    // (positive: late), µs.
    double errorUs() const { return errorUs_; }
    // In divider steps (1/divider of the period each).
    double trim() const { return trim_; }

private:
    Action restartAt(const Timer& t, const Sample& s);

    bool enabled_ = true;
    State state_ = State::Waiting;
    bool started_ = false;
    uint32_t lastMs_ = 0;
    bool acquired_ = false;  // a first sample taken (and a restart, if needed)
    uint32_t divider_ = 0;
    double integral_ = 0;    // divider steps
    double trim_ = 0;
    double dither_ = 0;      // what the whole steps given owe trim_
    double errorUs_ = 0;
    int slips_ = 0;
    int good_ = 0;
    int64_t restartAtUs_ = 0;  // samples before it say nothing yet
};

}  // namespace fluante

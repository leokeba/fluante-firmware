#pragma once

// NowTP's network time, filtered, as the layers' time base (docs/DESIGN.md
// section 5.5): base = offset + rate · local, steered once a second toward
// the network's time by a PI loop slow enough that NowTP's single bad rounds
// are filtered rather than followed. Only the rate is steered, so the base
// never jumps but at the first lock, and when the lock is lost for good (the
// network time jumped: a new reference). Platform-free: the caller reads the
// clocks.

#include <stdint.h>

#include "tesserkit/pulses.h"

namespace fluante {

class NetworkClock {
public:
    enum class State : uint8_t {
        Off,        // not asked: the base is the local clock
        Waiting,    // no network time yet: the base runs on at its last rate
        Acquiring,  // steering toward the network's time
        Locked,     // within kLockedUs for kLockedSamples samples in a row
    };

    static constexpr uint32_t kIntervalMs = 1000;
    // The loop's time constant, and its integral's: critically damped, as
    // the strobe's (section 5.4). The integral learns the crystal's offset.
    static constexpr double kTauS = 10;
    static constexpr double kIntegralTauS = 4 * kTauS;
    // A sample further off than this is set aside; kSlips in a row and the
    // base jumps to the network's time.
    static constexpr double kSlipUs = 1000;
    static constexpr int kSlips = 3;
    static constexpr double kLockedUs = 100;
    static constexpr int kLockedSamples = 5;
    // The rate's range against the local clock, far more than crystals need
    // (±20 ppm).
    static constexpr double kMaxPpm = 200;

    bool enabled() const { return enabled_; }
    // Off makes the base the local clock: the layers run free.
    void setEnabled(bool on);

    // Every tick: true when a sample is due.
    bool due(uint32_t nowMs);
    // Instead of update(), when there's no network time: the base held, the
    // next sample a first lock.
    void wait();
    // A sample: the local clock (µs) and network time minus local time then.
    void update(int64_t localUs, int64_t offsetUs);

    const tesserkit::pulses::Timebase& timebase() const { return base_; }
    State state() const { return state_; }
    // The last sample's: the base behind the network's time (positive: the
    // layers late), µs.
    double errorUs() const { return errorUs_; }
    // The base's rate against the local clock, in ppm.
    double ppm() const { return (base_.rate - 1) * 1e6; }

private:
    // The base set to `baseUs` at local time `localUs`, running at `rate`
    // from there.
    void set(int64_t localUs, double baseUs, double rate);

    bool enabled_ = true;
    State state_ = State::Waiting;
    bool started_ = false;
    uint32_t lastMs_ = 0;
    bool acquired_ = false;
    double integral_ = 0;  // rate - 1
    double errorUs_ = 0;
    int slips_ = 0;
    int good_ = 0;
    tesserkit::pulses::Timebase base_;
};

}  // namespace fluante

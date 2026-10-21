// NowTP's network time, filtered, as the layers' time base (docs/DESIGN.md
// section 5.5).
#include "fluante/network_clock.h"

#include <math.h>

namespace fluante {

namespace {

double clamp(double v, double lim) { return v < -lim ? -lim : v > lim ? lim : v; }

}  // namespace

void NetworkClock::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    acquired_ = false;
    integral_ = 0;
    errorUs_ = 0;
    good_ = slips_ = 0;
    base_ = tesserkit::pulses::Timebase();
    state_ = on ? State::Waiting : State::Off;
}

bool NetworkClock::due(uint32_t nowMs) {
    if (started_ && nowMs - lastMs_ < kIntervalMs) return false;
    started_ = true;
    lastMs_ = nowMs;
    return true;
}

void NetworkClock::wait() {
    if (!enabled_) return;
    acquired_ = false;
    good_ = slips_ = 0;
    state_ = State::Waiting;
}

void NetworkClock::update(int64_t localUs, int64_t offsetUs) {
    if (!enabled_) {
        state_ = State::Off;
        return;
    }
    double local = static_cast<double>(localUs);
    double network = local + static_cast<double>(offsetUs);
    if (!acquired_) {
        // The first lock: the base jumps to the network's time, at the rate
        // the integral learned before.
        acquired_ = true;
        good_ = slips_ = 0;
        errorUs_ = 0;
        state_ = State::Acquiring;
        set(localUs, network, 1 + integral_);
        return;
    }
    double base = base_.offsetUs + base_.rate * local;
    double e = network - base;
    errorUs_ = e;
    if (fabs(e) > kSlipUs) {
        // A bad round, or the network time jumped: set aside, then start over.
        good_ = 0;
        state_ = State::Acquiring;
        if (++slips_ < kSlips) return;
        slips_ = 0;
        set(localUs, network, 1 + integral_);
        return;
    }
    slips_ = 0;

    // The error grows by the rate's shortfall: 1 µs per second per ppm.
    double dt = kIntervalMs / 1000.0;
    double maxRate = kMaxPpm * 1e-6;
    integral_ = clamp(integral_ + e * 1e-6 * dt / (kTauS * kIntegralTauS), maxRate);
    double rate = 1 + clamp(integral_ + e * 1e-6 / kTauS, maxRate);
    // Only the rate changes: the base goes on from where it is.
    set(localUs, base, rate);

    good_ = fabs(e) < kLockedUs ? good_ + 1 : 0;
    state_ = good_ >= kLockedSamples ? State::Locked : State::Acquiring;
}

void NetworkClock::set(int64_t localUs, double baseUs, double rate) {
    base_.rate = rate;
    base_.offsetUs = baseUs - rate * static_cast<double>(localUs);
}

}  // namespace fluante

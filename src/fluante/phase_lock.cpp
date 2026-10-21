// The strobe locked to network time (docs/DESIGN.md section 5.4).
#include "fluante/phase_lock.h"

#include <math.h>

namespace fluante {

namespace {

double clamp(double v, double lim) { return v < -lim ? -lim : v > lim ? lim : v; }

}  // namespace

void PhaseLock::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    acquired_ = false;
    integral_ = 0;
    trim_ = dither_ = 0;
    good_ = slips_ = 0;
    state_ = on ? State::Waiting : State::Off;
}

bool PhaseLock::due(uint32_t nowMs) {
    if (started_ && nowMs - lastMs_ < kIntervalMs) return false;
    started_ = true;
    lastMs_ = nowMs;
    return true;
}

int32_t PhaseLock::step() {
    if (!enabled_) return 0;
    // First-order sigma-delta: the error carried to the next tick.
    double want = trim_ + dither_;
    double whole = floor(want + 0.5);
    dither_ = want - whole;
    return static_cast<int32_t>(whole);
}

void PhaseLock::wait() {
    if (!enabled_) return;
    acquired_ = false;
    good_ = slips_ = 0;
    state_ = State::Waiting;
}

PhaseLock::Action PhaseLock::update(const Timer& t, const Sample& s) {
    if (!enabled_) {
        state_ = State::Off;
        return {};
    }
    if (!(t.periodUs > 0) || !t.divider) {
        wait();
        return {};
    }
    // A new frequency or resolution: a new plan, the crystal's offset in
    // steps of another size.
    if (t.divider != divider_) {
        divider_ = t.divider;
        acquired_ = false;
        integral_ = 0;
        trim_ = dither_ = 0;
        good_ = slips_ = 0;
    }
    if (acquired_ && s.localUs < restartAtUs_) return {};

    // When this period began, in network time, and how far from a whole
    // number of the network's periods. The counter is somewhere in its count:
    // its middle, unbiased (122 µs at 1 Hz).
    double begun = static_cast<double>(s.localUs) + static_cast<double>(s.offsetUs) -
                   (static_cast<double>(s.count) + 0.5) * t.stepUs;
    double e = remainder(begun, t.periodUs);
    errorUs_ = e;

    if (!acquired_) {
        acquired_ = true;
        good_ = slips_ = 0;
        state_ = State::Acquiring;
        if (fabs(e) > kJumpUs) return restartAt(t, s);
    }
    double slip = kSlipUs < t.periodUs / 8 ? kSlipUs : t.periodUs / 8;
    if (fabs(e) > slip) {
        // A bad round, or the network time jumped: set aside, then start over.
        good_ = 0;
        state_ = State::Acquiring;
        if (++slips_ < kSlips) return {};
        slips_ = 0;
        return restartAt(t, s);
    }
    slips_ = 0;

    // A divider step moves the phase by 1/divider of a period each period:
    // 1e6/divider µs each second.
    double perStep = 1e6 / static_cast<double>(t.divider);
    double dt = kIntervalMs / 1000.0;
    integral_ = clamp(integral_ + e * dt / (perStep * kTauS * kIntegralTauS), kMaxTrim);
    double u = -(e / (perStep * kTauS) + integral_);
    trim_ = clamp(u, kMaxTrim);

    // A sample can't tell better than a count (244 µs at 1 Hz).
    double locked = kLockedUs > t.stepUs ? kLockedUs : t.stepUs;
    good_ = fabs(e) < locked ? good_ + 1 : 0;
    state_ = good_ >= kLockedSamples ? State::Locked : State::Acquiring;
    return {};
}

PhaseLock::Action PhaseLock::restartAt(const Timer& t, const Sample& s) {
    // The first boundary of the network's periods past the lead.
    double now = static_cast<double>(s.localUs) + static_cast<double>(s.offsetUs);
    double target = (floor((now + static_cast<double>(kLeadUs)) / t.periodUs) + 1) * t.periodUs;
    Action a;
    a.restart = true;
    a.restartAtUs = static_cast<int64_t>(llround(target)) - s.offsetUs;
    restartAtUs_ = a.restartAtUs;
    good_ = 0;
    state_ = State::Acquiring;
    return a;
}

}  // namespace fluante

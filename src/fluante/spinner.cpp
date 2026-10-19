#include "fluante/spinner.h"

#include <math.h>

namespace fluante {

using tessermotion::Guard;

void Spinner::run(bool on) {
    Guard g(lock());
    want_ = on;
    if (state_ == State::Halted) state_ = State::Stopped;
}

bool Spinner::running() {
    Guard g(lock());
    return want_;
}

Spinner::State Spinner::state() {
    Guard g(lock());
    return state_;
}

float Spinner::rpm() {
    Guard g(lock());
    float perRpm = microstepsPerSecond(1);
    return perRpm != 0 ? driver_.actualVelocity() / perRpm : 0;
}

float Spinner::microstepsPerSecond(float sculptureRpm) const {
    float ratio = settings.ratio > 0 ? settings.ratio : 1;
    float motorTurnsPerSecond = sculptureRpm / 60.0f / ratio;
    return motorTurnsPerSecond * static_cast<float>(settings.stepsPerTurn) *
           static_cast<float>(driver_.microsteps());
}

void Spinner::begin() { driver_.claim("spinner"); }

void Spinner::update(uint64_t nowUs) {
    // The driver's ramp follows the spinner's acceleration, in its units.
    driver_.ramp.acceleration = fabsf(microstepsPerSecond(settings.acceleration));
    if (!booted_) {
        booted_ = true;
        tessermotion::TmcFound found = driver_.status().found;
        if (found == tessermotion::TmcFound::Turning && driver_.enabled()) {
            // Adopted at the speed it turned at: on to the speed asked, or
            // down to a stop, on the ramp.
            state_ = want_ ? State::Running : State::Stopping;
        } else if (found == tessermotion::TmcFound::Coasting) {
            coastUntilUs_ = nowUs + static_cast<uint64_t>(settings.coastMs) * 1000;
        }
    }
    switch (state_) {
        case State::Halted:
            return;
        case State::Stopped:
        case State::Absent:
            if (!want_) {
                state_ = State::Stopped;
                return;
            }
            if (!driver_.present()) {
                state_ = State::Absent;
                return;
            }
            if (nowUs < coastUntilUs_) return;  // the sculpture still coasting
            driver_.overrideCurrent(settings.settleCurrent);
            driver_.enable(true);
            sinceUs_ = nowUs;
            state_ = State::Settling;
            return;
        case State::Settling:
            if (!driver_.enabled()) {
                halt();
                return;
            }
            if (!want_) {
                driver_.enable(false);
                driver_.overrideCurrent(0);
                state_ = State::Stopped;
                return;
            }
            if (nowUs - sinceUs_ >= static_cast<uint64_t>(settings.settleMs) * 1000) {
                driver_.overrideCurrent(0);
                state_ = State::Running;
                // Every tick, so the deadman never runs out while turning.
                driver_.setVelocity(microstepsPerSecond(settings.speed));
            }
            return;
        case State::Running:
        case State::Stopping:
            if (!driver_.enabled()) {
                halt();
                return;
            }
            if (want_) {
                state_ = State::Running;
                driver_.setVelocity(microstepsPerSecond(settings.speed));
                return;
            }
            state_ = State::Stopping;
            driver_.hold();  // ramps down
            if (driver_.actualVelocity() == 0) {
                driver_.enable(false);
                state_ = State::Stopped;
            }
            return;
    }
}

void Spinner::stop() {
    driver_.enable(false);
    halt();
}

void Spinner::halt() {
    driver_.overrideCurrent(0);
    state_ = State::Halted;
}

}  // namespace fluante

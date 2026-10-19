#include "fluante/spinner.h"

#include "tessermotion/tmc/tmc2209_map.h"

namespace fluante {

namespace r22 = tessermotion::tmc2209;
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
    return rpm_;
}

float Spinner::microstepsPerSecond(float sculptureRpm) const {
    float ratio = settings.ratio > 0 ? settings.ratio : 1;
    float motorTurnsPerSecond = sculptureRpm / 60.0f / ratio;
    return motorTurnsPerSecond * static_cast<float>(settings.stepsPerTurn) *
           static_cast<float>(driver_.settings.microsteps);
}

void Spinner::update(uint64_t nowUs) {
    switch (state_) {
        case State::Halted:
            return;
        case State::Stopped:
        case State::Absent: {
            rpm_ = 0;
            if (!want_) {
                state_ = State::Stopped;
                return;
            }
            if (!driver_.present()) {
                state_ = State::Absent;
                return;
            }
            // PWMCONF's other bits as the driver last read them: a register
            // copy still at 0 hasn't been read yet, and would clear them.
            uint32_t pwmconf = driver_.registerValue(r22::PWMCONF);
            if (settings.initPwm && pwmconf != 0) {
                pwmconf = r22::pwm_ofs.set(pwmconf, settings.pwmOfs);
                pwmconf = r22::pwm_grad.set(pwmconf, settings.pwmGrad);
                driver_.writeRegister(r22::PWMCONF, pwmconf);
            }
            driver_.enable(true);
            sinceUs_ = nowUs;
            state_ = State::Settling;
            return;
        }
        case State::Settling:
            if (!driver_.enabled()) {
                halt();
                return;
            }
            if (!want_) {
                driver_.enable(false);
                state_ = State::Stopped;
                return;
            }
            if (nowUs - sinceUs_ >= static_cast<uint64_t>(settings.settleMs) * 1000) {
                state_ = State::Running;
                lastUs_ = nowUs;
            }
            return;
        case State::Running:
        case State::Stopping: {
            if (!driver_.enabled()) {
                halt();
                return;
            }
            float target = want_ ? settings.speed : 0;
            state_ = want_ ? State::Running : State::Stopping;
            float dt = static_cast<float>(nowUs - lastUs_) * 1e-6f;
            lastUs_ = nowUs;
            float stepRpm = settings.acceleration * dt;
            if (!(settings.acceleration > 0) || (rpm_ < target ? target - rpm_ : rpm_ - target) <= stepRpm) {
                rpm_ = target;
            } else {
                rpm_ += rpm_ < target ? stepRpm : -stepRpm;
            }
            if (!want_ && rpm_ == 0) {
                driver_.enable(false);
                state_ = State::Stopped;
                return;
            }
            // Every tick, so the deadman never runs out while turning.
            driver_.setVelocity(microstepsPerSecond(rpm_));
            return;
        }
    }
}

void Spinner::stop() {
    driver_.enable(false);
    halt();
}

void Spinner::halt() {
    rpm_ = 0;
    state_ = State::Halted;
}

}  // namespace fluante

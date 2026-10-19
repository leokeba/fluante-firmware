#include "fluante/strobe.h"

namespace fluante {

bool Strobe::begin() {
    if (!pwm_.attach(pin_)) return false;
    begun_ = true;
    pwm_.timing(timing_);
    applyDuty();
    return true;
}

void Strobe::setOn(bool on) {
    on_ = on;
    applyDuty();
}

void Strobe::setDuty(float percent) {
    duty_ = percent;
    applyDuty();
}

void Strobe::setFrequency(double hz) {
    frequency_ = hz;
    retime();
}

double Strobe::pulseUs() const {
    if (timing_.frequency <= 0) return 0;
    return static_cast<double>(counts()) / static_cast<double>(1u << timing_.bits) / timing_.frequency * 1e6;
}

void Strobe::retime() {
    Timing t = plan(frequency_, kBits);
    if (t == timing_) return;
    timing_ = t;
    if (!begun_) return;
    pwm_.timing(timing_);
    applyDuty();  // the compare value scales with the resolution
}

void Strobe::applyDuty() {
    if (begun_) pwm_.duty(counts());
}

}  // namespace fluante

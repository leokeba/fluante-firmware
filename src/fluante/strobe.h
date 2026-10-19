#pragma once

// The strobe (docs/DESIGN.md section 5.1): an LED flashed by the LEDC at a
// frequency, for a share of each period. The tree's setters call these, under
// the API lock; nothing else touches it.

#include <stdint.h>

#include "fluante/pwm.h"
#include "fluante/timing.h"

namespace fluante {

class Strobe {
public:
    Strobe(Pwm& pwm, int pin) : pwm_(pwm), pin_(pin) {}

    // Factory defaults, until the stored settings say otherwise: the old
    // firmware's.
    static constexpr bool kOn = true;
    static constexpr double kFrequency = 60.01;
    static constexpr float kDuty = 3;
    // The resolution asked of the timer: 4 µs steps of the pulse at 60 Hz,
    // and frequency steps under 0.001 Hz. Under about 19 Hz the timer runs
    // from the 1 MHz clock instead of APB, at the same resolution.
    static constexpr uint8_t kBits = 12;
    // What the tree accepts.
    static constexpr double kLowestHz = 1;
    static constexpr double kHighestHz = 1000;

    // Attaches the pin and applies the settings. Before it, setters only store.
    bool begin();

    bool on() const { return on_; }
    float duty() const { return duty_; }             // percent
    double frequency() const { return frequency_; }  // Hz, as asked
    void setOn(bool on);
    void setDuty(float percent);
    void setFrequency(double hz);

    // What the timer runs.
    const Timing& timing() const { return timing_; }
    // The compare value applied: 0 while off.
    uint32_t counts() const { return on_ ? dutyCounts(duty_, timing_.bits) : 0; }
    // The flash's length as applied, in µs (0 while off).
    double pulseUs() const;
    int pin() const { return pin_; }

    void poll() { pwm_.poll(); }

private:
    void retime();
    void applyDuty();

    Pwm& pwm_;
    int pin_;
    bool begun_ = false;
    bool on_ = kOn;
    float duty_ = kDuty;
    double frequency_ = kFrequency;
    Timing timing_ = plan(kFrequency, kBits);
};

}  // namespace fluante

#pragma once

// The strobe (docs/DESIGN.md section 5.1): an LED flashed by a TesserKIT
// PwmOutput (the LEDC, a timer of its own) at a frequency, for a share of
// each period, with Fluante's defaults and resolution. The tree's setters
// call these, under the API lock; nothing else touches it.

#include <stdint.h>

#include <functional>

#include "fluante/phase_lock.h"
#include "tesserkit/pwm.h"

namespace fluante {

class Strobe {
public:
    Strobe(tesserkit::Pwm& pwm, int pin) : pwm_(pwm), out_(pwm, pin) {
        out_.setOn(kOn);
        out_.setFrequency(kFrequency);
        out_.setDuty(kDuty);
        out_.setResolution(kBits);
    }

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
    bool begin() { return out_.begin(); }
    // Gives the pin back, left low, its settings kept: the layers take it
    // (section 5.5). Its lock waits until begin() again.
    void end() { out_.end(); }
    bool begun() const { return out_.begun(); }

    bool on() const { return out_.on(); }
    float duty() const { return out_.duty(); }             // percent
    double frequency() const { return out_.frequency(); }  // Hz, as asked
    void setOn(bool on) { out_.setOn(on); }
    void setDuty(float percent) { out_.setDuty(percent); }
    void setFrequency(double hz) { out_.setFrequency(hz); }

    // What the timer runs (zeros before begin()).
    const tesserkit::pwm::Timing& timing() const { return out_.timing(); }
    // The same untrimmed: the same on every Fluante asked this frequency.
    const tesserkit::pwm::Timing& planned() const { return out_.planned(); }
    uint32_t counts() const { return out_.counts(); }
    // The flash's length as applied, in µs (0 while off).
    double pulseUs() const { return out_.pulseUs(); }
    int pin() const { return out_.pin(); }

    // Its lock to network time (section 5.4).
    PhaseLock& lock() { return lock_; }
    const PhaseLock& lock() const { return lock_; }
    // Network time minus local time (esp_timer's), false while there is none.
    using NetworkOffset = std::function<bool(int64_t& offsetUs)>;
    // Every tick, under the API lock: the lock's trim for the tick, and once
    // a second its sample of the timer against network time.
    void sync(uint32_t nowMs, const NetworkOffset& offset);

    // Applies a duty waiting for the end of its period; often, under the API
    // lock.
    void poll() { pwm_.poll(); }

private:
    tesserkit::Pwm& pwm_;
    tesserkit::PwmOutput out_;
    PhaseLock lock_;
};

}  // namespace fluante

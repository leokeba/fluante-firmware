#pragma once

// What the strobe needs from the chip (docs/DESIGN.md section 4.1): one LEDC
// timer and channel, whose clock, divider and resolution it sets itself.
// platform/esp/pwm_esp.cpp drives the classic ESP32's LEDC; the tests use a
// fake that records what it was given.

#include <stdint.h>

#include "fluante/timing.h"

namespace fluante {

class Pwm {
public:
    virtual ~Pwm() = default;
    // The output on `pin`, low until a duty comes.
    virtual bool attach(int pin) = 0;
    // The timer: clock, divider and resolution, applied together.
    virtual void timing(const Timing& t) = 0;
    // The compare value, 0 to 2^bits of the last timing. It takes effect at
    // the end of the current period; one written while an earlier one still
    // waits for its period is kept, and applied by poll().
    virtual void duty(uint32_t counts) = 0;
    // Applies a duty that was waiting. Often (every TesserKIT tick), under
    // the same lock as the other calls.
    virtual void poll() {}
};

#if defined(ESP_PLATFORM)
// The chip's LEDC: the high-speed group's timer 0 and channel 0.
Pwm& defaultPwm();
#endif

}  // namespace fluante

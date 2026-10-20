#pragma once

// The strobe's inrush limit (docs/DESIGN.md section 5.3): at power-up its
// capacitor charges through a resistor, the strobe dark; once it has, a
// MOSFET (GPIO 18) bypasses the resistor and the strobe begins. Only at boot:
// the supply may just have been plugged. Platform-free: the caller drives
// the pin and begins the strobe.

#include <stdint.h>

namespace fluante {

class Inrush {
public:
    static constexpr uint32_t kChargeMs = 2000;

    explicit Inrush(uint32_t chargeMs = kChargeMs) : chargeMs_(chargeMs) {}

    // Every tick, from its first: true once, when the bypass should close.
    bool update(uint32_t nowMs) {
        if (bypassed_) return false;
        if (!started_) {
            started_ = true;
            startMs_ = nowMs;
        }
        if (nowMs - startMs_ < chargeMs_) return false;
        bypassed_ = true;
        return true;
    }
    bool bypassed() const { return bypassed_; }

private:
    uint32_t chargeMs_;
    bool started_ = false;
    bool bypassed_ = false;
    uint32_t startMs_ = 0;
};

}  // namespace fluante

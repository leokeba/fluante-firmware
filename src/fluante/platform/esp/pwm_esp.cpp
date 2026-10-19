#if defined(ESP_PLATFORM)

// The classic ESP32's LEDC (docs/DESIGN.md section 4.1): the strobe on the
// high-speed group's timer 0 and channel 0 (TesserMotion takes LEDC from the
// top down, should one of its parts need it). ledc_timer_config() takes
// whole hertz and picks its own divider, so it only sets the timer up;
// plan()'s divider, resolution and clock are then written to the timer's
// register through the low-level layer, which the driver reads back for its
// duty checks (ledc_get_max_duty()). As ballet-photophonique's pwm_esp.cpp.

#include "driver/ledc.h"
#include "esp_log.h"
#include "fluante/pwm.h"
#include "hal/ledc_ll.h"
#include "soc/ledc_struct.h"

namespace fluante {

namespace {

const char* kTag = "fluante";

constexpr ledc_mode_t kMode = LEDC_HIGH_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;

class EspPwm : public Pwm {
public:
    bool attach(int pin) override {
        // APB at 1 kHz, 10 bits, until timing() writes the plan.
        ledc_timer_config_t t = {};
        t.speed_mode = kMode;
        t.timer_num = kTimer;
        t.duty_resolution = LEDC_TIMER_10_BIT;
        t.freq_hz = 1000;
        t.clk_cfg = LEDC_USE_APB_CLK;
        if (ledc_timer_config(&t) != ESP_OK) {
            ESP_LOGE(kTag, "strobe: timer setup failed");
            return false;
        }
        ledc_channel_config_t c = {};
        c.gpio_num = pin;
        c.speed_mode = kMode;
        c.channel = kChannel;
        c.timer_sel = kTimer;
        c.duty = 0;
        if (ledc_channel_config(&c) != ESP_OK) {
            ESP_LOGE(kTag, "strobe: GPIO %d refused", pin);
            return false;
        }
        return true;
    }

    void timing(const Timing& t) override {
        ledc_dev_t* hw = &LEDC;  // LEDC_LL_GET_HW() takes a group from ESP-IDF 6 on
        uint32_t bits = 0;
        ledc_ll_get_duty_resolution(hw, kMode, kTimer, &bits);
        ledc_ll_set_clock_divider(hw, kMode, kTimer, t.divider);
        ledc_ll_set_duty_resolution(hw, kMode, kTimer, t.bits);
        ledc_ll_set_clock_source(hw, kMode, kTimer, t.clock == Clock::Apb ? LEDC_APB_CLK : LEDC_REF_TICK);
        // A counter already past the new top would run on to the 20-bit wrap:
        // minutes on a slow clock. A new period starts instead.
        if (bits != t.bits) ledc_timer_rst(kMode, kTimer);
    }

    // ESP32's LEDC applies a duty at the end of the period, and
    // ledc_update_duty() spins in a critical section until the previous one
    // was applied: at low frequencies, longer than the interrupt watchdog
    // allows. So a duty is only written once the previous one is done; until
    // then the latest waits here, for poll().
    void duty(uint32_t counts) override {
        waiting_ = true;
        counts_ = counts;
        flush();
    }

    void poll() override { flush(); }

private:
    void flush() {
        if (!waiting_) return;
        if (LEDC.channel_group[kMode].channel[kChannel].conf1.duty_start) return;
        ledc_set_duty(kMode, kChannel, counts_);
        ledc_update_duty(kMode, kChannel);
        waiting_ = false;
    }

    bool waiting_ = false;
    uint32_t counts_ = 0;
};

}  // namespace

Pwm& defaultPwm() {
    static EspPwm pwm;
    return pwm;
}

}  // namespace fluante

#endif

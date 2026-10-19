// The strobe (docs/DESIGN.md section 5.1): its timer plan and what it gives
// the LEDC.
#pragma once

#include "rig.h"

using fluante::Strobe;
using tesserkit::pwm::kEsp32;

namespace {
// kEsp32's clocks' ids.
constexpr uint8_t kApb = 1, kRefTick = 2;
tesserkit::pwm::Timing plan(double hz, uint8_t bits) { return tesserkit::pwm::plan(kEsp32, hz, bits); }
}  // namespace

TEST(strobe_plan_at_60hz) {
    tesserkit::pwm::Timing t = plan(60.01, Strobe::kBits);
    CHECK(t.clock == kApb);
    CHECK_EQ(t.bits, 12);
    // A divider step is 1/83305 of the frequency: well under 0.001 Hz.
    CHECK(near(t.frequency, 60.01, 0.001));
    tesserkit::pwm::Timing next = plan(60.011, Strobe::kBits);
    CHECK(next != t);
}

TEST(strobe_plan_slow_and_fast) {
    // Under about 19 Hz, 12 bits from APB would need a divider over 1023:
    // the 1 MHz clock instead, down to 0.24 Hz.
    tesserkit::pwm::Timing slow = plan(1, Strobe::kBits);
    CHECK(slow.clock == kRefTick);
    CHECK_EQ(slow.bits, 12);
    CHECK(near(slow.frequency, 1, 1e-4));
    tesserkit::pwm::Timing fast = plan(1000, Strobe::kBits);
    CHECK_EQ(fast.bits, 12);
    CHECK(near(fast.frequency, 1000, 0.05));
}

TEST(strobe_applies_on_begin) {
    FakePwm pwm;
    Strobe s(pwm, kStrobePin);
    s.setDuty(10);  // stored only
    CHECK_EQ(pwm.timings, 0);
    CHECK_EQ(pwm.duties, 0);
    CHECK(s.begin());
    CHECK_EQ(pwm.pin, kStrobePin);
    CHECK_EQ(pwm.timings, 1);
    CHECK_EQ(pwm.counts, 410u);  // 10 % of 4096
    pwm.attachOk = false;
    Strobe broken(pwm, 40);
    CHECK(!broken.begin());
}

TEST(strobe_on_off_and_pulse) {
    Rig r;
    CHECK(r.strobe.on());
    CHECK_EQ(r.pwm.counts, 123u);  // 3 % of 4096
    // 123/4096 of 1/60.01 s.
    CHECK(near(r.strobe.pulseUs(), 500.4, 0.5));
    r.strobe.setOn(false);
    CHECK_EQ(r.pwm.counts, 0u);
    CHECK_EQ(r.strobe.pulseUs(), 0.0);
    r.strobe.setOn(true);
    r.strobe.setDuty(100);
    CHECK_EQ(r.pwm.counts, 4096u);
}

TEST(strobe_retimes_only_on_change) {
    Rig r;
    int timings = r.pwm.timings;
    r.strobe.setFrequency(60.01);
    CHECK_EQ(r.pwm.timings, timings);
    r.strobe.setFrequency(5);
    CHECK_EQ(r.pwm.timings, timings + 1);
    CHECK(r.pwm.timed.clock == kRefTick);
    CHECK_EQ(r.pwm.counts, 123u);
}

TEST(inrush_bypass_after_the_charge) {
    fluante::Inrush in;
    // From the first tick, whatever the clock says then.
    CHECK(!in.update(5000));
    CHECK(!in.update(6990));
    CHECK(!in.bypassed());
    CHECK(in.update(7000));
    CHECK(in.bypassed());
    CHECK(!in.update(7020));  // once
    fluante::Inrush wrap(100);
    CHECK(!wrap.update(0xFFFFFFF0u));
    CHECK(wrap.update(0x60u));  // across the clock's wrap
}

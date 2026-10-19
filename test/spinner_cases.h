// The spinner (docs/DESIGN.md section 5.2) on TesserMotion's fake TMC2209:
// the start, the driver's ramp, the deadman, stopping, the claim and halts.
#pragma once

#include "rig.h"

using fluante::Spinner;
using State = fluante::Spinner::State;

TEST(spinner_units) {
    Rig r;
    // 60 rpm of the sculpture through 54/60: 1.111 motor turns/s, 200 steps of
    // 256 microsteps.
    CHECK(near(r.spinner.microstepsPerSecond(60), 56888.9, 0.5));
    CHECK(near(r.spinner.microstepsPerSecond(-60), -56888.9, 0.5));
    r.spinner.settings.ratio = 1;
    r.driver.setMicrosteps(16);
    CHECK(near(r.spinner.microstepsPerSecond(60), 3200, 0.01));
}

TEST(spinner_boots_stopped_and_claims_the_driver) {
    Rig r;
    CHECK(r.spinner.state() == State::Stopped);
    CHECK(!r.driver.enabled());
    CHECK_EQ(r.written(r22::vactual), 0u);
    CHECK_EQ(r.driver.owner(), std::string("spinner"));
    CHECK(r.set("/driver/enabled", "true").status == tesser::Status::Busy);
    CHECK(r.set("/driver/velocity", "1000").status == tesser::Status::Busy);
    CHECK(!r.driver.enabled());
}

TEST(spinner_starts_settles_and_ramps) {
    Rig r;
    r.spinner.settings.speed = 60;
    r.spinner.settings.acceleration = 30;
    r.spinner.run(true);
    r.tick();
    CHECK(r.spinner.state() == State::Settling);
    CHECK(r.driver.enabled());
    // The driver's ramp, in its units: 30 rpm/s.
    CHECK(near(r.driver.ramp.acceleration, 28444.4, 0.5));
    r.run(0.9f);
    CHECK(r.spinner.state() == State::Settling);
    CHECK_EQ(r.vactual(), 0.0f);
    r.run(0.2f);
    CHECK(r.spinner.state() == State::Running);
    // 1 s of ramp at 30 rpm/s.
    float rpm = r.spinner.rpm();
    CHECK(rpm < 6);
    r.run(1);
    CHECK(near(r.spinner.rpm(), rpm + 30, 0.7));
    r.run(2);
    CHECK(near(r.spinner.rpm(), 60, 0.01));
    CHECK(near(r.vactual(), 56888.9f, 30));
}

TEST(spinner_settles_at_its_own_current) {
    Rig r;
    r.spinner.settings.settleCurrent = 0.03f;
    r.spinner.run(true);
    r.tick();
    CHECK(near(r.driver.currentOverride(), 0.03, 1e-6));
    r.run(1.1f);
    CHECK(r.spinner.state() == State::Running);
    CHECK_EQ(r.driver.currentOverride(), 0.0f);
    // Without one, the run current.
    Rig plain;
    plain.spinner.run(true);
    plain.tick();
    CHECK_EQ(plain.driver.currentOverride(), 0.0f);
}

TEST(spinner_outlives_the_deadman) {
    Rig r;
    r.spinner.settings.acceleration = 0;
    r.spinner.run(true);
    r.run(2);
    CHECK(r.spinner.state() == State::Running);
    r.run(5);  // five deadman times
    CHECK(r.driver.velocity() != 0);
    CHECK(r.vactual() > 0);
}

TEST(spinner_changes_speed_and_direction_on_a_ramp) {
    Rig r;
    r.spinner.settings.speed = 60;
    r.spinner.settings.acceleration = 60;
    r.spinner.run(true);
    r.run(3);
    CHECK(near(r.spinner.rpm(), 60, 0.01));
    r.spinner.settings.speed = -60;
    r.run(1);
    CHECK(near(r.spinner.rpm(), 0, 1.5));
    r.run(1.1f);
    CHECK(near(r.spinner.rpm(), -60, 0.01));
    CHECK(r.vactual() < 0);
}

TEST(spinner_stops_on_a_ramp) {
    Rig r;
    r.spinner.settings.speed = 60;
    r.spinner.settings.acceleration = 60;
    r.spinner.run(true);
    r.run(3);
    r.spinner.run(false);
    r.tick();
    CHECK(r.spinner.state() == State::Stopping);
    CHECK(r.driver.enabled());
    r.run(0.5f);
    CHECK(near(r.spinner.rpm(), 30, 1.5));
    r.run(0.6f);
    CHECK(r.spinner.state() == State::Stopped);
    CHECK(!r.driver.enabled());
    CHECK_EQ(r.spinner.rpm(), 0.0f);
    CHECK_EQ(r.written(r22::toff), 0u);
}

TEST(spinner_halts_on_stop_all_and_keeps_running) {
    Rig r;
    r.spinner.run(true);
    r.run(3);
    r.motion.stopAll();
    r.tick();
    CHECK(r.spinner.state() == State::Halted);
    CHECK(!r.driver.enabled());
    CHECK(r.spinner.running());  // stored as it was
    r.run(2);
    CHECK(r.spinner.state() == State::Halted);
    r.spinner.run(true);
    r.tick();
    CHECK(r.spinner.state() == State::Settling);
}

TEST(spinner_halts_when_disabled_underneath) {
    Rig r;
    r.spinner.settings.settleCurrent = 0.1f;
    r.spinner.run(true);
    r.tick();
    r.driver.enable(false);  // code isn't refused by the claim
    r.tick();
    CHECK(r.spinner.state() == State::Halted);
    CHECK_EQ(r.driver.currentOverride(), 0.0f);
    r.run(1);
    CHECK(!r.driver.enabled());
    CHECK(r.set("/spinner/running", "true").status == tesser::Status::Ok);
    r.tick();
    CHECK(r.spinner.state() == State::Settling);
}

TEST(spinner_waits_for_an_absent_driver) {
    Rig r;
    r.chip.powered = false;
    r.run(1);
    CHECK(!r.driver.present());
    r.spinner.run(true);
    r.tick();
    CHECK(r.spinner.state() == State::Absent);
    CHECK(!r.driver.enabled());
    r.chip.powered = true;
    r.run(3);  // found again within the driver's 2 s retry
    CHECK(r.driver.present());
    CHECK(r.spinner.state() == State::Settling || r.spinner.state() == State::Running);
}

TEST(spinner_halts_when_the_chip_resets) {
    Rig r;
    r.spinner.run(true);
    r.run(3);
    CHECK(r.spinner.state() == State::Running);
    r.chip.powerOn();  // lost power: the driver configures it again, disabled
    r.run(1);
    CHECK(r.spinner.state() == State::Halted);
    CHECK(!r.driver.enabled());
}

TEST(spinner_keeps_its_speed_through_a_microsteps_change) {
    for (const char* to : {"\"16\"", "\"1\"", "\"256\""}) {
        Rig r;
        r.spinner.settings.speed = 60;
        r.spinner.run(true);
        r.run(3);
        CHECK(near(r.spinner.rpm(), 60, 0.01));
        CHECK(r.set("/driver/config/microsteps", to).status == tesser::Status::Ok);
        for (int i = 0; i < 25; i++) {
            r.tick();
            CHECK(near(r.spinner.rpm(), 60, 0.1));
        }
        // What the chip runs, in full steps: 60 rpm through 0.9 is 222.2/s.
        uint32_t mres = r.written(r22::mres);
        CHECK(near(r.vactual() / static_cast<float>(256u >> mres), 222.2, 0.5));
    }
}

namespace {

// A TMC2209 left turning by a reset of the ESP32 alone: powered (no reset
// flag), its stage on, VACTUAL at 116 rpm of the sculpture (TSTEP 108: 434
// full steps/s) at 256 microsteps; MSCNT advancing `perRead` per read.
void turningChip(fake::Tmc2209& c, int32_t perRead) {
    c.regs[r22::GSTAT] = 0;
    c.regs[r22::DRV_STATUS] = r22::cs_actual.set(0, 31);
    c.regs[r22::TSTEP] = 108;
    c.regs[r22::CHOPCONF] = r22::mres.set(0x10000053, 0);
    c.mscntPerRead = perRead;
}

}  // namespace

TEST(spinner_goes_on_from_a_chip_found_turning) {
    Rig r(0, [](fake::Tmc2209& c) { turningChip(c, 140); });
    CHECK(r.driver.status().found == tessermotion::TmcFound::Turning);
    r.spinner.settings.speed = 100;
    r.spinner.run(true);  // as the stored state does, before Motion starts
    r.tick();
    CHECK(r.spinner.state() == State::Running);  // no settle
    CHECK(near(r.spinner.rpm(), 116, 1.5));
    for (int i = 0; i < 50; i++) {
        r.tick();
        CHECK(r.driver.enabled());  // never stopped
    }
    CHECK(near(r.spinner.rpm(), 100, 0.01));  // ramped down to the speed asked
    CHECK(near(r.vactual(), r.spinner.microstepsPerSecond(100), 30));
}

TEST(spinner_ramps_a_chip_found_turning_down_when_not_running) {
    Rig r(0, [](fake::Tmc2209& c) { turningChip(c, 140); });
    r.tick();
    CHECK(r.spinner.state() == State::Stopping);
    CHECK(r.driver.enabled());
    CHECK(r.spinner.rpm() > 100);
    r.run(2.1f);  // 116 rpm at 60 rpm/s
    CHECK(r.spinner.state() == State::Stopped);
    CHECK(!r.driver.enabled());
}

TEST(spinner_waits_for_a_chip_found_coasting) {
    Rig r(0, [](fake::Tmc2209& c) { turningChip(c, 0); });  // its direction can't be told
    CHECK(r.driver.status().found == tessermotion::TmcFound::Coasting);
    CHECK(!r.driver.enabled());
    r.spinner.settings.coastMs = 3000;
    r.spinner.run(true);
    r.run(2.9f);
    CHECK(r.spinner.state() == State::Stopped);
    CHECK(!r.driver.enabled());
    r.run(0.2f);
    CHECK(r.spinner.state() == State::Settling);
    CHECK(r.driver.enabled());
}

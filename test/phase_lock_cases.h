// The strobe locked to network time (docs/DESIGN.md section 5.4): a Fluante
// simulated with its LEDC counting on a crystal a few ppm off, sampled
// against a network clock with NowTP's noise.
#pragma once

#include "rig.h"

using fluante::PhaseLock;

namespace {

// One Fluante: the network's time is the true time; its own clock runs `ppm`
// fast from some boot instant, and its timer counts that clock.
struct LockSim {
    PhaseLock lock;
    tesserkit::pwm::Timing planned;
    double ppm;
    double localAtZero;
    double t = 0;  // network µs
    double pos;    // the counter, in counts
    int32_t trim = 0;
    bool restartPending = false;
    double restartAt = 0;  // local µs
    int restarts = 0;
    uint32_t rng = 12345;
    double noiseUs = 0;  // each sample's offset off by up to this
    double biasUs = 0;   // and by this, as a bad round would until the next

    // `phase`: where in its period the counter is at network time 0, as a
    // share of it.
    LockSim(double hz, double ppmOff, double phase = 0.37, double localStart = 123456789.0)
        : planned(tesserkit::pwm::plan(tesserkit::pwm::kEsp32, hz, fluante::Strobe::kBits)),
          ppm(ppmOff),
          localAtZero(localStart),
          pos(phase * static_cast<double>(1u << planned.bits)) {}

    double local() const { return localAtZero + t * (1 + ppm * 1e-6); }
    double counts() const { return static_cast<double>(1u << planned.bits); }
    // One count, in local µs: the divider counts the local crystal's clock.
    double stepUs() const { return (planned.divider + trim) / 256.0 / planned.clockHz * 1e6; }
    double periodUs() const { return 1e6 / planned.frequency; }

    // The device's ticks, 20 ms apart, each applying the dithered trim.
    double tickIn = 0;

    void advance(double us) {
        for (double done = 0; done < us; done += 100) {
            if ((tickIn -= 100) <= 0) {
                tickIn += 20000;
                trim = lock.step();
            }
            double l0 = local();
            t += 100;
            double l1 = local();
            if (restartPending && restartAt > l0 && restartAt <= l1) {
                pos = (l1 - restartAt) / stepUs();
                restartPending = false;
                restarts++;
            } else {
                pos += (l1 - l0) / stepUs();
            }
            pos = fmod(pos, counts());
        }
    }

    double noise() {
        rng = rng * 1664525u + 1013904223u;
        return (static_cast<double>(rng >> 8) / 16777216.0 * 2 - 1) * noiseUs;
    }

    // When the current period began, after the network's: the truth.
    double errorUs() const {
        double begunLocal = local() - pos * stepUs();
        double begun = (begunLocal - localAtZero) / (1 + ppm * 1e-6);
        return remainder(begun, periodUs());
    }

    // A sample and its action, then a second.
    void second() {
        PhaseLock::Timer timer{periodUs(), planned.divider, stepUs()};
        PhaseLock::Sample s;
        s.count = static_cast<uint32_t>(pos);
        s.localUs = llround(local());
        s.offsetUs = llround(t - local() + noise() + biasUs);
        PhaseLock::Action a = lock.update(timer, s);
        if (a.restart) {
            restartPending = true;
            restartAt = static_cast<double>(a.restartAtUs);
        }
        advance(1e6);
    }

    void run(int seconds) {
        for (int i = 0; i < seconds; i++) second();
    }

    // The largest error over `seconds`.
    double worst(int seconds) {
        double w = 0;
        for (int i = 0; i < seconds; i++) {
            second();
            w = fmax(w, fabs(errorUs()));
        }
        return w;
    }
};

}  // namespace

TEST(phase_lock_first_lock_jumps_then_holds) {
    LockSim f(60.01, 20);
    f.second();
    CHECK(f.lock.state() == PhaseLock::State::Acquiring);
    CHECK_EQ(f.restarts, 1);  // 30 ms on
    // Within the restart's own error, then steered: the crystal's 20 ppm
    // learned by the integral.
    CHECK(fabs(f.errorUs()) < 50);
    f.run(120);
    CHECK(f.lock.state() == PhaseLock::State::Locked);
    CHECK(f.worst(300) < 10);
    CHECK_EQ(f.restarts, 1);
    // A crystal 20 ppm fast makes short periods: about +1.7 steps lengthen
    // them, at a divider of 83 k.
    CHECK(near(f.lock.trim(), 20e-6 * f.planned.divider, 0.2));
}

TEST(phase_lock_two_fluantes_flash_together) {
    LockSim a(60.01, 20, 0.2, 5e6), b(60.01, -20, 0.7, 917e6);
    a.noiseUs = b.noiseUs = 90;  // NowTP's p5/p95
    a.run(120);
    b.run(120);
    double wa = 0, wb = 0, apart = 0;
    for (int i = 0; i < 300; i++) {
        a.second();
        b.second();
        wa = fmax(wa, fabs(a.errorUs()));
        wb = fmax(wb, fabs(b.errorUs()));
        apart = fmax(apart, fabs(a.errorUs() - b.errorUs()));
    }
    CHECK(wa < 60);
    CHECK(wb < 60);
    CHECK(apart < 100);
    CHECK(a.lock.state() == PhaseLock::State::Locked);
}

TEST(phase_lock_filters_a_bad_round) {
    LockSim f(60.01, 5);
    f.noiseUs = 40;
    f.run(120);
    CHECK(f.lock.state() == PhaseLock::State::Locked);
    // A round off by 120 µs until the next, 5 s later: followed a little.
    f.biasUs = 120;
    double w = f.worst(5);
    f.biasUs = 0;
    w = fmax(w, f.worst(60));
    CHECK(w < 80);
    CHECK_EQ(f.restarts, 1);
}

TEST(phase_lock_starts_over_when_network_time_jumps) {
    LockSim f(60.01, -10);
    f.run(120);
    CHECK(f.lock.state() == PhaseLock::State::Locked);
    // A new reference: network time 5 ms off from the old one.
    f.biasUs = 5000;
    f.run(2);
    CHECK_EQ(f.restarts, 1);  // set aside so far
    CHECK(f.lock.state() == PhaseLock::State::Acquiring);
    f.run(3);
    CHECK_EQ(f.restarts, 2);
    f.run(90);
    CHECK(f.lock.state() == PhaseLock::State::Locked);
    // Against the new network time, 5 ms ahead of the old.
    double e = remainder(f.errorUs() + 5000, f.periodUs());
    CHECK(fabs(e) < 20);
}

TEST(phase_lock_new_frequency_restarts) {
    LockSim f(60.01, 15);
    f.run(120);
    CHECK_EQ(f.restarts, 1);
    f.planned = tesserkit::pwm::plan(tesserkit::pwm::kEsp32, 48.5, fluante::Strobe::kBits);
    f.run(2);
    CHECK_EQ(f.restarts, 2);
    f.run(120);
    CHECK(f.lock.state() == PhaseLock::State::Locked);
    CHECK(fabs(f.errorUs()) < 10);
}

TEST(phase_lock_slow_and_fast_strobes) {
    // 1 Hz from the 1 MHz clock, 1 kHz from APB.
    LockSim slow(1, 20, 0.3), fast(1000, -20, 0.3);
    slow.run(150);
    fast.run(150);
    CHECK(slow.lock.state() == PhaseLock::State::Locked);
    CHECK(fast.lock.state() == PhaseLock::State::Locked);
    // At 1 Hz a count is 244 µs: the sample's own uncertainty, filtered.
    CHECK(slow.worst(60) < 60);
    CHECK(fast.worst(60) < 20);
}

TEST(phase_lock_off_and_waiting) {
    PhaseLock lock;
    CHECK(lock.enabled());
    CHECK(lock.state() == PhaseLock::State::Waiting);
    LockSim f(60.01, 20);
    f.run(60);
    double held = f.lock.trim();
    CHECK(held > 1);
    // No network time: the trim held, the next sample a first lock.
    f.lock.wait();
    CHECK(f.lock.state() == PhaseLock::State::Waiting);
    CHECK_EQ(f.lock.trim(), held);
    // Off: untrimmed.
    f.lock.setEnabled(false);
    CHECK(f.lock.state() == PhaseLock::State::Off);
    CHECK_EQ(f.lock.trim(), 0.0);
    f.second();
    CHECK_EQ(f.trim, 0);
    CHECK(!f.restartPending);
}

TEST(phase_lock_dithers_the_trim) {
    PhaseLock lock;
    LockSim f(60.01, 20);
    f.run(120);
    // Whole steps, averaging the trim over the ticks.
    double sum = 0;
    for (int i = 0; i < 1000; i++) {
        int32_t step = f.lock.step();
        CHECK(step == static_cast<int32_t>(floor(f.lock.trim())) ||
              step == static_cast<int32_t>(ceil(f.lock.trim())));
        sum += step;
    }
    CHECK(near(sum / 1000, f.lock.trim(), 0.002));
    CHECK_EQ(lock.step(), 0);
}

TEST(phase_lock_due_once_a_second) {
    PhaseLock lock;
    CHECK(lock.due(5));
    CHECK(!lock.due(500));
    CHECK(!lock.due(1004));
    CHECK(lock.due(1005));
    // Across the millisecond counter's wrap.
    PhaseLock wrap;
    CHECK(wrap.due(0xFFFFFF00u));
    CHECK(wrap.due(0xFFFFFF00u + PhaseLock::kIntervalMs));
}

// The layers (docs/DESIGN.md section 5.5): what a layer asks of the output,
// the pin handed between the strobe and the layers, their union on the pin,
// and two Fluantes' layers locked to network time.
#pragma once

#include <math.h>
#include <stdlib.h>

#include "rig.h"

using fluante::Layer;
using fluante::Layers;
using fluante::NetworkClock;

namespace {

// One Fluante's layers on its own crystal, `ppm` fast from `localStart`,
// network time being the true time.
struct LayersSim {
    FakePulses pulses;
    Layers layers{pulses, kStrobePin};
    double ppm;
    double localStart;
    double t = 0;  // network µs
    uint32_t rng;
    double noiseUs = 0;  // each NowTP sample off by up to this
    bool synced = true;

    LayersSim(double ppmOff, double start, uint32_t seed = 12345) : ppm(ppmOff), localStart(start), rng(seed) {
        pulses.clock = local();
        layers.setOn(true);
        layers.begin();
    }

    int64_t local() const { return static_cast<int64_t>(llround(localStart + t * (1 + ppm * 1e-6))); }
    double networkAt(int64_t localUs) const {
        return (static_cast<double>(localUs) - localStart) / (1 + ppm * 1e-6);
    }
    double noise() {
        rng = rng * 1103515245u + 12345u;
        return ((rng >> 8) / 16777216.0 * 2 - 1) * noiseUs;
    }
    // Ticks of 20 ms, the device's, for `seconds` of network time.
    void run(double seconds) {
        for (double done = 0; done < seconds * 1e6; done += kTickUs) {
            t += kTickUs;
            pulses.run(local());
            Layers::NetworkOffset offset = [this](int64_t& o) {
                if (!synced) return false;
                o = static_cast<int64_t>(llround(t + noise())) - local();
                return true;
            };
            layers.poll(static_cast<uint32_t>(local() / 1000), offset);
        }
    }
    // The rising edges from network time `fromUs` to `toUs`, in network
    // time.
    std::vector<double> rises(double fromUs, double toUs = INFINITY) const {
        std::vector<double> r;
        for (const auto& e : pulses.edges) {
            double n = networkAt(e.first);
            if (e.second && n >= fromUs && n < toUs) r.push_back(n);
        }
        return r;
    }
};

}  // namespace

TEST(layers_train) {
    Layer l;
    l.frequency = 50;
    l.phase = 90;
    l.duty = 2.5f;
    tesserkit::pulses::Train t = Layers::train(l);
    CHECK(t.on);
    CHECK(near(t.periodUs, 20000, 1e-9));
    CHECK(near(t.phaseUs, 5000, 1e-9));
    CHECK(near(t.pulseUs, 500, 1e-6));
    l.duty = 0;
    CHECK(!Layers::train(l).on);
    l.duty = 3;
    l.on = false;
    CHECK(!Layers::train(l).on);
}

TEST(layers_take_the_pin_from_the_strobe) {
    Rig r;
    // At boot, once the capacitor has charged: the strobe, the layers off.
    fluante::route(r.strobe, r.layers);
    CHECK(r.pwm.attached);
    CHECK(!r.layers.begun());
    CHECK(r.set("/layers/on", "true").status == tesser::Status::Ok);
    fluante::route(r.strobe, r.layers);
    CHECK(!r.pwm.attached);
    CHECK_EQ(r.pwm.detaches, 1);
    CHECK(r.layers.begun());
    CHECK_EQ(r.pulses.pin, kStrobePin);
    CHECK_EQ(r.get("/layers/active").body, "true");
    fluante::route(r.strobe, r.layers);  // every tick: nothing more
    CHECK_EQ(r.pulses.begins, 1);
    CHECK(r.set("/layers/on", "false").status == tesser::Status::Ok);
    fluante::route(r.strobe, r.layers);
    CHECK(!r.layers.begun());
    CHECK_EQ(r.pulses.ends, 1);
    CHECK(r.pwm.attached);
    CHECK_EQ(r.get("/layers/active").body, "false");
}

TEST(layers_refused_leave_the_strobe) {
    Rig r;
    fluante::route(r.strobe, r.layers);
    r.pulses.beginOk = false;  // a build without the IRAM-safe alarm, say
    r.layers.setOn(true);
    fluante::route(r.strobe, r.layers);
    CHECK(r.layers.refused());
    CHECK(r.pwm.attached);  // given back at once
    fluante::route(r.strobe, r.layers);
    CHECK_EQ(r.pulses.begins, 1);  // not tried again every tick
    CHECK(r.pwm.attached);
    // Asked again: tried again.
    r.pulses.beginOk = true;
    r.layers.setOn(false);
    r.layers.setOn(true);
    fluante::route(r.strobe, r.layers);
    CHECK(r.layers.begun());
    CHECK(!r.pwm.attached);
}

TEST(layers_union_on_the_pin) {
    // Free-running (sync off): periods counted on the local clock.
    LayersSim f(0, 1000000);
    f.layers.clock().setEnabled(false);
    Layer a;
    a.frequency = 10;  // a 1 ms flash every 100 ms
    a.duty = 1;
    Layer b;
    b.frequency = 4;  // a 1 ms flash every 250 ms, half a period in
    b.phase = 180;
    b.duty = 0.4f;
    f.layers.list = {a, b};
    f.run(3);
    std::vector<double> rises = f.rises(2e6 - 1000, 3e6 - 1000);
    // 10 of a, 4 of b: b's at 125, 375, 625 and 875 ms fall between a's.
    CHECK_EQ(rises.size(), 14u);
    for (double r : rises) {
        double local = r + 1e6;
        double inA = fmod(local, 100000);
        double inB = fmod(local - 125000, 250000);
        CHECK(near(inA, 0, 2) || near(inA, 100000, 2) || near(inB, 0, 2) || near(inB, 250000, 2));
    }
    // A layer turned off leaves its flashes out.
    f.layers.list[1].on = false;
    double from = f.t;
    f.run(1);
    CHECK_EQ(f.rises(from + 50000, from + 950000).size(), 9u);
}

TEST(layers_union_merges_overlaps) {
    LayersSim f(0, 0);
    f.layers.clock().setEnabled(false);
    Layer a;
    a.frequency = 10;
    a.duty = 2;  // 0–2 ms of each 100 ms
    Layer b = a;
    b.phase = 3.6f;  // 1–3 ms: overlapping a's
    f.layers.list = {a, b};
    f.run(2);
    // One flash per period, 0 to 3 ms: not two.
    double from = 1e6 - 500;
    std::vector<double> rises = f.rises(from, 2e6 - 500);
    CHECK_EQ(rises.size(), 10u);
    int64_t up = 0;
    for (const auto& e : f.pulses.edges) {
        if (static_cast<double>(e.first) < from) continue;
        if (e.second) {
            up = e.first;
        } else if (up) {
            CHECK(near(static_cast<double>(e.first - up), 3000, 2));
        }
    }
}

TEST(layers_lock_to_network_time) {
    // Two Fluantes, their crystals ±15 ppm apart, booted at different times,
    // the same layer at 60.01 Hz: their flashes at whole periods of network
    // time, within NowTP's noise.
    LayersSim a(15, 3.7e9, 1);
    LayersSim b(-12, 1.234e8, 2);
    for (LayersSim* f : {&a, &b}) {
        f->noiseUs = 60;
        f->layers.list[0].frequency = 60.01;
    }
    a.run(150);
    b.run(150);
    CHECK(a.layers.clock().state() == NetworkClock::State::Locked);
    CHECK(b.layers.clock().state() == NetworkClock::State::Locked);
    // The crystals' offsets learned, give or take the last sample's pull
    // (its error over the loop's 10 s: 6 ppm for 60 µs).
    CHECK(near(a.layers.clock().ppm(), -15, 1.5 + fabs(a.layers.clock().errorUs()) / NetworkClock::kTauS));
    CHECK(near(b.layers.clock().ppm(), 12, 1.5 + fabs(b.layers.clock().errorUs()) / NetworkClock::kTauS));
    double period = 1e6 / 60.01;
    for (LayersSim* f : {&a, &b}) {
        std::vector<double> rises = f->rises(140e6);
        CHECK(rises.size() > 500u);
        double worst = 0;
        for (double r : rises) worst = fmax(worst, fabs(remainder(r, period)));
        CHECK(worst < 40);
    }
}

TEST(layers_without_network_time_run_on) {
    LayersSim f(10, 5e8);
    f.synced = false;
    f.run(3);
    CHECK(f.layers.clock().state() == NetworkClock::State::Waiting);
    // Flashing all the same, on the local clock.
    CHECK(f.rises(1e6).size() > 100u);
    f.synced = true;
    f.run(60);
    CHECK(f.layers.clock().state() == NetworkClock::State::Locked);
}

TEST(network_clock_first_lock_jumps_then_steers) {
    NetworkClock c;
    CHECK(c.state() == NetworkClock::State::Waiting);
    c.update(1000000, 5000000);
    CHECK(c.state() == NetworkClock::State::Acquiring);
    const tesserkit::pulses::Timebase& tb = c.timebase();
    CHECK(near(tb.offsetUs + tb.rate * 1000000, 6000000, 1e-6));
    // 50 µs off a second later: the rate pulled, the base continuous.
    double before = tb.offsetUs + tb.rate * 2000000;
    c.update(2000000, 5000050);
    CHECK(near(c.errorUs(), 50, 1e-6));
    CHECK(near(tb.offsetUs + tb.rate * 2000000, before, 1e-6));
    CHECK(c.ppm() > 0);
}

TEST(network_clock_sets_aside_bad_rounds) {
    NetworkClock c;
    int64_t local = 1000000;
    c.update(local, 0);
    for (int i = 0; i < 2; i++) {
        local += 1000000;
        c.update(local, 5000);  // 5 ms off: set aside
        CHECK(near(c.timebase().offsetUs + c.timebase().rate * static_cast<double>(local),
                   static_cast<double>(local), 1));
    }
    // The third in a row: the network time jumped, followed at once.
    local += 1000000;
    c.update(local, 5000);
    CHECK(near(c.timebase().offsetUs + c.timebase().rate * static_cast<double>(local),
               static_cast<double>(local + 5000), 1e-3));
}

TEST(network_clock_off_is_the_local_clock) {
    NetworkClock c;
    c.update(1000000, 777);
    c.setEnabled(false);
    CHECK(c.state() == NetworkClock::State::Off);
    CHECK_EQ(c.timebase().offsetUs, 0.0);
    CHECK_EQ(c.timebase().rate, 1.0);
    c.update(2000000, 777);
    CHECK_EQ(c.timebase().offsetUs, 0.0);
}

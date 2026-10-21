// A Fluante on the host: the strobe on a fake LEDC, the layers on a fake
// alarm running TesserKIT's engine on the rig's clock, the spinner on
// TesserMotion's fake TMC2209 behind a fake UART, the tree, and a clock ticked
// at 50 Hz as the Motion task would.
#pragma once

#include <math.h>

#include <stdint.h>

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ArduinoJson.h>

#include "Fluante.h"
#include "TesserAPI.h"
#include "TesserMotion.h"
#include "tesserkit/pulses.h"
#include "tesserkit/pwm.h"
#include "check.h"
#include "fake_hw.h"
#include "fake_tmc.h"
#include "tessermotion/tmc/tmc2209_map.h"

namespace {

namespace r22 = tessermotion::tmc2209;

constexpr uint32_t kTickUs = 20000;
constexpr int kStrobePin = 19;
constexpr int kEnablePin = 13;

// What the strobe gave the LEDC: its one output.
struct FakePwm : tesserkit::Pwm {
    int pin = -1;
    bool attachOk = true;
    int timings = 0;
    tesserkit::pwm::Timing timed;
    uint32_t counts = 0;
    int duties = 0;

    const tesserkit::pwm::Chip& chip() const override { return tesserkit::pwm::kEsp32; }
    bool attached = false;
    int detaches = 0;
    int attach(int p) override {
        pin = p;
        attached = attachOk;
        return attachOk ? 0 : -1;
    }
    void detach(int) override {
        attached = false;
        detaches++;
    }
    void timing(int, const tesserkit::pwm::Timing& t) override {
        timed = t;
        timings++;
    }
    void duty(int, uint32_t c) override {
        counts = c;
        duties++;
    }
    // The counter and the local clock, as a test sets them.
    bool sampleOk = true;
    uint32_t count = 0;
    int64_t localUs = 0;
    bool sample(int, uint32_t& c, int64_t& l) override {
        c = count;
        l = localUs;
        return sampleOk;
    }
    int restarts = 0;
    int64_t restartUs = 0;
    bool restartAt(int, int64_t l) override {
        restarts++;
        restartUs = l;
        return true;
    }
};


// The layers' alarm: TesserKIT's engine stepped at each alarm on a clock the
// rig moves, the pin's edges recorded.
struct FakePulses : tesserkit::Pulses {
    tesserkit::pulses::Engine engine;
    bool beginOk = true;
    bool taken = false;
    int pin = -1;
    int begins = 0, ends = 0;
    int64_t clock = 1000000;
    bool armed = false;
    int64_t alarm = 0;
    bool level = false;
    std::vector<std::pair<int64_t, bool>> edges;

    bool begin(int p) override {
        begins++;
        if (!beginOk || taken) return false;
        taken = true;
        pin = p;
        engine.clear();
        engine.stats = tesserkit::pulses::Stats();
        armed = false;
        return true;
    }
    void end() override {
        ends++;
        taken = false;
        armed = false;
        engine.clear();
        set(false, clock);
    }
    int64_t nowUs() override { return clock; }
    void install(const tesserkit::pulses::Plan& p) override {
        if (!taken) return;
        engine.install(p, clock);
        step(clock, clock);
    }
    tesserkit::pulses::Stats stats() override { return engine.stats; }
    void resetStats() override { engine.stats = tesserkit::pulses::Stats(); }

    void step(int64_t now, int64_t due) {
        int64_t next = 0;
        set(engine.advance(now, due, next), now);
        armed = next != INT64_MAX;
        if (armed) alarm = next;
    }
    void set(bool l, int64_t at) {
        if (l == level) return;
        level = l;
        edges.emplace_back(at, l);
    }
    // The clock to `untilUs`, the alarms answered on time on the way.
    void run(int64_t untilUs) {
        while (armed && alarm <= untilUs) {
            clock = alarm;
            engine.stats.alarms++;
            step(clock, alarm);
        }
        clock = untilUs;
    }
    // The rising edges since `fromUs`.
    std::vector<int64_t> rises(int64_t fromUs) const {
        std::vector<int64_t> r;
        for (const auto& e : edges) {
            if (e.second && e.first >= fromUs) r.push_back(e.first);
        }
        return r;
    }
};

struct Rig {
    FakePwm pwm;
    fluante::Strobe strobe{pwm, kStrobePin};
    FakePulses pulses;
    fluante::Layers layers{pulses, kStrobePin};
    fake::Gpio gpio;
    fake::Uart uart;
    fake::Tmc2209 chip;
    tessermotion::Tmc2209 driver{uart, config(), &gpio};
    fluante::Spinner spinner{driver};
    tessermotion::Motion motion;
    tesser::Api api;
    uint64_t now = 1000000;

    static tessermotion::Tmc2209Config config() {
        tessermotion::Tmc2209Config c;
        c.enable = kEnablePin;
        return c;
    }

    // `idle` seconds of ticks, so the driver has read every register.
    // `prepare` sets the chip up before the driver looks for it.
    explicit Rig(float idle = 0.5f, std::function<void(fake::Tmc2209&)> prepare = nullptr) {
        if (prepare) prepare(chip);
        chip.attach(uart);
        driver.settings.microsteps = 256;
        motion.add(spinner);
        motion.add(driver);
        fluante::mountTree(api, strobe, layers, spinner, driver, motion);
        motion.begin();
        strobe.begin();
        run(idle);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            now += kTickUs;
            fake::now = now;
            motion.step(now);
        }
    }
    void run(float seconds) { tick(static_cast<int>(seconds * 1e6f / kTickUs + 0.5f)); }

    // The velocity the chip was last given, in µsteps/s.
    float vactual() const {
        return static_cast<float>(r22::vactual.get(chip.written[r22::VACTUAL])) * 12e6f / 16777216.0f;
    }
    uint32_t written(tessermotion::TmcField f) const {
        return static_cast<uint32_t>(f.get(chip.written[f.reg]));
    }

    struct Reply {
        tesser::Status status;
        std::string body;
    };
    Reply request(tesser::Op op, std::string_view path, std::string_view body = {}) {
        JsonDocument doc;
        if (!body.empty()) deserializeJson(doc, body.data(), body.size());
        tesser::Request req;
        req.op = op;
        req.path = path;
        req.body = doc.as<JsonVariantConst>();
        req.client.authenticated = true;
        tesser::StringReply reply;
        api.handle(req, reply);
        return {reply.status, reply.body};
    }
    Reply get(std::string_view path) { return request(tesser::Op::Get, path); }
    Reply set(std::string_view path, std::string_view body) { return request(tesser::Op::Set, path, body); }
};

bool near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

}  // namespace

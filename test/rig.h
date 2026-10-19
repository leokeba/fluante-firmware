// A Fluante on the host: the strobe on a fake LEDC, the spinner on
// TesserMotion's fake TMC2209 behind a fake UART, the tree, and a clock ticked
// at 50 Hz as the Motion task would.
#pragma once

#include <math.h>

#include <string>
#include <string_view>

#include <ArduinoJson.h>

#include "Fluante.h"
#include "TesserAPI.h"
#include "TesserMotion.h"
#include "check.h"
#include "fake_hw.h"
#include "fake_tmc.h"
#include "tessermotion/tmc/tmc2209_map.h"

namespace {

namespace r22 = tessermotion::tmc2209;

constexpr uint32_t kTickUs = 20000;
constexpr int kStrobePin = 19;
constexpr int kEnablePin = 13;

// What the strobe gave the LEDC.
struct FakePwm : fluante::Pwm {
    int pin = -1;
    bool attachOk = true;
    int timings = 0;
    fluante::Timing timed;
    uint32_t counts = 0;
    int duties = 0;

    bool attach(int p) override {
        pin = p;
        return attachOk;
    }
    void timing(const fluante::Timing& t) override {
        timed = t;
        timings++;
    }
    void duty(uint32_t c) override {
        counts = c;
        duties++;
    }
};

struct Rig {
    FakePwm pwm;
    fluante::Strobe strobe{pwm, kStrobePin};
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
    explicit Rig(float idle = 0.5f) {
        chip.attach(uart);
        driver.settings.microsteps = 256;
        motion.add(spinner);
        motion.add(driver);
        fluante::mountTree(api, strobe, spinner, driver, motion);
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

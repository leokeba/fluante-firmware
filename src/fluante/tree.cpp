// The /strobe and /spinner trees (docs/DESIGN.md section 6).
#include "fluante/tree.h"

#include "tesser/status.h"
#include "tessermotion/tree_setting.h"

namespace fluante {

namespace {

using tesser::Access;
using tesser::Object;
using tessermotion::setting;

}  // namespace

void describe(Object& o, Strobe& s) {
    o.writeAccess(Access::User);
    o.value("on", [&s] { return s.on(); }, [&s](bool v) { s.setOn(v); }).persist().doc("Off holds the LED dark");
    o.value("frequency", [&s] { return s.frequency(); }, [&s](double v) { s.setFrequency(v); })
        .range(Strobe::kLowestHz, Strobe::kHighestHz)
        .step(0.001)
        .unit("Hz")
        .persist()
        .doc("Flashes per second. The timer reaches it in steps: see Applied")
        .ui("widget", "number");
    o.value("duty", [&s] { return s.duty(); }, [&s](float v) { s.setDuty(v); })
        .range(0, 100)
        .step(0.1)
        .unit("%")
        .persist()
        .doc("The share of each period the LED is lit: shorter flashes give sharper, darker images");

    auto& applied = o.object("applied").label("Applied").doc("What the timer runs").ui("inline");
    applied.value("frequency", [&s] { return s.timing().frequency; }).unit("Hz").ui("precision", 4).watch();
    applied.value("pulse", [&s] { return s.pulseUs(); }).unit("µs").ui("precision", 0).watch();
    applied.value("bits", [&s] { return s.timing().bits; }).unit("bits").ui("advanced").watch();
    o.value("gpio", [&s] { return s.pin(); }).label("GPIO").ui("advanced");
}

void describe(Object& o, Spinner& sp) {
    o.writeAccess(Access::User);
    Spinner::Settings& s = sp.settings;
    o.value("running", [&sp] { return sp.running(); }, [&sp](bool on) { sp.run(on); })
        .persist()
        .watch()
        .doc("Turns the sculpture, and again at power-up while on");
    o.value("state", [&sp] { return sp.state(); }, {"stopped", "absent", "settling", "running", "stopping", "halted"})
        .watch()
        .doc("absent: the driver doesn't answer. halted: the driver was disabled underneath; set running again");
    o.value("rpm", [&sp] { return sp.rpm(); }).unit("rpm").ui("precision", 1).watch().doc("The speed now, as ramped");
    setting(o, "speed", sp, s.speed)
        .range(-240, 240)
        .step(0.1)
        .unit("rpm")
        .persist()
        .doc("Turns per minute of the sculpture; negative turns the other way. Approximate: the driver's clock "
             "isn't calibrated");
    setting(o, "acceleration", sp, s.acceleration)
        .range(0, 600)
        .step(1)
        .unit("rpm/s")
        .persist()
        .doc("Up and down; 0 jumps. Sets the driver's ramp (/driver/ramp)");

    auto& mech = o.object("mechanics")
                     .label("Mechanics")
                     .persist()
                     .writeAccess(Access::Admin)
                     .ui("collapsed");
    setting(mech, "ratio", sp, s.ratio)
        .range(0.01, 100)
        .step(0.001)
        .doc("Sculpture turns per motor turn: the motor pulley's teeth over the sculpture's");
    setting(mech, "stepsPerTurn", sp, s.stepsPerTurn).range(1, 1000).doc("The motor's full steps per turn");

    auto& start = o.object("start")
                      .label("Start")
                      .persist()
                      .writeAccess(Access::Admin)
                      .doc("What starting does before turning")
                      .ui("collapsed");
    setting(start, "settle", sp, s.settleMs).range(0, 10000).unit("ms").doc("At standstill after enabling");
    setting(start, "settleCurrent", sp, s.settleCurrent)
        .range(0, 2)
        .step(0.01)
        .unit("A")
        .doc("rms while settling; 0: the run current. The old firmware settled at 0.03");
}

void mountTree(tesser::Api& api, Strobe& strobe, Spinner& spinner, tessermotion::Tmc2209& driver,
               tessermotion::Motion& motion) {
    api.mount("strobe", strobe).label("Strobe").ui("icon", "bulb");
    api.mount("spinner", spinner).label("Rotation").ui("icon", "motor");
    api.mount("driver", driver).label("Motor driver").ui("icon", "motor").ui("group", "Hardware");
    api.mount("motion", motion).label("Control loop").ui("icon", "chart").ui("group", "Hardware");
}

}  // namespace fluante

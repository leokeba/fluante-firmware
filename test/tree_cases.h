// The /strobe and /spinner trees (docs/DESIGN.md section 6): validation and
// persistence.
#pragma once

#include <vector>

#include "rig.h"

using tesser::Status;

TEST(tree_declares_cleanly) {
    Rig r;
    CHECK_EQ(tesser::declarationErrors(), 0);
    CHECK(r.get("/strobe").status == Status::Ok);
    CHECK_EQ(r.get("/strobe/on").body, "true");
    CHECK_EQ(r.get("/spinner/state").body, "\"stopped\"");
    CHECK(r.get("/driver/status/present").status == Status::Ok);
    CHECK(r.get("/motion").status == Status::Ok);
}

TEST(tree_validates) {
    Rig r;
    CHECK(r.set("/strobe/frequency", "0.5").status == Status::InvalidValue);
    CHECK(r.set("/strobe/duty", "101").status == Status::InvalidValue);
    CHECK(r.set("/spinner/speed", "500").status == Status::InvalidValue);
    CHECK(r.set("/spinner/state", "\"running\"").status != Status::Ok);
    CHECK(r.set("/strobe/frequency", "48.5").status == Status::Ok);
    CHECK(near(r.pwm.timed.frequency, 48.5, 0.001));
}

TEST(tree_persists_and_restores) {
    std::string stored;
    {
        Rig r;
        CHECK(r.set("/strobe", R"({"on":false,"frequency":72.25,"duty":1.5})").status == Status::Ok);
        CHECK(r.set("/strobe/sync/on", "false").status == Status::Ok);
        CHECK(r.set("/spinner", R"({"speed":-90,"acceleration":20,"running":true})").status == Status::Ok);
        CHECK(r.set("/spinner/mechanics/ratio", "0.5").status == Status::Ok);
        CHECK(r.set("/spinner/start", R"({"settle":250})").status == Status::Ok);
        CHECK(r.set("/driver/config/runCurrent", "0.95").status == Status::Ok);
        stored = r.api.persistedState();
    }
    Rig r;
    std::vector<std::string> skipped;
    CHECK(r.api.restore(stored, &skipped) == Status::Ok);
    CHECK(skipped.empty());
    CHECK(!r.strobe.on());
    CHECK_EQ(r.strobe.frequency(), 72.25);
    CHECK_EQ(r.strobe.duty(), 1.5f);
    CHECK(!r.strobe.lock().enabled());
    CHECK_EQ(r.spinner.settings.speed, -90.0f);
    CHECK_EQ(r.spinner.settings.acceleration, 20.0f);
    CHECK_EQ(r.spinner.settings.ratio, 0.5f);
    CHECK_EQ(r.spinner.settings.settleMs, 250u);
    CHECK(r.spinner.running());  // turns again at power-up
    CHECK(near(r.driver.settings.runCurrent, 0.95, 1e-6));
}

TEST(tree_skips_the_old_settle_current) {
    // Stored by firmware before 2026-10-20: the rest still applies.
    Rig r;
    std::vector<std::string> skipped;
    CHECK(r.api.restore(R"({"spinner":{"start":{"settle":250,"settleCurrent":0.03}}})", &skipped) == Status::Ok);
    CHECK_EQ(skipped.size(), 1u);
    CHECK_EQ(r.spinner.settings.settleMs, 250u);
}

TEST(tree_sync) {
    Rig r;
    CHECK_EQ(r.get("/strobe/sync/on").body, "true");
    CHECK_EQ(r.get("/strobe/sync/state").body, "\"waiting\"");
    CHECK(r.set("/strobe/sync/on", "false").status == Status::Ok);
    CHECK(!r.strobe.lock().enabled());
    CHECK_EQ(r.get("/strobe/sync/state").body, "\"off\"");
    CHECK_EQ(r.get("/strobe/sync/trim").body, "0");
}

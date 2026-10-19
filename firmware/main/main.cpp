// Fluante (docs/DESIGN.md): TesserKIT for Wi-Fi, updates, files, accounts,
// the log and the Fluantes nearby over ESP-NOW (NowTP) under /peers;
// TesserMotion for the TMC2209; this repository's strobe and spinner.
//
// Flash it, join the "fluante-xxxx" network from a phone, and the interface
// opens. A fresh Fluante boots with its motor off and its strobe on: check
// the motor's current (/driver/config/runCurrent), then turn
// /spinner/running on.
// While on, it turns again at every power-up.
//
// For development, a gitignored main/secrets.h may define WIFI_SSID and
// WIFI_PASSWORD, and WIFI_TX_POWER (dBm) for boards with a weak supply.
#include "Fluante.h"
#include "TesserKIT.h"
#include "TesserMotion.h"
#include "board.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

using namespace tessermotion;

namespace {

tesserkit::Device dev("Fluante");
tesserkit::System sys(dev);
tesserkit::Network net(dev);
tesserkit::Firmware firmware(dev);
tesserkit::Files files(dev);  // LittleFS: the settings
tesserkit::Auth auth(dev);    // open until an admin account exists
tesserkit::NowTp espNow(dev);  // the Fluantes nearby over ESP-NOW (after Network: Wi-Fi first)
tesserkit::Log logs(dev);      // the firmware's log, as events and a file
// The launcher's USB way: envelopes on the console's UART, one per line, with
// Admin access (physical access). Logs stay at warnings, so that few land in
// the middle of a reply (sdkconfig.defaults).
tesser::UartTransport serialApi(dev.api(), UART_NUM_0);

UartPort tmcUart({.rx = board::kTmcRx, .tx = board::kTmcTx, .baud = 115200, .port = board::kTmcUart});
Tmc2209 driver(tmcUart, {.address = 0, .enable = board::kTmcEnable});  // 0.11 Ω
Motion motion;

fluante::Spinner spinner(driver);
fluante::Strobe strobe(tesserkit::espPwm(), board::kStrobe);
fluante::Inrush inrush;

}  // namespace

extern "C" void app_main() {
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    net.defaultNetwork(WIFI_SSID, WIFI_PASSWORD);
#endif
#if defined(WIFI_TX_POWER)
    net.txPower(WIFI_TX_POWER);
#endif
    // Updates from this repository's releases (.github/workflows/release.yml),
    // and from any URL.
    firmware.github("leokeba/fluante-firmware", "fluante-{target}.bin");
    // Writes to every Fluante nearby (/peers/*/...) each wait in a pending slot.
    dev.api().config().maxPending = 8;

    // The board's pins the libraries don't drive.
    Gpio& gpio = defaultGpio();
    gpio.output(board::kTmcMs1, false);
    gpio.output(board::kTmcMs2, false);
    gpio.output(board::kTmcStep, false);
    gpio.output(board::kTmcDir, false);
    gpio.output(board::kInrushBypass, false);  // the strobe's capacitor charges through its resistor

    // Factory defaults, until the stored settings say otherwise: the old
    // firmware's 256 microsteps and 0.95 A.
    driver.settings.microsteps = 256;
    driver.settings.runCurrent = 0.95f;
    driver.settings.holdCurrent = 0.5f;
    // stealthChop's start values, as the old firmware's preheat wrote them.
    driver.pwm.ofs = 255;
    driver.pwm.grad = 14;

    // The spinner sets the driver's velocity before it acts on it.
    motion.add(spinner);
    motion.add(driver);

    auto& api = dev.api();
    fluante::mountTree(api, strobe, spinner, driver, motion);
    static tesserui::Views views(api);
    fluante::addViews(views);

    // Nothing turns while new firmware is written.
    dev.onState([](tesserkit::State s) {
        if (s == tesserkit::State::Updating) motion.stopAll();
    });
    // The strobe's duty waiting for the end of a period, in TesserKIT's task
    // under the API lock, as its setters are; the inrush resistor bypassed
    // once the capacitor has charged.
    dev.onTick([&api](uint32_t nowMs) {
        if (inrush.update(nowMs)) defaultGpio().set(board::kInrushBypass, true);
        tesser::Lock lock(api);
        strobe.poll();
    });

    dev.cors("https://leokeba.github.io");  // the launchers (its own, TesserKIT's), reaching it by its address
    dev.begin();  // loads the stored settings
    strobe.begin();
    serialApi.begin();
    motion.start(50);
}

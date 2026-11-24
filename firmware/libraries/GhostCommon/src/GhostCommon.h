// GhostCommon: Wi-Fi setup portal and saved settings shared by both Ghost boards.
#pragma once
#include <Arduino.h>

namespace ghost {

struct Settings {
  String ssid;
  String pass;
  String room;   // listener only: "Bathroom", "Hallway"...
  String topic;  // listener only: ntfy topic for alerts
};

// Load saved settings. Returns false if no Wi-Fi has been set up yet.
bool loadSettings(Settings &s);
void saveSettings(const Settings &s);
void clearSettings();

// Start the setup hotspot ("Ghost-<role>-XXXX") with a captive page where the
// person picks their Wi-Fi from a phone. Never returns: restarts when saved.
// askRoom: also ask for the room name (listener).
[[noreturn]] void runSetupPortal(const char *role, bool askRoom);

// Join the saved Wi-Fi. Returns true when connected within timeoutMs.
bool connectWiFi(const Settings &s, uint32_t timeoutMs);

// Holding the BOOT button for 3 seconds clears Wi-Fi and restarts into setup.
// Call often from loop().
void checkResetButton();

// Onboard LED helpers (GPIO 2 on most ESP32 dev boards).
void ledInit();
void ledSet(bool on);

// Random hex string of n bytes (2n chars).
String randomHex(int bytes);

}  // namespace ghost

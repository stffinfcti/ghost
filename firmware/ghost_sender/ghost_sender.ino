// Ghost sender (board A).
// Joins the home Wi-Fi so it sits on the same channel as the listener, then
// broadcasts 100 tiny ESP-NOW packets per second at an OFDM rate. The listener
// reads how each packet's Wi-Fi channel looks (CSI) to sense people moving.
//
// Light: slow blink = setup hotspot is open; fast blink = joining Wi-Fi;
// short flicker = sending. Hold BOOT for 3 s to redo Wi-Fi setup.

#include <GhostCommon.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

static const uint32_t SEND_INTERVAL_US = 10000;  // 100 packets per second
static const uint8_t BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct __attribute__((packed)) GhostPacket {
  char magic[4];  // "GHST"
  uint32_t seq;
  uint8_t pad[16];
};

static GhostPacket pkt;
static uint32_t lastSend = 0;
static uint32_t sentOk = 0;

void setup() {
  Serial.begin(115200);
  ghost::ledInit();
  delay(200);
  Serial.println("\n[ghost-sender] starting");

  ghost::Settings s;
  if (!ghost::loadSettings(s)) {
    Serial.println("[ghost-sender] no Wi-Fi saved, opening setup hotspot");
    ghost::runSetupPortal("Sender", false);
  }
  if (!ghost::connectWiFi(s, 20000)) {
    Serial.println("[ghost-sender] could not join Wi-Fi, opening setup hotspot");
    ghost::runSetupPortal("Sender", false);
  }
  Serial.printf("[ghost-sender] joined %s on channel %d\n", s.ssid.c_str(), WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ghost-sender] ESP-NOW failed, restarting");
    delay(1000);
    ESP.restart();
  }
  // OFDM rate so every packet carries the training fields CSI is read from.
  esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_MCS0_LGI);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = 0;  // current channel
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  memcpy(pkt.magic, "GHST", 4);
  memset(pkt.pad, 0, sizeof(pkt.pad));
  Serial.printf("[ghost-sender] sending as %s\n", WiFi.macAddress().c_str());
}

void loop() {
  ghost::checkResetButton();

  uint32_t now = micros();
  if (now - lastSend >= SEND_INTERVAL_US) {
    lastSend = now;
    pkt.seq++;
    if (esp_now_send(BROADCAST, (const uint8_t *)&pkt, sizeof(pkt)) == ESP_OK) sentOk++;
    ghost::ledSet((pkt.seq / 25) % 8 != 0);  // mostly on, short flicker
  }

  static uint32_t lastLog = 0;
  if (millis() - lastLog > 5000) {
    lastLog = millis();
    Serial.printf("[ghost-sender] sent %lu packets, wifi %s\n", (unsigned long)sentOk,
                  WiFi.status() == WL_CONNECTED ? "ok" : "lost");
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  }
}

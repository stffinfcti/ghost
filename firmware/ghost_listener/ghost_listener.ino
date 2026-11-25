// Ghost listener (board B).
// Reads the Wi-Fi channel (CSI) of every packet from the Ghost sender, turns it
// into motion and presence numbers, runs the fall detector (sensing.h), serves
// the phone page at http://ghost.local, and pushes alerts through ntfy.sh.
//
// Light: slow blink = setup hotspot; fast blink = joining Wi-Fi; solid = running;
// blinking fast after start-up = no packets from the sender yet.

#include <ESPmDNS.h>
#include <GhostCommon.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "page.h"
#include "sensing.h"

using namespace ghostsense;

// ---------- settings and state ----------
static ghost::Settings settings;
static Sensor sensor;
static WebServer server(80);

struct Pkt {
  uint32_t t;
  int8_t rssi;
  uint8_t amp[NSC];
};
static QueueHandle_t pktQ;
static uint8_t txMac[6];
static volatile bool txKnown = false;
static volatile uint32_t lastTxSeen = 0, nowPackets = 0, csiPackets = 0, csiDropped = 0;
static bool promiscuousOn = false;

static const int RING = 300;  // 30 s of frames for the phone page
static Frame ring[RING];
static int ringHead = 0, ringCount = 0;

// ---------- alerts (ntfy.sh) ----------
// Let's Encrypt root (ISRG Root X1), used to check ntfy.sh's certificate.
static const char ISRG_ROOT_X1[] PROGMEM = R"PEM(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)PEM";

struct Note {
  char suffix[8];  // "" = alerts topic, "-live" = dashboard feed
  char title[64];
  char body[200];
  uint8_t priority;
  char tags[40];
};
static QueueHandle_t noteQ;

static void notifyTask(void *) {
  Note n;
  for (;;) {
    if (xQueueReceive(noteQ, &n, portMAX_DELAY) != pdTRUE) continue;
    if (WiFi.status() != WL_CONNECTED || settings.topic.length() == 0) continue;
    WiFiClientSecure tls;
    tls.setCACert(ISRG_ROOT_X1);
    HTTPClient http;
    String url = "https://ntfy.sh/" + settings.topic + n.suffix;
    if (!http.begin(tls, url)) continue;
    if (n.title[0]) http.addHeader("Title", n.title);
    if (n.tags[0]) http.addHeader("Tags", n.tags);
    http.addHeader("Priority", String(n.priority));
    int code = http.POST((uint8_t *)n.body, strlen(n.body));
    Serial.printf("[ghost] ntfy %s -> %d\n", n.suffix[0] ? "live" : "alert", code);
    http.end();
  }
}

static void sendNote(const char *suffix, const char *title, const char *body, uint8_t prio, const char *tags) {
  Note n = {};
  strlcpy(n.suffix, suffix, sizeof(n.suffix));
  strlcpy(n.title, title, sizeof(n.title));
  strlcpy(n.body, body, sizeof(n.body));
  strlcpy(n.tags, tags, sizeof(n.tags));
  n.priority = prio;
  xQueueSend(noteQ, &n, 0);
}

// ---------- saved detector settings ----------
static void saveDetector() {
  Preferences p;
  p.begin("ghostcfg", false);
  p.putBytes("cfg", &sensor.cfg, sizeof(Config));
  p.putBytes("base", sensor.base, sizeof(sensor.base));
  p.putBool("hasBase", sensor.hasBase);
  p.end();
}

static void loadDetector() {
  Preferences p;
  p.begin("ghostcfg", true);
  Config c;
  if (p.getBytesLength("cfg") == sizeof(Config)) {
    p.getBytes("cfg", &c, sizeof(Config));
    if (c.version == Config().version) sensor.cfg = c;
  }
  if (p.getBytesLength("base") == sizeof(sensor.base)) {
    p.getBytes("base", sensor.base, sizeof(sensor.base));
    sensor.hasBase = p.getBool("hasBase", false);
  }
  p.end();
  sensor.restoreFloor();
}

// ---------- Wi-Fi capture ----------
static void onEspNow(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < 8 || memcmp(data, "GHST", 4) != 0) return;
  if (!txKnown) {
    memcpy(txMac, info->src_addr, 6);
    txKnown = true;
  }
  if (memcmp(info->src_addr, txMac, 6) == 0) {
    lastTxSeen = millis();
    nowPackets++;
  }
}

static void onCsi(void *, wifi_csi_info_t *info) {
  if (!info || !info->buf || info->len < 128 || !txKnown) return;
  if (memcmp(info->mac, txMac, 6) != 0) return;
  Pkt p;
  p.t = millis();
  p.rssi = info->rx_ctrl.rssi;
  const int8_t *b = info->buf;
  int j = 0;
  // LLTF: 64 subcarriers stored as (imaginary, real). Skip DC (0) and the guard band (27..37).
  for (int k = 0; k < 64; k++) {
    if (k == 0 || (k >= 27 && k <= 37)) continue;
    int im = b[2 * k], re = b[2 * k + 1];
    float a = sqrtf((float)(im * im + re * re));
    p.amp[j++] = a > 255 ? 255 : (uint8_t)a;
  }
  if (info->first_word_invalid) p.amp[0] = p.amp[1];
  csiPackets++;
  if (xQueueSend(pktQ, &p, 0) != pdTRUE) csiDropped++;
}

static void startCapture() {
  esp_now_init();
  esp_now_register_recv_cb(onEspNow);
  wifi_csi_config_t c = {};
  c.lltf_en = true;
  c.htltf_en = false;
  c.stbc_htltf2_en = false;
  c.ltf_merge_en = false;
  c.channel_filter_en = false;
  c.manu_scale = false;
  esp_wifi_set_csi_config(&c);
  esp_wifi_set_csi_rx_cb(onCsi, nullptr);
  esp_wifi_set_csi(true);
}

// ---------- web page and API ----------
static String num(float v, int digits = 4) { return String(v, digits); }

static String stateJson() {
  String j = "{";
  j += "\"state\":\"" + String(STATE_NAMES[sensor.state]) + "\"";
  const Frame *f = ringCount ? &ring[(ringHead - 1 + RING) % RING] : nullptr;
  if (f) {
    j += ",\"motion\":" + num(f->motion) + ",\"short\":" + num(f->shortm) + ",\"dev\":" + num(f->dev);
    j += ",\"rssi\":" + String(f->rssi) + ",\"rate\":" + String(f->rate);
  }
  j += ",\"room\":\"" + settings.room + "\",\"topic\":\"" + settings.topic + "\"";
  j += ",\"senderSeen\":" + String(txKnown ? (long)(millis() - lastTxSeen) : -1L);
  j += ",\"csi\":" + String(csiPackets) + ",\"dropped\":" + String(csiDropped);
  j += ",\"calib\":" + num(sensor.calibProgress(), 2) + ",\"hasBase\":" + String(sensor.hasBase ? "true" : "false");
  static const char *const CAL_NAMES[] = {"none", "ok", "rejected"};
  j += ",\"calibResult\":\"" + String(CAL_NAMES[sensor.calibResult]) + "\"";
  // Thresholds are multiples of these learned quiet levels.
  j += ",\"floor\":" + num(sensor.floorLevel) + ",\"floorShort\":" + num(sensor.floorShort);
  const Config &c = sensor.cfg;
  j += ",\"cfg\":{\"motionTh\":" + num(c.motionTh) + ",\"devTh\":" + num(c.devTh) + ",\"spikeTh\":" + num(c.spikeTh);
  j += ",\"stillSecs\":" + num(c.stillSecs, 1) + ",\"maxBurstSecs\":" + num(c.maxBurstSecs, 1) + ",\"modelOn\":" + String(c.modelOn);
  j += ",\"holdSecs\":" + num(c.holdSecs, 1) + ",\"presFrac\":" + num(c.presFrac, 2);
  j += ",\"w\":[";
  for (int i = 0; i < NFEAT; i++) j += (i ? "," : "") + num(c.w[i]);
  j += "],\"b\":" + num(c.b) + "}";
  j += ",\"events\":[";
  for (int i = 0; i < sensor.nEvents; i++) {
    const Event &e = sensor.events[i];
    if (i) j += ",";
    j += "{\"t\":" + String(e.t) + ",\"outcome\":\"" + OUTCOME_NAMES[e.outcome] + "\",\"prob\":" + num(e.prob, 3);
    j += ",\"f\":[" + num(e.f.peak) + "," + num(e.f.dur, 2) + "," + num(e.f.preDev) + "," + num(e.f.postDev) + "," +
         num(e.f.shift) + "," + num(e.f.postMotion) + "]}";
  }
  j += "],\"uptime\":" + String(millis()) + "}";
  return j;
}

static String framesJson(uint32_t since) {
  static const char *hex = "0123456789abcdef";
  String j;
  j.reserve(16000);
  j = "{\"frames\":[";
  int sent = 0;
  int start = ringCount < RING ? 0 : ringHead;
  for (int i = 0; i < ringCount && sent < 100; i++) {
    const Frame &f = ring[(start + i) % RING];
    if (f.seq <= since) continue;
    if (sent++) j += ",";
    j += "[" + String(f.seq) + "," + String(f.t) + "," + String((int)(f.motion * 10000)) + "," +
         String((int)(f.shortm * 10000)) + "," + String((int)(f.dev * 10000)) + "," + String(f.rssi) + "," +
         String(f.rate) + "," + String(f.state) + ",\"";
    for (int k = 0; k < NSC; k++) { j += hex[f.prof[k] >> 4]; j += hex[f.prof[k] & 15]; }
    j += "\"]";
  }
  j += "]}";
  return j;
}

static void cors() { server.sendHeader("Access-Control-Allow-Origin", "*"); }

static void setupServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", PAGE_HTML); });
  server.on("/api/state", HTTP_GET, []() { cors(); server.send(200, "application/json", stateJson()); });
  server.on("/api/frames", HTTP_GET, []() {
    cors();
    server.send(200, "application/json", framesJson(server.arg("since").toInt()));
  });
  server.on("/api/calibrate", HTTP_POST, []() {
    int secs = server.hasArg("secs") ? server.arg("secs").toInt() : 20;
    sensor.startCalibration(secs < 10 ? 10 : secs);
    cors();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/config", HTTP_POST, []() {
    Config &c = sensor.cfg;
    if (server.hasArg("motionTh")) c.motionTh = server.arg("motionTh").toFloat();
    if (server.hasArg("devTh")) c.devTh = server.arg("devTh").toFloat();
    if (server.hasArg("spikeTh")) c.spikeTh = server.arg("spikeTh").toFloat();
    if (server.hasArg("stillSecs")) c.stillSecs = server.arg("stillSecs").toFloat();
    if (server.hasArg("maxBurstSecs")) c.maxBurstSecs = server.arg("maxBurstSecs").toFloat();
    if (server.hasArg("modelOn")) c.modelOn = server.arg("modelOn").toInt() ? 1 : 0;
    if (server.hasArg("holdSecs")) c.holdSecs = server.arg("holdSecs").toFloat();
    if (server.hasArg("presFrac")) c.presFrac = server.arg("presFrac").toFloat();
    for (int i = 0; i < NFEAT; i++) {
      String k = "w" + String(i);
      if (server.hasArg(k)) c.w[i] = server.arg(k).toFloat();
    }
    if (server.hasArg("b")) c.b = server.arg("b").toFloat();
    if (server.hasArg("room") && server.arg("room").length()) {
      settings.room = server.arg("room");
      ghost::saveSettings(settings);
    }
    saveDetector();
    cors();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/ack", HTTP_POST, []() {
    sensor.acknowledge();
    cors();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/test-alert", HTTP_POST, []() {
    String body = "Test alert from Ghost in the " + settings.room + ". Alerts are working.";
    sendNote("", "Ghost test", body.c_str(), 3, "white_check_mark");
    cors();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/reset-wifi", HTTP_POST, []() {
    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ghost::clearSettings();
    ESP.restart();
  });
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

// ---------- main ----------
void setup() {
  Serial.begin(115200);
  ghost::ledInit();
  delay(200);
  Serial.println("\n[ghost] listener starting");

  if (!ghost::loadSettings(settings)) ghost::runSetupPortal("Listener", true);
  if (settings.topic.length() == 0) {
    settings.topic = "ghost-" + ghost::randomHex(6);
    ghost::saveSettings(settings);
  }
  if (!ghost::connectWiFi(settings, 20000)) ghost::runSetupPortal("Listener", true);
  Serial.printf("[ghost] joined %s, channel %d, ip %s\n", settings.ssid.c_str(), WiFi.channel(),
                WiFi.localIP().toString().c_str());
  Serial.printf("[ghost] alerts topic: %s\n", settings.topic.c_str());

  loadDetector();
  pktQ = xQueueCreate(128, sizeof(Pkt));
  noteQ = xQueueCreate(8, sizeof(Note));
  xTaskCreatePinnedToCore(notifyTask, "notify", 8192, nullptr, 1, nullptr, 0);
  startCapture();

  MDNS.begin("ghost");
  MDNS.addService("http", "tcp", 80);
  setupServer();
  Serial.println("[ghost] open http://ghost.local or http://" + WiFi.localIP().toString());
}

void loop() {
  ghost::checkResetButton();
  server.handleClient();

  Pkt p;
  while (xQueueReceive(pktQ, &p, 0) == pdTRUE) sensor.addPacket(p.t, p.rssi, p.amp);

  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    uint8_t before = sensor.state;
    Frame f;
    if (sensor.frame(now, f)) {
      ring[ringHead] = f;
      ringHead = (ringHead + 1) % RING;
      if (ringCount < RING) ringCount++;
      // Serial line for laptop recording: F,seq,t,motion,short,dev,rssi,rate,state
      Serial.printf("F,%lu,%lu,%.4f,%.4f,%.4f,%d,%u,%u\n", (unsigned long)f.seq, (unsigned long)f.t, f.motion,
                    f.shortm, f.dev, f.rssi, f.rate, f.state);
    }
    bool calibrated = before != sensor.state || sensor.calibProgress() < 0;
    (void)calibrated;

    if (sensor.fallFired) {
      sensor.fallFired = false;
      String body = "Possible fall in the " + settings.room + ". Someone went down and has not moved for " +
                    String((int)sensor.cfg.stillSecs) + " seconds.";
      sendNote("", "Ghost: possible fall", body.c_str(), 5, "rotating_light");
    }
    if (sensor.gotUp) {
      sensor.gotUp = false;
      String body = "Movement again in the " + settings.room + " after the fall alert.";
      sendNote("", "Ghost: moving again", body.c_str(), 3, "walking");
    }
    // Live feed for the online dashboard: state changes, at most every 5 s (falls right away).
    static uint32_t lastLive = 0;
    static uint8_t lastLiveState = 255;
    if (sensor.state != lastLiveState &&
        (sensor.state == FALL || now - lastLive > 5000)) {
      lastLive = now;
      lastLiveState = sensor.state;
      char body[160];
      snprintf(body, sizeof(body), "{\"s\":\"%s\",\"room\":\"%s\",\"m\":%.3f,\"d\":%.3f}", STATE_NAMES[sensor.state],
               settings.room.c_str(), f.motion, f.dev);
      sendNote("-live", "", body, 1, "");
    }
    if (sensor.calibProgress() < 0) {
      static bool wasCalibrating = false;
      (void)wasCalibrating;
    }
  }

  // Save new thresholds once a calibration finishes.
  static bool calibrating = false;
  bool nowCal = sensor.calibProgress() >= 0;
  if (calibrating && !nowCal) saveDetector();
  calibrating = nowCal;

  // If ESP-NOW packets arrive but no CSI does, listen to all frames instead.
  static uint32_t lastCheck = 0, lastCsi = 0, lastNow = 0;
  if (now - lastCheck > 3000) {
    lastCheck = now;
    if (nowPackets - lastNow > 50 && csiPackets == lastCsi && !promiscuousOn) {
      esp_wifi_set_promiscuous(true);
      promiscuousOn = true;
      Serial.println("[ghost] no CSI from ESP-NOW frames, switched to listening to all frames");
    }
    lastCsi = csiPackets;
    lastNow = nowPackets;
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  }

  // Light: solid when packets flow, fast blink when the sender is missing.
  bool flowing = txKnown && now - lastTxSeen < 2000;
  ghost::ledSet(flowing ? true : (now / 150) % 2);
}

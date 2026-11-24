#include "GhostCommon.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

namespace ghost {

static const int LED_PIN = 2;
static const int BOOT_PIN = 0;

void ledInit() { pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW); }
void ledSet(bool on) { digitalWrite(LED_PIN, on ? HIGH : LOW); }

String randomHex(int bytes) {
  static const char *hex = "0123456789abcdef";
  String out;
  for (int i = 0; i < bytes; i++) {
    uint8_t b = esp_random() & 0xff;
    out += hex[b >> 4];
    out += hex[b & 0xf];
  }
  return out;
}

bool loadSettings(Settings &s) {
  Preferences p;
  p.begin("ghost", true);
  s.ssid = p.getString("ssid", "");
  s.pass = p.getString("pass", "");
  s.room = p.getString("room", "Room");
  s.topic = p.getString("topic", "");
  p.end();
  return s.ssid.length() > 0;
}

void saveSettings(const Settings &s) {
  Preferences p;
  p.begin("ghost", false);
  p.putString("ssid", s.ssid);
  p.putString("pass", s.pass);
  p.putString("room", s.room);
  p.putString("topic", s.topic);
  p.end();
}

void clearSettings() {
  Preferences p;
  p.begin("ghost", false);
  p.remove("ssid");
  p.remove("pass");
  p.end();
}

bool connectWiFi(const Settings &s, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // keep the radio awake so CSI keeps flowing
  WiFi.begin(s.ssid.c_str(), s.pass.c_str());
  uint32_t start = millis();
  bool led = false;
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    led = !led;
    ledSet(led);
    delay(250);
    checkResetButton();
  }
  ledSet(WiFi.status() == WL_CONNECTED);
  return WiFi.status() == WL_CONNECTED;
}

void checkResetButton() {
  static uint32_t downSince = 0;
  pinMode(BOOT_PIN, INPUT_PULLUP);
  if (digitalRead(BOOT_PIN) == LOW) {
    if (downSince == 0) downSince = millis();
    if (millis() - downSince > 3000) {
      clearSettings();
      for (int i = 0; i < 10; i++) { ledSet(i % 2); delay(80); }
      ESP.restart();
    }
  } else {
    downSince = 0;
  }
}

// ---------------- setup portal ----------------

static String htmlEscape(const String &in) {
  String o;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static const char PORTAL_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Ghost setup</title>
<style>
body{font-family:-apple-system,system-ui,sans-serif;background:#0b0d12;color:#e8eaf0;margin:0;padding:24px}
h1{font-size:22px;margin:0 0 4px} p{color:#9aa3b2;margin:0 0 18px;font-size:15px}
label{display:block;font-size:13px;color:#9aa3b2;margin:14px 0 6px}
select,input{width:100%;box-sizing:border-box;padding:13px;border-radius:10px;border:1px solid #2a3140;background:#141821;color:#e8eaf0;font-size:16px}
button{margin-top:22px;width:100%;padding:15px;border:0;border-radius:12px;background:#7c5cff;color:#fff;font-size:17px;font-weight:600}
.note{font-size:13px;color:#6f7889;margin-top:16px}
</style></head><body>)HTML";

[[noreturn]] void runSetupPortal(const char *role, bool askRoom) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  String options;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    if (options.indexOf(">" + htmlEscape(ssid) + "<") >= 0) continue;  // skip duplicates
    options += "<option>" + htmlEscape(ssid) + "</option>";
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char apName[40];
  snprintf(apName, sizeof(apName), "Ghost-%s-%02X%02X", role, mac[4], mac[5]);
  IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName);

  DNSServer dns;
  dns.start(53, "*", ip);
  WebServer server(80);

  Settings cur;
  loadSettings(cur);

  server.on("/", HTTP_GET, [&]() {
    String page = FPSTR(PORTAL_HEAD);
    page += "<h1>Set up Ghost ";
    page += role;
    page += "</h1><p>Pick your home Wi-Fi. Ghost only works on 2.4 GHz networks.</p>";
    page += "<form method='POST' action='/save'><label>Wi-Fi network</label><select name='ssid'>";
    page += options;
    page += "</select><label>Or type the network name</label><input name='ssid2' placeholder='Only if it is not in the list'>";
    page += "<label>Wi-Fi password</label><input name='pass' type='password'>";
    if (askRoom) {
      page += "<label>Room this board watches</label><input name='room' value='" + htmlEscape(cur.room) + "'>";
    }
    page += "<button type='submit'>Save and connect</button></form>";
    page += "<p class='note'>Both Ghost boards must join the same Wi-Fi. Hold the BOOT button for 3 seconds to come back here.</p></body></html>";
    server.send(200, "text/html", page);
  });

  server.on("/save", HTTP_POST, [&]() {
    Settings s = cur;
    s.ssid = server.arg("ssid2").length() ? server.arg("ssid2") : server.arg("ssid");
    s.pass = server.arg("pass");
    if (askRoom && server.arg("room").length()) s.room = server.arg("room");
    saveSettings(s);
    String page = FPSTR(PORTAL_HEAD);
    page += "<h1>Saved</h1><p>Ghost is restarting and joining <b>" + htmlEscape(s.ssid) +
            "</b>. The light turns solid when it is connected. You can close this page.</p></body></html>";
    server.send(200, "text/html", page);
    delay(1500);
    ESP.restart();
  });

  // Captive portal: send every other URL to the setup page.
  server.onNotFound([&]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  uint32_t lastBlink = 0;
  bool led = false;
  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    if (millis() - lastBlink > 1000) {  // slow blink = setup mode
      led = !led;
      ledSet(led);
      lastBlink = millis();
    }
    delay(2);
  }
}

}  // namespace ghost

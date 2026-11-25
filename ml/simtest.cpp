// Scenario tests for the board's fall state machine (firmware/ghost_listener/sensing.h)
// using made-up Wi-Fi signals. Checks the logic, not the physics: real signals come
// from the boards. Build: g++ -O2 -std=c++17 -I../firmware/ghost_listener simtest.cpp -o simtest
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "sensing.h"

using namespace ghostsense;

struct Seg { float secs; float motion; float personShape; };  // motion: fluctuation size, shape: how much the person bends the room

static std::mt19937 rng(1);

// Runs one scenario and returns the outcomes of the events it produced, plus "FALL!" if an
// alert fired, "UP" if the alert cleared, "cal=ok"/"cal=rejected", "end=<state>" and
// "baddur" if an event's duration came out impossible.
// calibrateFirst: 0 = no calibration, 1 = calibrate an empty room, 2 = calibrate while
// someone walks around (a mistake people make).
static std::string run(const std::vector<Seg> &segs, int calibrateFirst = 1) {
  static Sensor s;
  s = Sensor();
  float room[NSC], person[NSC];
  std::normal_distribution<float> n01(0, 1);
  for (int k = 0; k < NSC; k++) { room[k] = 60 + 15 * sinf(k * 0.37f); person[k] = 10 * sinf(k * 0.9f + 1); }
  std::vector<Seg> all;
  if (calibrateFirst == 1) all.push_back({32, 0.4f, 0});  // empty room while calibrating
  if (calibrateFirst == 2) {  // someone walks around for part of the calibration
    all.push_back({12, 0.4f, 0});
    all.push_back({8, 6, 0.5f});
    all.push_back({12, 0.4f, 0});
  }
  all.insert(all.end(), segs.begin(), segs.end());
  uint32_t t = 0, nextFrame = FRAME_MS;
  bool startedCal = false;
  std::string out;
  for (const Seg &g : all) {
    for (int i = 0; i < (int)(g.secs * 100); i++, t += 10) {
      if (calibrateFirst && !startedCal && t >= 1000) { s.startCalibration(30); startedCal = true; }
      uint8_t amp[NSC];
      float gain = 1 + 0.05f * n01(rng);  // automatic gain jumps, which the sensor must ignore
      for (int k = 0; k < NSC; k++) {
        float v = (room[k] + g.personShape * person[k] + g.motion * 4 * n01(rng)) * gain;
        amp[k] = (uint8_t)std::max(0.f, std::min(255.f, v));
      }
      s.addPacket(t, -50, amp);
      while (t >= nextFrame) {
        Frame f;
        s.frame(nextFrame, f);
        nextFrame += FRAME_MS;
        if (s.fallFired) { out += "FALL! "; s.fallFired = false; }
        if (s.gotUp) { out += "UP "; s.gotUp = false; }
      }
    }
  }
  for (int i = 0; i < s.nEvents; i++) {
    out += std::string(OUTCOME_NAMES[s.events[i].outcome]) + " ";
    if (s.events[i].f.dur < 0 || s.events[i].f.dur > 600) out += "baddur ";
  }
  if (s.calibResult == CAL_OK) out += "cal=ok ";
  if (s.calibResult == CAL_REJECTED) out += "cal=rejected ";
  out += std::string("end=") + STATE_NAMES[s.state];
  return out;
}

int main() {
  // expect: words that must all appear in the result. forbid: words that must not.
  struct Case { const char *name; std::vector<Seg> segs; const char *expect; const char *forbid; int calib; };
  // Already sitting at a desk, typing: small fidgets every few seconds. In the first real
  // room test the old code took one of these for a fall burst and raised an alert.
  std::vector<Seg> typing;
  for (int i = 0; i < 20; i++) { typing.push_back({2.7f, 0.45f, 0}); typing.push_back({0.3f, 1.0f, 0}); }
  std::vector<Case> cases = {
    {"empty room, nothing happens", {{30, 0.4f, 0}}, "end=empty", "FALL! moved_again", 1},
    {"walk in, fall, lie still 15 s", {{4, 6, 0.5f}, {1, 2.5f, 0.8f}, {15, 0.6f, 1.0f}}, "FALL!", "", 1},
    {"walk in, squat, stand back up and walk", {{4, 6, 0.5f}, {1, 2.5f, 0.6f}, {3, 0.6f, 0.6f}, {4, 6, 0.5f}}, "moved_again", "FALL!", 1},
    {"walk through and out", {{3, 6, 0.5f}, {10, 0.4f, 0}}, "", "FALL! moved_again", 1},
    {"fall, then get up after 20 s: the alert clears", {{4, 6, 0.5f}, {1, 2.5f, 0.8f}, {20, 0.6f, 1.0f}, {5, 6, 0.5f}}, "FALL! UP end=moving", "", 1},
    {"sit down and stay still 15 s (expected false alarm without a model)", {{4, 6, 0.5f}, {1, 2.5f, 0.7f}, {15, 0.6f, 0.9f}}, "FALL!", "", 1},
    // Regressions from the first real-room test (Nov 2026):
    {"typing at a desk for a minute: no fall check starts", typing, "", "FALL! fall moved_again left", 1},
    {"walk in, sit still out of the boards' view: still counted as here", {{4, 6, 0.5f}, {20, 0.4f, 0}}, "end=still", "FALL!", 1},
    {"... and 40 s later with no movement: empty", {{4, 6, 0.5f}, {40, 0.4f, 0}}, "end=empty", "FALL!", 1},
    {"calibrating while someone walks around is refused", {{10, 0.4f, 0}}, "cal=rejected", "cal=ok", 2},
    {"a tiny 0.3 s blip gives a sane event duration", {{0.3f, 3, 0.3f}, {15, 0.4f, 0}}, "", "FALL! baddur", 1},
  };
  auto words = [](const char *s) {
    std::vector<std::string> w; std::string cur;
    for (const char *p = s;; p++) {
      if (*p == ' ' || !*p) { if (!cur.empty()) w.push_back(cur); cur.clear(); if (!*p) break; }
      else cur += *p;
    }
    return w;
  };
  int bad = 0;
  for (auto &c : cases) {
    std::string got = run(c.segs, c.calib);
    std::string padded = " " + got + " ";
    bool ok = true;
    for (auto &w : words(c.expect)) ok &= padded.find(" " + w + " ") != std::string::npos;
    for (auto &w : words(c.forbid)) ok &= padded.find(" " + w + " ") == std::string::npos;
    ok &= padded.find(" baddur ") == std::string::npos;
    printf("%s  %-70s -> %s\n", ok ? "PASS" : "FAIL", c.name, got.c_str());
    bad += !ok;
  }
  printf("%d of %zu scenarios passed\n", (int)cases.size() - bad, cases.size());
  return bad ? 1 : 0;
}

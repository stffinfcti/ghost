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

// Runs one scenario and returns the outcomes of the events it produced, plus "FALL!" if an alert fired.
static std::string run(const std::vector<Seg> &segs, bool calibrateFirst = true) {
  static Sensor s;
  s = Sensor();
  float room[NSC], person[NSC];
  std::normal_distribution<float> n01(0, 1);
  for (int k = 0; k < NSC; k++) { room[k] = 60 + 15 * sinf(k * 0.37f); person[k] = 10 * sinf(k * 0.9f + 1); }
  std::vector<Seg> all;
  if (calibrateFirst) all.push_back({32, 0.4f, 0});  // empty room while calibrating
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
      }
    }
  }
  for (int i = 0; i < s.nEvents; i++) out += std::string(OUTCOME_NAMES[s.events[i].outcome]) + " ";
  return out;
}

int main() {
  struct Case { const char *name; std::vector<Seg> segs; const char *expect; };
  std::vector<Case> cases = {
    {"empty room, nothing happens", {{30, 0.4f, 0}}, ""},
    {"walk in, fall, lie still 15 s", {{4, 6, 0.5f}, {1, 2.5f, 0.8f}, {15, 0.6f, 1.0f}}, "FALL!"},
    {"walk in, squat, stand back up and walk", {{4, 6, 0.5f}, {1, 2.5f, 0.6f}, {3, 0.6f, 0.6f}, {4, 6, 0.5f}}, "moved_again"},
    {"walk through and out", {{3, 6, 0.5f}, {10, 0.4f, 0}}, ""},
    {"fall, then get up after 20 s", {{4, 6, 0.5f}, {1, 2.5f, 0.8f}, {20, 0.6f, 1.0f}, {5, 6, 0.5f}}, "FALL!"},
    {"sit down and stay still 15 s (expected false alarm without a model)", {{4, 6, 0.5f}, {1, 2.5f, 0.7f}, {15, 0.6f, 0.9f}}, "FALL!"},
  };
  int bad = 0;
  for (auto &c : cases) {
    std::string got = run(c.segs);
    bool ok = std::string(c.expect).empty() ? got.find("FALL!") == std::string::npos && got.find("moved_again") == std::string::npos
                                            : got.find(c.expect) != std::string::npos;
    printf("%s  %-70s -> %s\n", ok ? "PASS" : "FAIL", c.name, got.empty() ? "(no events)" : got.c_str());
    bad += !ok;
  }
  printf("%d of %zu scenarios passed\n", (int)cases.size() - bad, cases.size());
  return bad ? 1 : 0;
}

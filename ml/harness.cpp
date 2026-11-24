// Desktop test harness: runs the exact sensing code from the listener board
// (firmware/ghost_listener/sensing.h) on recorded CSI amplitudes.
//
// Build: g++ -O2 -std=c++17 -I../firmware/ghost_listener harness.cpp -o harness
// Run:   ./harness <file.f32> <packets> <ms_per_packet> [motionTh spikeTh stillSecs maxBurstSecs]
// Input: packets x 52 float32 amplitudes. Output on stdout:
//   F,t,motion,shortm,dev,state            one line per 100 ms frame
//   E,t,outcome,peak,dur,preDev,postDev,shift,postMotion   one line per finished event
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "sensing.h"

using namespace ghostsense;

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: harness file packets ms_per_packet [motionTh spikeTh stillSecs maxBurstSecs]\n"); return 2; }
  int packets = atoi(argv[2]);
  double msPer = atof(argv[3]);
  std::vector<float> data((size_t)packets * NSC);
  FILE *f = fopen(argv[1], "rb");
  if (!f || fread(data.data(), sizeof(float), data.size(), f) != data.size()) { fprintf(stderr, "read failed\n"); return 1; }
  fclose(f);

  static Sensor s;  // big buffers, keep off the stack
  if (argc >= 8) {
    s.cfg.motionTh = atof(argv[4]);
    s.cfg.spikeTh = atof(argv[5]);
    s.cfg.stillSecs = atof(argv[6]);
    s.cfg.maxBurstSecs = atof(argv[7]);
  }
  int seen = 0;
  uint32_t nextFrame = FRAME_MS;
  for (int i = 0; i < packets; i++) {
    uint32_t t = (uint32_t)(i * msPer);
    while (t >= nextFrame) {
      Frame fr;
      s.frame(nextFrame, fr);
      printf("F,%u,%.5f,%.5f,%.5f,%d\n", fr.t, fr.motion, fr.shortm, fr.dev, fr.state);
      for (; seen < (int)s.eventsTotal; seen++) {
        const Event &e = s.events[s.nEvents - ((int)s.eventsTotal - seen)];
        printf("E,%u,%s,%.5f,%.3f,%.5f,%.5f,%.5f,%.5f\n", e.t, OUTCOME_NAMES[e.outcome], e.f.peak, e.f.dur,
               e.f.preDev, e.f.postDev, e.f.shift, e.f.postMotion);
      }
      nextFrame += FRAME_MS;
    }
    uint8_t amp[NSC];
    for (int k = 0; k < NSC; k++) {
      float v = data[(size_t)i * NSC + k];
      amp[k] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f));
    }
    s.addPacket(t, -50, amp);
  }
  return 0;
}

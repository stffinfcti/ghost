// Ghost sensing core: turns Wi-Fi CSI amplitudes into motion, presence and a
// fall decision. Plain C++ with no Arduino calls, so the exact same code runs on
// the board and in the desktop test harness (ml/harness.cpp).
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace ghostsense {

constexpr int NSC = 52;        // usable subcarriers in the LLTF part of CSI
constexpr int WIN = 100;       // packets kept, about 1 s at 100 packets/s
constexpr int SHORT_WIN = 20;  // about 0.2 s, catches sudden bursts
constexpr int PROF_WIN = 50;   // packets averaged into the "shape" of the room
constexpr uint32_t FRAME_MS = 100;
constexpr int NFEAT = 6;
constexpr int NEVENTS = 16;

enum State : uint8_t { NO_SIGNAL = 0, EMPTY = 1, STILL = 2, MOVING = 3, CHECKING = 4, FALL = 5 };
static const char *const STATE_NAMES[] = {"no_signal", "empty", "still", "moving", "checking", "fall"};

enum Outcome : uint8_t { EV_WALKING = 0, EV_MOVED_AGAIN = 1, EV_LEFT = 2, EV_MODEL_SAFE = 3, EV_FALL = 4 };
static const char *const OUTCOME_NAMES[] = {"walking", "moved_again", "left", "model_safe", "fall"};

struct Config {
  uint32_t version = 2;
  float motionTh = 0.05f;    // motion level that counts as someone moving
  float devTh = 0.02f;       // change from the empty-room shape that counts as someone there
  float spikeTh = 0.12f;     // a sudden burst this big might be a fall
  float stillSecs = 10.0f;   // still on the floor this long after a burst -> alert
  float maxBurstSecs = 6.0f; // bursts longer than this are walking around, not a fall
  uint8_t modelOn = 0;       // use the trained classifier to veto sit-downs
  float w[NFEAT] = {0, 0, 0, 0, 0, 0};
  float b = 0;
};

// Numbers that describe one burst-then-still event. The classifier reads these.
struct EventFeat {
  float peak;        // biggest short-window motion in the burst
  float dur;         // seconds the burst lasted
  float preDev;      // how different from the empty room before the burst
  float postDev;     // ... and after it
  float shift;       // how much the room's signal shape changed across the burst
  float postMotion;  // leftover motion after the burst (breathing, shifting)
};

struct Event {
  uint32_t t;
  EventFeat f;
  float prob;  // classifier fall probability, or -1 when the model is off
  uint8_t outcome;
};

struct Frame {
  uint32_t seq;
  uint32_t t;
  float motion;
  float shortm;
  float dev;
  int8_t rssi;
  uint8_t rate;
  uint8_t state;
  uint8_t prof[NSC];  // room shape, x100
};

inline float pearson(const float *a, const float *b, int n) {
  float ma = 0, mb = 0;
  for (int i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
  ma /= n; mb /= n;
  float sab = 0, saa = 0, sbb = 0;
  for (int i = 0; i < n; i++) {
    float da = a[i] - ma, db = b[i] - mb;
    sab += da * db; saa += da * da; sbb += db * db;
  }
  if (saa <= 1e-12f || sbb <= 1e-12f) return 1.0f;
  return sab / sqrtf(saa * sbb);
}

inline void featVector(const EventFeat &f, float x[NFEAT]) {
  x[0] = f.peak; x[1] = f.dur; x[2] = f.preDev; x[3] = f.postDev; x[4] = f.shift; x[5] = f.postMotion;
}

class Sensor {
 public:
  Config cfg;
  float base[NSC];
  bool hasBase = false;

  // Outputs the caller reads after frame():
  uint8_t state = NO_SIGNAL;
  bool fallFired = false;  // set once when a fall alert should go out
  bool gotUp = false;      // set once when someone gets up after an alert
  Event events[NEVENTS];
  int nEvents = 0;
  uint32_t eventsTotal = 0;

  // Calibration progress, 0..1, or -1 when not calibrating.
  float calibProgress() const {
    if (!calibrating) return -1;
    return calibDone / (float)(calibFrames * 2);
  }

  Sensor() { memset(base, 0, sizeof(base)); reset(); }

  void reset() {
    head = 0; count = 0; lastPktT = 0; seq = 0;
    phase = IDLE; state = NO_SIGNAL; movingRun = 0; calmRun = 0;
    calmInit = false; calmDev = 0;
    for (int k = 0; k < NSC; k++) calmProf[k] = 1;
  }

  void addPacket(uint32_t tMs, int8_t rssi, const uint8_t amp[NSC]) {
    float mean = 0;
    for (int k = 0; k < NSC; k++) mean += amp[k];
    mean /= NSC;
    if (mean < 1.0f) return;  // empty or broken packet
    // Divide by the packet's own average so automatic gain changes cancel out.
    float *row = win[head];
    for (int k = 0; k < NSC; k++) row[k] = amp[k] / mean;
    tWin[head] = tMs;
    rssiWin[head] = rssi;
    head = (head + 1) % WIN;
    if (count < WIN) count++;
    lastPktT = tMs;
  }

  void startCalibration(uint32_t secs) {
    calibFrames = secs * 1000 / FRAME_MS / 2;  // first half: room shape, second half: noise levels
    if (calibFrames < 20) calibFrames = 20;
    calibDone = 0;
    calibrating = true;
    memset(calibSum, 0, sizeof(calibSum));
    sm = ssm = ss = sss = sd = ssd = 0;
  }

  void acknowledge() {
    if (state == FALL) state = STILL;
  }

  // Call every FRAME_MS. Fills out and returns true when a frame was made.
  bool frame(uint32_t tMs, Frame &out) {
    int n = count;
    out.seq = ++seq;
    out.t = tMs;
    if (n < SHORT_WIN || tMs - lastPktT > 2000) {
      state = NO_SIGNAL;
      phase = IDLE;
      out.motion = out.shortm = out.dev = 0;
      out.rssi = 0; out.rate = 0; out.state = state;
      memset(out.prof, 0, NSC);
      return true;
    }
    float motion = meanStd(n < WIN ? n : WIN);
    float shortm = meanStd(SHORT_WIN);
    float prof[NSC];
    profile(n < PROF_WIN ? n : PROF_WIN, prof);
    float dev = hasBase ? 1.0f - pearson(prof, base, NSC) : 0.0f;
    if (dev < 0) dev = 0;

    // packets in the last second and average signal strength
    int rate = 0, rs = 0;
    for (int i = 0; i < n; i++) {
      if (tMs - tWin[i] <= 1000) { rate++; rs += rssiWin[i]; }
    }

    if (calibrating) calibStep(prof, motion, shortm);
    else step(tMs, motion, shortm, dev, prof);

    out.motion = motion;
    out.shortm = shortm;
    out.dev = dev;
    out.rssi = rate ? (int8_t)(rs / rate) : 0;
    out.rate = (uint8_t)(rate > 255 ? 255 : rate);
    out.state = state;
    for (int k = 0; k < NSC; k++) {
      float v = prof[k] * 100.0f;
      out.prof[k] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    return true;
  }

  float fallProbability(const EventFeat &f) const {
    float x[NFEAT];
    featVector(f, x);
    float z = cfg.b;
    for (int i = 0; i < NFEAT; i++) z += cfg.w[i] * x[i];
    return 1.0f / (1.0f + expf(-z));
  }

 private:
  enum Phase : uint8_t { IDLE, BURST, CHECK };
  float win[WIN][NSC];
  uint32_t tWin[WIN];
  int8_t rssiWin[WIN];
  int head = 0, count = 0;
  uint32_t lastPktT = 0, seq = 0;

  Phase phase = IDLE;
  int movingRun = 0, calmRun = 0, absentRun = 0;
  uint32_t evStart = 0, burstEnd = 0, checkStart = 0;
  float evPeak = 0, preDev = 0;
  float preProf[NSC], postProfSum[NSC];
  float postDevSum = 0, postMotionSum = 0;
  int postN = 0;
  bool scored = false;
  float evProb = -1;
  EventFeat evFeat;

  bool calmInit = false;
  float calmProf[NSC];
  float calmDev = 0;

  bool calibrating = false;
  uint32_t calibFrames = 0, calibDone = 0;
  float calibSum[NSC];
  double sm, ssm, ss, sss, sd, ssd;

  // Average over subcarriers of each subcarrier's spread over the last m packets.
  float meanStd(int m) const {
    if (m > count) m = count;
    if (m < 2) return 0;
    float total = 0;
    for (int k = 0; k < NSC; k++) {
      float s = 0, s2 = 0;
      for (int i = 0; i < m; i++) {
        float v = win[(head - 1 - i + WIN) % WIN][k];
        s += v; s2 += v * v;
      }
      float mean = s / m;
      float var = s2 / m - mean * mean;
      total += var > 0 ? sqrtf(var) : 0;
    }
    return total / NSC;
  }

  void profile(int m, float out[NSC]) const {
    for (int k = 0; k < NSC; k++) out[k] = 0;
    for (int i = 0; i < m; i++) {
      const float *row = win[(head - 1 - i + WIN) % WIN];
      for (int k = 0; k < NSC; k++) out[k] += row[k];
    }
    for (int k = 0; k < NSC; k++) out[k] /= (m > 0 ? m : 1);
  }

  void calibStep(const float prof[NSC], float motion, float shortm) {
    state = EMPTY;
    if (calibDone < calibFrames) {
      for (int k = 0; k < NSC; k++) calibSum[k] += prof[k];
      calibDone++;
      if (calibDone == calibFrames) {
        for (int k = 0; k < NSC; k++) base[k] = calibSum[k] / calibFrames;
        hasBase = true;
      }
      return;
    }
    float dev = 1.0f - pearson(prof, base, NSC);
    if (dev < 0) dev = 0;
    sm += motion; ssm += motion * motion;
    ss += shortm; sss += shortm * shortm;
    sd += dev; ssd += dev * dev;
    calibDone++;
    if (calibDone >= calibFrames * 2) {
      double N = calibFrames;
      double mM = sm / N, sdM = sqrt(fmax(0.0, ssm / N - mM * mM));
      double mS = ss / N, sdS = sqrt(fmax(0.0, sss / N - mS * mS));
      double mD = sd / N, sdD = sqrt(fmax(0.0, ssd / N - mD * mD));
      // Starting thresholds from the empty room's own noise. Tuned later on real recordings.
      cfg.motionTh = (float)fmax(fmax(mM * 2.0, mM + 6 * sdM), 0.01);
      cfg.devTh = (float)fmax(fmax(mD * 3.0, mD + 6 * sdD), 0.004);
      cfg.spikeTh = (float)fmax(cfg.motionTh * 2.5, mS + 12 * sdS);
      calibrating = false;
      phase = IDLE;
    }
  }

  void logEvent(uint32_t t, uint8_t outcome) {
    Event &e = events[nEvents < NEVENTS ? nEvents : NEVENTS - 1];
    if (nEvents >= NEVENTS) memmove(events, events + 1, sizeof(Event) * (NEVENTS - 1));
    else nEvents++;
    e.t = t;
    e.f = evFeat;
    e.prob = evProb;
    e.outcome = outcome;
    eventsTotal++;
  }

  void step(uint32_t t, float motion, float shortm, float dev, const float prof[NSC]) {
    bool moving = motion >= cfg.motionTh;
    bool present = hasBase ? dev >= cfg.devTh : true;
    bool spike = shortm >= cfg.spikeTh;
    movingRun = moving ? movingRun + 1 : 0;
    calmRun = moving ? 0 : calmRun + 1;

    if (!moving) {  // remember what the room looked like while calm
      if (!calmInit) { memcpy(calmProf, prof, sizeof(calmProf)); calmDev = dev; calmInit = true; }
      for (int k = 0; k < NSC; k++) calmProf[k] = 0.9f * calmProf[k] + 0.1f * prof[k];
      calmDev = 0.9f * calmDev + 0.1f * dev;
    }

    if (state == FALL) {
      if (movingRun >= 15) { state = MOVING; gotUp = true; }  // 1.5 s of movement: they are up
      return;
    }

    switch (phase) {
      case IDLE:
        if (spike) {
          phase = BURST;
          evStart = t;
          evPeak = shortm;
          preDev = calmDev;
          memcpy(preProf, calmProf, sizeof(preProf));
          evProb = -1;
        }
        break;
      case BURST:
        if (shortm > evPeak) evPeak = shortm;
        if (t - evStart > (uint32_t)(cfg.maxBurstSecs * 1000)) {
          evFeat = {evPeak, (t - evStart) / 1000.0f, preDev, dev, 0, motion};
          logEvent(t, EV_WALKING);
          phase = IDLE;
        } else if (calmRun >= 10) {  // a full second of calm ends the burst
          phase = CHECK;
          burstEnd = t - 1000;
          checkStart = t;
          postN = 0; postDevSum = 0; postMotionSum = 0; absentRun = 0;
          memset(postProfSum, 0, sizeof(postProfSum));
          scored = false;
        }
        break;
      case CHECK: {
        if (movingRun >= 5) {  // half a second of movement: they got up or kept going
          evFeat = makeFeat();
          logEvent(t, EV_MOVED_AGAIN);
          phase = IDLE;
          break;
        }
        if (t - checkStart <= 2000) {
          postN++; postDevSum += dev; postMotionSum += motion;
          for (int k = 0; k < NSC; k++) postProfSum[k] += prof[k];
        } else if (!scored) {
          scored = true;
          evFeat = makeFeat();
          evProb = cfg.modelOn ? fallProbability(evFeat) : -1;
          if (cfg.modelOn && evProb < 0.5f) {  // looks like sitting or lying down on purpose
            logEvent(t, EV_MODEL_SAFE);
            phase = IDLE;
            break;
          }
        }
        absentRun = present ? 0 : absentRun + 1;
        if (absentRun >= 30) {  // 3 s with nobody there: they left
          evFeat = makeFeat();
          logEvent(t, EV_LEFT);
          phase = IDLE;
          break;
        }
        if (t - checkStart >= (uint32_t)(cfg.stillSecs * 1000)) {
          if (!scored) evFeat = makeFeat();
          logEvent(t, EV_FALL);
          state = FALL;
          fallFired = true;
          phase = IDLE;
          return;
        }
        break;
      }
    }

    if (phase == CHECK) state = CHECKING;
    else if (moving) state = MOVING;
    else state = present ? STILL : EMPTY;
  }

  EventFeat makeFeat() const {
    EventFeat f;
    f.peak = evPeak;
    f.dur = (burstEnd - evStart) / 1000.0f;
    f.preDev = preDev;
    int n = postN > 0 ? postN : 1;
    f.postDev = postDevSum / n;
    f.postMotion = postMotionSum / n;
    float post[NSC];
    for (int k = 0; k < NSC; k++) post[k] = postN > 0 ? postProfSum[k] / postN : preProf[k];
    f.shift = 1.0f - pearson(preProf, post, NSC);
    return f;
  }
};

}  // namespace ghostsense

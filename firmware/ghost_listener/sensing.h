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
constexpr int SHORT_WIN = 20;  // minimum packets before frames are made
constexpr int PROF_WIN = 50;   // packets averaged into the "shape" of the room
constexpr uint32_t FRAME_MS = 100;
// Motion is measured on the room shape (already averaged over PROF_WIN packets), not on
// single packets: packet-to-packet noise is as big as a person walking, the averaged
// shape is not. Recorded in a real bedroom (Nov 2026): walking moved it 2-3x the empty
// level, while the old per-packet measure read the same for empty, typing and walking.
constexpr int MOT_FRAMES = 20;    // 2 s of frames for "moving"
constexpr int SPIKE_FRAMES = 10;  // 1 s of frames for sudden bursts
constexpr int PRES_FRAMES = 100;  // 10 s of frames for "someone fidgeting here"
constexpr int CAL_MAX = 600;      // frames kept while calibrating (60 s)
constexpr int NFEAT = 6;
constexpr int NEVENTS = 16;

enum State : uint8_t { NO_SIGNAL = 0, EMPTY = 1, STILL = 2, MOVING = 3, CHECKING = 4, FALL = 5 };
static const char *const STATE_NAMES[] = {"no_signal", "empty", "still", "moving", "checking", "fall"};

enum Outcome : uint8_t { EV_WALKING = 0, EV_MOVED_AGAIN = 1, EV_LEFT = 2, EV_MODEL_SAFE = 3, EV_FALL = 4 };
static const char *const OUTCOME_NAMES[] = {"walking", "moved_again", "left", "model_safe", "fall"};

struct Config {
  uint32_t version = 3;
  // motionTh and spikeTh are multiples of the room's quiet level, which Ghost keeps
  // learning by itself, so they work in any room and with any board placement.
  float motionTh = 1.4f;     // this many times the quiet level counts as someone moving
  float devTh = 0.02f;       // change from the empty-room shape that counts as someone there
  float spikeTh = 2.0f;      // a sudden burst this many times the quiet level might be a fall
  float stillSecs = 10.0f;   // still on the floor this long after a burst -> alert
  float maxBurstSecs = 6.0f; // kept for settings compatibility; bursts are no longer cut off
  uint8_t modelOn = 0;       // use the trained classifier to veto sit-downs
  float w[NFEAT] = {0, 0, 0, 0, 0, 0};
  float b = 0;
  float holdSecs = 30.0f;    // after someone moves around, count them as present this long
  float presFrac = 0.15f;    // share of the last 10 s with movement that means someone is here
  float calFloor = 0;        // quiet level measured by the last good calibration (0 = none)
  float calFloorShort = 0;   // same for the 1 s measure
};

enum CalibResult : int8_t { CAL_NONE = 0, CAL_OK = 1, CAL_REJECTED = 2 };

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
  float floorLevel = 0;            // learned quiet level of the motion measure (0 = not yet)
  float floorShort = 0;            // same for the 1 s measure the fall logic uses
  int8_t calibResult = CAL_NONE;   // how the last calibration went

  // Calibration progress, 0..1, or -1 when not calibrating.
  float calibProgress() const {
    if (!calibrating) return -1;
    return calibDone / (float)(calibFrames * 2);
  }

  Sensor() { memset(base, 0, sizeof(base)); reset(); }

  void reset() {
    head = 0; count = 0; lastPktT = 0; seq = 0;
    phase = IDLE; state = NO_SIGNAL; movingRun = 0; fastRun = 0; calmRun = 0;
    calmInit = false; calmDev = 0;
    for (int k = 0; k < NSC; k++) calmProf[k] = 1;
    profHead = 0; profCount = 0;
    memset(movHist, 0, sizeof(movHist)); movHead = 0; movCount = 0; movSum = 0;
    haveSustained = false; lastSustainedT = 0;
  }

  // Start from a saved calibration (call after loading cfg from storage).
  void restoreFloor() {
    if (cfg.calFloor > 0) floorLevel = cfg.calFloor;
    if (cfg.calFloorShort > 0) floorShort = cfg.calFloorShort;
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
    if (calibFrames > CAL_MAX / 2) calibFrames = CAL_MAX / 2;
    calibDone = 0;
    calibrating = true;
    memset(calibSum, 0, sizeof(calibSum));
    memcpy(prevBase, base, sizeof(base));
    prevHasBase = hasBase;
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
      profCount = 0;  // don't mix old room shapes into the motion measure later
      out.motion = out.shortm = out.dev = 0;
      out.rssi = 0; out.rate = 0; out.state = state;
      memset(out.prof, 0, NSC);
      return true;
    }
    float prof[NSC];
    profile(n < PROF_WIN ? n : PROF_WIN, prof);
    memcpy(profRing[profHead], prof, sizeof(prof));
    profHead = (profHead + 1) % MOT_FRAMES;
    if (profCount < MOT_FRAMES) profCount++;
    float motion = profMotion(MOT_FRAMES);
    float shortm = profMotion(SPIKE_FRAMES);
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
  int movingRun = 0, fastRun = 0, calmRun = 0, absentRun = 0;
  uint32_t evStart = 0, burstEnd = 0, checkStart = 0;
  float evPeak = 0, preDev = 0;
  static constexpr int PEAK_FRAMES = 30;  // 3 s of frames
  float peakRing[PEAK_FRAMES] = {0};
  int peakHead = 0;
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
  float calM[CAL_MAX], calS[CAL_MAX], calD[CAL_MAX / 2], calTmp[CAL_MAX];
  float prevBase[NSC];
  bool prevHasBase = false;

  float profRing[MOT_FRAMES][NSC];
  int profHead = 0, profCount = 0;
  uint8_t movHist[PRES_FRAMES];
  int movHead = 0, movCount = 0, movSum = 0;
  bool haveSustained = false;
  uint32_t lastSustainedT = 0;

  static float median(float *a, int n) {  // sorts a in place
    for (int i = 1; i < n; i++) {
      float v = a[i];
      int j = i - 1;
      while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
      a[j + 1] = v;
    }
    return n % 2 ? a[n / 2] : 0.5f * (a[n / 2 - 1] + a[n / 2]);
  }

  // How much the room shape moved over the last m frames: each subcarrier's spread,
  // then the median subcarrier. The median ignores the few subcarriers that are always
  // noisy, and it does not depend on the person standing on the line between the boards.
  float profMotion(int m) const {
    if (m > profCount) m = profCount;
    if (m < 3) return 0;
    float sp[NSC];
    for (int k = 0; k < NSC; k++) {
      float s = 0, s2 = 0;
      for (int i = 0; i < m; i++) {
        float v = profRing[(profHead - 1 - i + MOT_FRAMES) % MOT_FRAMES][k];
        s += v; s2 += v * v;
      }
      float mean = s / m;
      float var = s2 / m - mean * mean;
      sp[k] = var > 0 ? sqrtf(var) : 0;
    }
    return median(sp, NSC);
  }

  // Follows the quiet level of the motion measure: drops quickly, rises very slowly, so a
  // person moving around barely lifts it but a noisier room is learned within minutes.
  static void follow(float &fl, float v, float cal) {
    if (fl <= 0) fl = v;
    else fl += (v < fl ? 0.02f : 0.0005f) * (v - fl);
    if (cal > 0) {  // a good calibration keeps it within sensible bounds
      if (fl < 0.6f * cal) fl = 0.6f * cal;
      if (fl > 2.0f * cal) fl = 2.0f * cal;
    }
  }
  void updateFloor(float motion, float shortm) {
    if (profCount < MOT_FRAMES) return;
    follow(floorLevel, motion, cfg.calFloor);
    follow(floorShort, shortm, cfg.calFloorShort);
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
    calM[calibDone] = motion;
    calS[calibDone] = shortm;
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
    calD[calibDone - calibFrames] = dev < 0 ? 0 : dev;
    calibDone++;
    if (calibDone < calibFrames * 2) return;

    calibrating = false;
    phase = IDLE;
    int nM = calibFrames * 2, nD = calibFrames;
    // Someone moving during calibration would set everything too high (the room would
    // then look "empty" with a person in it). Moments well above the calibration's own
    // typical level give them away: an empty room has almost none.
    float *tmp = calTmp;  // not on the stack: the loop task's stack is small
    memcpy(tmp, calM, sizeof(float) * nM);
    float medM = median(tmp, nM);
    int loud = 0;
    for (int i = 0; i < nM; i++) loud += calM[i] > cfg.motionTh * medM;
    memcpy(tmp, calD, sizeof(float) * nD);
    float medD = median(tmp, nD);
    int shifted = 0;
    for (int i = 0; i < nD; i++) shifted += calD[i] > 3 * medD + 0.004f;
    if (loud > nM / 10 || shifted > nD / 10) {
      memcpy(base, prevBase, sizeof(base));
      hasBase = prevHasBase;
      calibResult = CAL_REJECTED;
      return;
    }
    double sd = 0, ssd = 0;
    for (int i = 0; i < nD; i++) { sd += calD[i]; ssd += calD[i] * calD[i]; }
    double mD = sd / nD, sdD = sqrt(fmax(0.0, ssd / nD - mD * mD));
    cfg.devTh = (float)fmax(fmax(mD * 3.0, mD + 6 * sdD), 0.004);
    memcpy(tmp, calS, sizeof(float) * nM);
    float medS = median(tmp, nM);
    cfg.calFloor = medM;
    cfg.calFloorShort = medS;
    floorLevel = medM;
    floorShort = medS;
    calibResult = CAL_OK;
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
    updateFloor(motion, shortm);
    bool ready = floorLevel > 0 && floorShort > 0;
    bool moving = ready && motion >= cfg.motionTh * floorLevel;  // steady 2 s view: what the page shows
    // The fall logic reacts on the 1 s view, so a pause or a getting-up shows within a second.
    bool fast = ready && shortm >= cfg.motionTh * floorShort;
    bool spike = ready && shortm >= cfg.spikeTh * floorShort;
    movingRun = moving ? movingRun + 1 : 0;
    fastRun = fast ? fastRun + 1 : 0;
    calmRun = fast ? 0 : calmRun + 1;

    // Someone is here if the room's shape differs from the empty room, or if there was
    // movement in a good share of the last 10 s (fidgeting, typing), or for a while after
    // they clearly moved around (sitting still out of the boards' best view).
    movSum += (int)moving - movHist[movHead];
    movHist[movHead] = moving;
    movHead = (movHead + 1) % PRES_FRAMES;
    if (movCount < PRES_FRAMES) movCount++;
    if (movingRun >= 10) { haveSustained = true; lastSustainedT = t; }
    bool shapePresent = hasBase && dev >= cfg.devTh;
    bool fidgeting = movCount >= 20 && movSum >= cfg.presFrac * movCount;
    bool recentlyMoved = haveSustained && t - lastSustainedT < (uint32_t)(cfg.holdSecs * 1000);
    bool present = shapePresent || fidgeting || recentlyMoved;
    // Deciding "they left" after a burst keeps the old rule (room shape back to empty),
    // since the hold above would otherwise turn every exit into a fall alert.
    bool shapeAbsent = hasBase ? !shapePresent : false;

    if (!moving) {  // remember what the room looked like while calm
      if (!calmInit) { memcpy(calmProf, prof, sizeof(calmProf)); calmDev = dev; calmInit = true; }
      for (int k = 0; k < NSC; k++) calmProf[k] = 0.9f * calmProf[k] + 0.1f * prof[k];
      calmDev = 0.9f * calmDev + 0.1f * dev;
    }

    if (state == FALL) {
      if (fastRun >= 15) { state = MOVING; gotUp = true; }  // 1.5 s of movement: they are up
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
          for (int i = 0; i < PEAK_FRAMES; i++) peakRing[i] = 0;
          peakRing[0] = shortm;
          peakHead = 1;
        }
        break;
      case BURST:
        // Someone can walk in and then fall, so a long burst is not dropped: it keeps
        // going until a calm second. The peak only counts the last 3 s, so the fall
        // itself, not the walk before it, sets it. A long burst shows in the duration.
        peakRing[peakHead] = shortm;
        peakHead = (peakHead + 1) % PEAK_FRAMES;
        evPeak = 0;
        for (int i = 0; i < PEAK_FRAMES; i++) if (peakRing[i] > evPeak) evPeak = peakRing[i];
        if (calmRun >= 10) {  // a full second of calm ends the burst
          phase = CHECK;
          // The calm second can start before the burst did (a short spike that never
          // counted as moving); without this the duration wrapped around to ~4 million s.
          burstEnd = t - 1000 > evStart ? t - 1000 : evStart;
          checkStart = t;
          postN = 0; postDevSum = 0; postMotionSum = 0; absentRun = 0;
          memset(postProfSum, 0, sizeof(postProfSum));
          scored = false;
        }
        break;
      case CHECK: {
        if (fastRun >= 5) {  // half a second of movement: they got up or kept going
          evFeat = makeFeat();
          logEvent(t, EV_MOVED_AGAIN);
          phase = IDLE;
          break;
        }
        // Not done on purpose: cancelling the check on small fidgets. In a real bedroom
        // recording, quiet typing showed movement in only ~7% of frames with gaps up to
        // 30 s, close to an empty room, so such a rule would mostly be noise and could hide
        // a real fall where the person is breathing hard. Telling sitting down from falling
        // is the trained model's job (docs/RECORDING_SESSION.md).
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
        absentRun = shapeAbsent ? absentRun + 1 : 0;
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

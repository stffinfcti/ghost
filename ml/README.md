# Does Ghost's sensing work? Tests on real ESP32 recordings

Run on 24 Nov 2026, before the boards arrived. Everything here uses public data,
not our own boards.

## Data

ESP-Fi HAR: Wen, Ruan, Wang, Zhou, Gao, Li, "ESP-Fi HAR: A low-power WiFi CSI
dataset for Ad-Hoc IoT human activity recognition", Ad Hoc Networks, 2026. CC BY 4.0.
We used the meeting-room part that ships with the code: 8 people x 7 activities
(arm wave, fall, jump, run, squat, turn, walk) x 10 tries = 560 clips of 950 CSI
packets x 52 subcarrier amplitudes, recorded on ESP32-C3 boards.

Differences from Ghost: a different ESP32 chip (C3, ours is the original ESP32),
packet rate not stated (we assumed 100 per second), clips are only 9.5 s, and there
are no empty-room recordings.

## How

1. `convert_espfi.py` turns the .mat files into raw float files.
2. `harness.cpp` compiles the board's own `sensing.h` on a PC and feeds it the clips,
   so the numbers below come from the same code that runs on the listener.
3. `eval_espfi.py`, `upper_bound_espfi.py`, `same_room_espfi.py`, `pipeline_espfi.py`
   score it. Results are in `results/`.

## Results

| Test | Falls caught | Other moves flagged as falls |
|---|---|---|
| Movement detected at all (1.6x quiet level) | 98-100% of clips of every activity | n/a |
| Board's state machine, "burst then still for 3 s" | 66% | squat 76%, arm wave 74%, walk 1% |
| Simple 6-number model, new person never seen | 74% | 33% (squat 50%) |
| Bigger model, new person never seen | 49% | 7% (squat 15%) |
| Bigger model, same person and room, trained on 8 of their falls | 73% | 1% (squat 2.5%) |

What this means:

- Seeing movement works. Every activity shows up clearly above the quiet level.
- Bug found and fixed: the first calibration rule set the burst threshold about 5x the
  quiet level, which only 2% of falls reach. It is now 1.6x (98% of falls reach it).
- "Big burst, then still" alone cannot tell a fall from someone who squats or waves
  and then stands still. The long-stillness rule (10 s on the board) is what has to do
  that work, and these 9.5 s clips can't test it.
- Telling a fall apart needs a model trained in the room where Ghost runs. Trained on
  the same person and room it caught 73% of falls with 1 false alarm in 100 other
  moves, from just 8 example falls each. Across new people it falls apart. So the plan
  is: record our own labeled falls and non-falls in the demo room, and train there.

## Only the real boards can prove

- That our original-ESP32 boards give CSI from the sender's ESP-NOW packets while on
  home Wi-Fi, at about 100 packets per second.
- Empty room vs someone there (no empty-room data here).
- That "still on the floor for 10 s" separates falls from standing still, and how
  often it false-alarms over a whole day.
- Alerts reaching a phone through ntfy.

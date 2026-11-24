# Ghost

Ghost notices a fall using only Wi-Fi signals. Two $6 ESP32 boards sit on opposite
sides of a room. One sends a steady stream of tiny Wi-Fi packets; the other listens
and reads how a human body bends those signals (Channel State Information, CSI).
When someone falls and stays down, Ghost sends a push alert to family. No camera,
no microphone, nothing to wear, so it can watch the rooms where nobody puts a camera.

Built for GenHacks (Robotics & IoT) and Arbiter Hacks V1, Nov to Dec 2026.

## Try it
- Flash the boards from your browser: https://stffinfcti.github.io/ghost/ (Chrome or Edge on a computer)
- How well it works so far, tested on public ESP32 recordings: `ml/README.md`

## Parts
- 2 or 3 ESP32-WROOM-32 dev boards (USB-C), about $19 for a 3-pack
- USB cables and any phone chargers
- A 2.4 GHz home Wi-Fi network

## Layout
- `firmware/ghost_sender`: board A, sends 100 packets per second over ESP-NOW
- `firmware/ghost_listener`: board B, turns CSI into motion and presence, runs the fall detector, serves the phone page, sends alerts
- `firmware/libraries/GhostCommon`: Wi-Fi setup portal shared by both boards
- `web/`: the hosted site (one-click flasher and a live view over the internet)
- `ml/`: desktop harness that runs the board's sensing code, scenario tests, tests on public ESP32 data
- `docs/PLAN.md`: day-by-day plan
- `docs/RECORDING_SESSION.md`: how to record training data in your room

## Tools used
Arduino core for ESP32 3.0.7, ESP-NOW, ESP-IDF Wi-Fi CSI API, ESP Web Tools,
ntfy.sh for push alerts, Python, NumPy, SciPy and scikit-learn. AI coding help: Claude (Anthropic).

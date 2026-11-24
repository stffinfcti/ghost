# Ghost

Ghost notices a fall using only Wi-Fi signals. Two $6 ESP32 boards sit on opposite
sides of a room. One sends a steady stream of tiny Wi-Fi packets; the other listens
and reads how a human body bends those signals (Channel State Information, CSI).
When someone falls and stays down, Ghost sends a push alert to family. No camera,
no microphone, nothing to wear, so it can watch the rooms where nobody puts a camera.

Built for GenHacks (Robotics & IoT) and Arbiter Hacks V1, Nov to Dec 2026.

## Parts
- 2 or 3 ESP32-WROOM-32 dev boards (USB-C), about $19 for a 3-pack
- USB cables and any phone chargers
- A 2.4 GHz home Wi-Fi network

## Layout
- `firmware/ghost_sender`: board A, sends 100 packets per second over ESP-NOW
- `firmware/ghost_listener`: board B, turns CSI into motion and presence, runs the fall detector, serves the phone page, sends alerts
- `firmware/libraries/GhostCommon`: Wi-Fi setup portal shared by both boards
- `web/`: the hosted site (flasher, live dashboard, replay of recorded sessions)
- `ml/`: simulator, offline replay and training of the fall classifier
- `docs/PLAN.md`: day-by-day plan

## Tools used
Arduino core for ESP32 3.0.7, ESP-NOW, ESP-IDF Wi-Fi CSI API, ESP Web Tools,
ntfy.sh for push alerts, Python and NumPy. AI coding help: Claude (Anthropic).

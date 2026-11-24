# Ghost plan to the deadlines

Arbiter Hacks is due Dec 20, 9 PM Pacific (General track). GenHacks is due Dec 26,
5 PM Eastern (Robotics & IoT track). Commit every working session, and keep every
recording in the repo (EvoHacks lessons).

| When | What | Done when |
|---|---|---|
| Nov 24 (done) | Board code, phone page, flasher page, tests on public ESP32 data | both firmwares compile, 6 of 6 scenario tests pass |
| Boards arrive | Flash both boards from the flasher page, join Wi-Fi, calibrate | the listener page shows packets at about 100 per second |
| Same day | Walk around vs empty room | motion line clearly jumps when you move. If not, tell me before anything else |
| Thanksgiving weekend | Recording session 1 (docs/RECORDING_SESSION.md), both listeners | about 100 labeled moves saved |
| Dec 1 checkpoint | I train on session 1 and report falls caught and false alarms, one listener vs two | if the boards can't tell an empty room from someone moving, we switch to BinGuard |
| Dec 2-7 | Model on the board or on the phone page, second-listener merge, alert wording | a staged fall on the mattress sends a phone alert |
| Dec 8-10 | Recording session 2 on another day | numbers hold up on new data |
| Dec 11-14 | 100-trial test: 30 falls, 70 everyday moves, plus one full evening of normal life | a real table of misses and false alarms for the video |
| Dec 15-18 | Demo video (3 min max), photos of the setup, Devpost write-up, public repo | everything uploaded |
| Dec 19 | Submit to Arbiter (one day early) | confirmation email |
| Dec 20-25 | Polish from Arbiter feedback, GenHacks write-up with its 4 criteria | |
| Dec 25 | Submit to GenHacks (one day early) | confirmation email |

The demo video's story: who falls and lies there unseen, why cameras in bedrooms and
bathrooms are a no, two $8 boards, a live fall on the mattress, the phone buzzing,
then the honest numbers.

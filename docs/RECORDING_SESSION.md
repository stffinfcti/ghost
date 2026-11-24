# In-room recording session (about 1 hour, once the boards arrive)

Goal: teach Ghost what a fall looks like in this one room, and measure whether the
third board (a second listener) helps.

## Setup (10 min)
1. Sender board plugged in at one side of the room, about waist height.
2. Listener A across the room, about 3-5 m away, so the floor area where a fall
   could happen sits between them.
3. Listener B on a third wall, so the sender-to-B line crosses the room at a
   different angle from sender-to-A.
4. A mattress or thick blankets on the floor in the middle for staged falls.
5. Open each listener's page on the phone (ghost.local, or the address it shows).

## Calibrate (2 min)
Leave the room empty and press Calibrate on each listener. 30 s each.

## Record (40 min)
Press Record on both pages, tap the label as you do each move, and save both
files afterwards. Each move takes about 15 s: stand still 3 s, do the move, then
stay where you end up for 10 s (still on the floor after a fall, still in the chair
after sitting).

| Move | Times | Why |
|---|---|---|
| Fall onto the mattress (forward, sideways, backward, slump from sitting) | 30 | what we want to catch |
| Sit down on a chair or bed and stay | 15 | the hardest false alarm: sudden drop, then still |
| Lie down on purpose | 10 | looks like a fall ending |
| Squat or pick something up, then stand | 10 | the worst confuser in the public test |
| Bend over, tie a shoe | 10 | short burst, then still |
| Walk through, walk in and out | 10 | normal life |
| Empty room | 5 x 30 s | presence check |

About 100 labeled moves per session. A second session on another day (moved
furniture, different clothes) is how we find out whether it holds up.

## After
Send the saved files over and I train the model on them, test it on moves it
never saw, and compare listener A alone, listener B alone, and both together.

## Safety
Only fall onto the mattress, from standing or sitting, nothing from a height.
Stop if anything hurts. A teammate or family member in the room is better.

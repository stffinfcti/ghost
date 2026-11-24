# First run: from the box to a working motion line (about 30 minutes)

You need: the boards, 2 USB cables that carry data, a computer with Chrome or Edge,
your phone, and your home Wi-Fi password. Your Wi-Fi must have a 2.4 GHz network.

## 0. Two ways to flash
- **From a terminal (or Claude Code) on your computer:** `pip install esptool`, then
  `tools/flash.sh sender` with the first board plugged in and `tools/flash.sh listener`
  with the second. Add the port if it picks the wrong one, like `tools/flash.sh sender COM3`.
  Then skip to step 3.
- **From the browser:** the steps below, once the flasher page is online.

## 0b. Once, for the browser way: turn on the flasher page
GitHub repo ghost, then Settings, then Pages, then Source: GitHub Actions. After about
a minute the page is at https://stffinfcti.github.io/ghost/

## 1. Flash the sender (5 min)
1. Put a piece of tape on one board and write S on it.
2. Plug it into the computer, open the flasher page, press **Install Sender on this board**.
3. Pick the board from the list (usually "CP2102" or "USB Serial"), then Install.
   - Nothing in the list: try another cable, then install the CP210x or CH340 driver
     (whichever chip sits next to the board's USB port).
   - Stuck on "connecting": hold the board's BOOT button while you press Install.
4. Wait for "Installation complete". Unplug.

## 2. Flash the listener (5 min)
Tape the second board with an L, plug it in, press **Install Listener on this board**, same steps.
Keep the third board as a spare (or a second listener later).

## 3. Put both boards on your Wi-Fi (5 min)
1. Plug the S board into any phone charger. Its blue light blinks slowly.
2. On your phone, join the Wi-Fi called Ghost-sender-XXXX. A setup page opens
   (if not, go to http://192.168.4.1). Pick your Wi-Fi, type the password, Save.
3. Do the same for the L board (Ghost-listener-XXXX), and type the room's name.
4. When the L board's blue light stays solid, it is hearing the sender.

## 4. Open the listener's page (2 min)
On the computer (same Wi-Fi), open http://ghost.local
- Doesn't open? Plug the L board into the computer, open the flasher page, press
  Install Listener, then **Logs & console**, press **Reset device**, and read the line
  "open http://ghost.local or http://192.168.x.x". Use that number on any device.
- Check the numbers under Motion: about 100 pkt/s means it works.
  Below 50 or 0: move the boards closer, and check both joined the same Wi-Fi.

## 5. The test that matters (5 min)
1. Put S and L on opposite sides of the room, about waist height, 3 to 5 m apart.
2. Leave the room, press **Calibrate** from the doorway with the phone, stay out 30 s.
3. Walk around the room for 10 s, then stand still for 10 s, then leave.
4. The blue motion line should jump while you walk and drop when you stop.
5. Screenshot the page and send it over, with the two numbers: motion while empty,
   and motion while walking.

If the line jumps: we're good, do the recording session (docs/RECORDING_SESSION.md).
If it doesn't: send the screenshot anyway, that's the Dec 1 checkpoint coming early.

## 6. Alerts (2 min)
Install the free ntfy app, subscribe to the topic shown on the listener page, press
**Send test alert**. Your phone should buzz.

# Dial Throttle (M5Stack Dial + SparkFun Qwiic Numpad)

This project turns an `M5Stack Dial` into a WiThrottle controller for JMRI,
with a `SparkFun Qwiic Keypad/Numpad (COM-15290)` as extra input buttons.

## What It Does

- Connects to your WiFi network.
- Connects to JMRI WiThrottle server (default port `12090`).
- Acquires one locomotive address.
- Uses the Dial encoder to set speed (`0..126`).
- Uses Dial button press to toggle direction.
- Uses Qwiic keypad keys for common actions and functions.
- Sends WiThrottle heartbeat commands to keep the session alive.

## Hardware

- M5Stack Dial (ESP32-S3)
- SparkFun Qwiic Keypad/Numpad (COM-15290)
- Qwiic cable

## Wiring

The Qwiic connector carries:

- `3V3`
- `GND`
- `SDA`
- `SCL`

For M5Stack Dial, plug the Qwiic cable into the Dial's Grove/I2C-capable port
or adapter that exposes `3V3/GND/SDA/SCL`.

The code uses default I2C address `0x4B` for the SparkFun keypad.

## Project Layout

- `platformio.ini` - PlatformIO environment and dependencies
- `include/config.h` - local configuration (WiFi, JMRI host, loco address)
- `src/main.cpp` - firmware

## Configure

Edit `include/config.h`:

- `WIFI_SSID`
- `WIFI_PASS`
- `WITHROTTLE_HOST`
- `WITHROTTLE_PORT`
- `LOCO_ADDRESS`
- `LOCO_IS_LONG_ADDRESS`

## Build And Flash (PlatformIO)

1. Install PlatformIO extension in VS Code.
2. Open this folder as the workspace root.
3. Build and upload:

```bash
pio run -t upload
```

4. Open serial monitor:

```bash
pio device monitor -b 115200
```

## Key Mapping (Qwiic Numpad)

- `0` -> Toggle `F0`
- `1` -> Toggle `F1`
- `2` -> Toggle `F2`
- `3` -> Toggle `F3`
- `4` -> Toggle `F4`
- `5` -> Toggle `F5`
- `6` -> Toggle `F6`
- `7` -> Toggle `F7`
- `8` -> Toggle `F8`
- `9` -> Emergency stop
- `*` -> Turnout mode (enter digits, press `*` again to flip)
- `#` -> Loco select mode (enter digits, press `#` again to acquire)

Locomotive acquire/release is automatic:

- Selecting a new loco automatically releases the old one and acquires the new one.
- Speed, direction, function, and e-stop actions auto-acquire if needed.

## Dial Controls

- Rotate encoder: speed up/down
- Rotate encoder past 0: automatically flips direction and ramps in reverse
- Press center button: stop to 0 if moving; if already 0, reverse direction
- Press and hold touchscreen: brake while held, recover to demand speed on release
- While E-Stop is latched, press center button to clear latch
- Address shows `-` until a loco is selected (keypad/RFID/serial)
- Display redraw is event-driven and updates only when input/state changes

## Battery / Power Saving

The firmware now includes built-in idle power saving for rechargeable battery use:

- Backlight runs at `DISPLAY_BRIGHTNESS_ACTIVE` during use.
- Backlight dims to `DISPLAY_BRIGHTNESS_DIM` after `DISPLAY_DIM_AFTER_MS` idle.
- Backlight turns off after `DISPLAY_OFF_AFTER_MS` idle.
- Any user input (dial/touch/button/keypad/RFID/serial command) wakes backlight immediately.
- Optional WiFi modem sleep is controlled by `ENABLE_WIFI_MODEM_SLEEP`.

Tune these values in `include/config.h`:

- `DISPLAY_BRIGHTNESS_ACTIVE`
- `DISPLAY_BRIGHTNESS_DIM`
- `DISPLAY_DIM_AFTER_MS`
- `DISPLAY_OFF_AFTER_MS`
- `ENABLE_WIFI_MODEM_SLEEP`


## Serial Debug Commands

This project currently ships with `SERIAL_OUTPUT_ONLY = false` in `include/config.h`.
To debug, switch this flag to `true` so outgoing WiThrottle lines are printed to serial as `WT> ...` and are
not transmitted over WiFi.

Open monitor:

```bash
pio device monitor -b 115200 --port /dev/ttyACM0
```

Type commands and press Enter:

- `help` - list commands
- `status` - print WiFi/WiThrottle/loco state
- `acq` - acquire configured loco
- `rel` - release loco
- `estop` - emergency stop
- `clear` - clear E-Stop latch (also supports `estop clear`)
- `speed 45` - set speed to 45
- `dir f` / `dir r` / `dir t` - set/toggle direction
- `fn 0 on` / `fn 1 off` / `fn 2 t` - function control
- `wt` - reconnect WiThrottle
- `wifi` - reconnect WiFi
- `send M0A*<;>qV` - send raw WiThrottle command line

## JMRI Setup Notes

1. In JMRI, enable WiThrottle server.
2. Confirm server port (default `12090`).
3. Make sure M5Stack Dial and JMRI host are on the same network.
4. If firewall is enabled, allow TCP port `12090`.

## Troubleshooting

- If keypad is not detected:
	- Verify I2C wiring/Qwiic cable.
	- Confirm keypad address (`0x4B` by default).
- If WiThrottle does not connect:
	- Check `WITHROTTLE_HOST` and `WITHROTTLE_PORT`.
	- Verify JMRI WiThrottle service is running.
- If loco does not respond:
	- Verify `LOCO_ADDRESS` and long/short address setting.

## Protocol Notes

The firmware uses WiThrottle text commands including:

- `N` for client name
- `HU` for unique client id
- `*` / `*+` heartbeat
- `M0+` to acquire loco
- `M0A...<;>V` for speed
- `M0A...<;>R` for direction
- `M0A...<;>f` for force function state
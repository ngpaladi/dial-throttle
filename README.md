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

- `1` -> Toggle `F0` (headlight)
- `2` -> Toggle `F1` (bell)
- `3` -> Toggle `F2` (horn)
- `4` -> Emergency stop
- `5` -> Speed to `0`
- `6` -> Toggle direction
- `7` -> Release locomotive
- `8` -> Acquire locomotive
- `9` -> Toggle `F5`
- `0` -> Toggle `F8`
- `*` -> Speed `-5`
- `#` -> Speed `+5`

## Dial Controls

- Rotate encoder: speed up/down
- Press center button: toggle direction

## Serial Debug Commands

This project currently ships with `SERIAL_OUTPUT_ONLY = true` in `include/config.h`.
In this mode, outgoing WiThrottle lines are printed to serial as `WT> ...` and are
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
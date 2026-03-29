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

## Interactive Installer (Recommended)

For less technical users, use the guided installer script.

Linux/macOS:

```bash
./install.sh
```

Windows PowerShell:

```powershell
.\install.ps1
```

Windows Command Prompt:

```bat
install.bat
```

For unattended installs (clubs/events), you can run non-interactive mode:

```bash
./install.sh --yes --port /dev/ttyACM0 --no-monitor
```

Windows unattended example:

```powershell
.\install.ps1 --yes --port COM3 --no-monitor
```

Useful flags:

- `--yes` / `-y`: assume yes to prompts
- `--port <path>`: choose port without prompts
- `--monitor` / `--no-monitor`: force serial monitor behavior after flashing
- `--edit-config` / `--no-edit-config`: force config editor behavior
- `--help`: show all options

The installer will:

- Check/install PlatformIO Core (`pio`)
- Optionally let you edit `include/config.h`
- Detect connected serial ports and ask you to choose one
- Build firmware and upload to the selected M5Stack Dial
- Optionally open serial monitor after flashing

If your board does not appear or upload fails on Linux, you may need serial permissions:

```bash
sudo usermod -aG dialout "$USER"
```

Then log out and back in.

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

## RFID Lookup Order

RFID tag selection now resolves locomotive IDs in this order:

1. MySQL lookup on the same host as WiThrottle (if enabled)
2. Local `RFID_LOCO_MAP` dictionary in `include/config.h`
3. Last 4 hex digits of tag UID (normalized to address `1..9999`)

Configure MySQL fields in `include/config.h`:

- `ENABLE_RFID_MYSQL_LOOKUP`
- `RFID_MYSQL_HOST`
- `RFID_MYSQL_PORT`
- `RFID_MYSQL_USER`
- `RFID_MYSQL_PASS`
- `RFID_MYSQL_DB`
- `RFID_MYSQL_TABLE`
- `RFID_MYSQL_UID_COLUMN`
- `RFID_MYSQL_LOCO_COLUMN`
- `RFID_MYSQL_IS_LONG_COLUMN` (optional)

Expected `loco_id` values can be either:

- Prefixed (`S3`, `L128`)
- Numeric only (`3`, `128`), where `>127` is treated as long address

Use the included sample schema and seed script:

```bash
mysql -h <server-ip> -u <user> -p < /path/to/dial-throttle/rfid_mysql_schema.sql
```

### MySQL Verification Checklist

1. In `include/config.h`, set:
	- `ENABLE_RFID_MYSQL_LOOKUP = true`
	- `RFID_MYSQL_HOST` to an IP address (same server as WiThrottle)
	- DB credentials and column names to match your table
2. Confirm the DB row exists for a test tag UID:
	- `SELECT loco_id, is_long FROM wifithrottle.rfid_loco_map WHERE rfid_uid='DEADBEEF' LIMIT 1;`
3. Flash firmware and open serial monitor.
4. Scan a mapped tag and verify serial shows source `(db)` and status source `RFID-DB`.
5. Temporarily remove/rename that DB row, scan again, and verify fallback to `(map)` / `RFID-MAP`.
6. Remove local map entry too, scan again, and verify fallback to `(tail)` / `RFID-TAIL`.
7. Confirm resulting loco address is in range `1..9999` and acquires in JMRI.

Note: Current firmware expects `RFID_MYSQL_HOST` as a numeric IP string.


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
#pragma once

// WiFi settings
static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// JMRI WiThrottle server settings
static const char* WITHROTTLE_HOST = "192.168.1.50";
static const uint16_t WITHROTTLE_PORT = 12090;

// Locomotive settings
static const uint16_t LOCO_ADDRESS = 3;
static const bool LOCO_IS_LONG_ADDRESS = false;

// Device identity shown in JMRI
static const char* THROTTLE_NAME = "DialThrottle";
static const char* THROTTLE_ID_PREFIX = "M5DIAL-";

// Optional SparkFun Qwiic Keypad address (default is 0x4B)
static const uint8_t KEYPAD_I2C_ADDR = 0x4B;

// Encoder tuning (Dial v1.1):
// Use raw absolute encoder position and hysteresis to suppress jitter blips.
static const int ENCODER_RAW_COUNTS_PER_STEP = 2;
static const int ENCODER_SPEED_STEP = 1;
// Set to -1 if turning clockwise decreases speed on your unit.
static const int ENCODER_DIRECTION_SIGN = 1;
// Extra counts required beyond step boundary before direction change is accepted.
static const int ENCODER_HYSTERESIS_COUNTS = 1;

// Touch brake behavior (press and hold to brake, release to recover).
static const uint32_t BRAKE_TICK_MS = 40;
static const int BRAKE_DECEL_PER_TICK = 3;
static const int BRAKE_ACCEL_PER_TICK = 4;
static const uint32_t BRAKE_TOUCH_DEBOUNCE_MS = 120;

// Debug mode: print outgoing WiThrottle lines to Serial only, do not send over WiFi.
static const bool SERIAL_OUTPUT_ONLY = false;

// RFID tag to locomotive mapping.
// UID must be uppercase hex without separators, e.g. "04A1B2C3D4".
struct RfidLocoMap {
	const char* uidHex;
	uint16_t address;
	bool isLong;
};

// Add your known tag mappings here.
static const RfidLocoMap RFID_LOCO_MAP[] = {
		{"DEADBEEF", 3, false},
};

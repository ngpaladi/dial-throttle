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
// Velocity-dependent acceleration: quick spins apply larger speed deltas.
static const uint32_t ENCODER_FAST_WINDOW_MS = 28;
static const uint32_t ENCODER_MEDIUM_WINDOW_MS = 65;
static const int ENCODER_FAST_MULTIPLIER = 4;
static const int ENCODER_MEDIUM_MULTIPLIER = 2;
// Anti-jitter filtering for light single-finger scrolling.
static const uint32_t ENCODER_STEP_DEBOUNCE_MS = 4;
static const uint8_t ENCODER_DIR_CHANGE_CONFIRM_STEPS = 2;
static const int ENCODER_MAX_STEPS_PER_UPDATE = 4;
static const int ENCODER_REVERSAL_RAW_THRESHOLD = 5;
// Adaptive feel: relax filtering and allow larger bursts at higher spin rates.
static const int ENCODER_FAST_EXTRA_STEPS = 3;
static const int ENCODER_MEDIUM_EXTRA_STEPS = 1;
// Speed-adaptive scaling: as current speed rises, each accepted step moves farther.
// Use smooth piecewise linear interpolation instead of hard thresholds.
static const int ENCODER_SPEED_SCALING_LOW = 30;    // Below this, no bonus
static const int ENCODER_SPEED_SCALING_MID = 60;    // Ramp +0.5 bonus here
static const int ENCODER_SPEED_SCALING_HIGH = 100;  // At this+, full +1 bonus at mid, +2 at high
// Legacy thresholds (kept for reference, now superseded by smooth curve):
static const int ENCODER_SPEED_STEP_THRESHOLD_MID = 40;
static const int ENCODER_SPEED_STEP_THRESHOLD_HIGH = 85;
static const int ENCODER_SPEED_STEP_BONUS_MID = 1;
static const int ENCODER_SPEED_STEP_BONUS_HIGH = 2;

// Touch brake behavior (press and hold to brake, release to recover).
static const uint32_t BRAKE_TICK_MS = 40;
static const int BRAKE_DECEL_PER_TICK = 3;
static const int BRAKE_ACCEL_PER_TICK = 4;
static const uint32_t BRAKE_TOUCH_DEBOUNCE_MS = 120;

// Debug mode: print outgoing WiThrottle lines to Serial only, do not send over WiFi.
static const bool SERIAL_OUTPUT_ONLY = false;

// UI layout: position of function icon columns (tighter spacing for balanced visual)
static const int16_t UI_FN_LEFT_X = 48;   // Left column x-position
static const int16_t UI_FN_RIGHT_X = 192; // Right column x-position

// Power saving (useful for rechargeable battery operation).
static const uint8_t DISPLAY_BRIGHTNESS_ACTIVE = 72;
static const uint8_t DISPLAY_BRIGHTNESS_DIM = 8;
static const uint32_t DISPLAY_DIM_AFTER_MS = 8000;
static const uint32_t DISPLAY_OFF_AFTER_MS = 20000;
static const bool ENABLE_WIFI_MODEM_SLEEP = true;
static const bool ENABLE_WIFI_LOW_TX_POWER = true;
static const bool ENABLE_WIFI_POWER_GATING_WHEN_DISPLAY_OFF = true;
static const uint16_t CPU_FREQ_MHZ = 80;
static const uint16_t LOOP_DELAY_ACTIVE_MS = 8;
static const uint16_t LOOP_DELAY_DIM_MS = 25;
static const uint16_t LOOP_DELAY_OFF_MS = 80;

// RFID tag to locomotive mapping.
// UID must be uppercase hex without separators, e.g. "04A1B2C3D4".
struct RfidLocoMap {
	const char* uidHex;
	uint16_t address;
	bool isLong;
};

// Optional MySQL lookup for RFID -> loco assignment.
// Lookup order is: MySQL -> RFID_LOCO_MAP -> last 4 UID hex digits.
// Leave disabled until credentials and schema are set.
static const bool ENABLE_RFID_MYSQL_LOOKUP = false;
static const char* RFID_MYSQL_HOST = WITHROTTLE_HOST;
static const uint16_t RFID_MYSQL_PORT = 3306;
static const char* RFID_MYSQL_USER = "dialthrottle";
static const char* RFID_MYSQL_PASS = "change_me";
static const char* RFID_MYSQL_DB = "wifithrottle";
static const char* RFID_MYSQL_TABLE = "rfid_loco_map";
static const char* RFID_MYSQL_UID_COLUMN = "rfid_uid";
static const char* RFID_MYSQL_LOCO_COLUMN = "loco_id";
// Optional long/short column. Set to nullptr to derive long from address (>127).
static const char* RFID_MYSQL_IS_LONG_COLUMN = nullptr;

// Add your known tag mappings here.
static const RfidLocoMap RFID_LOCO_MAP[] = {
		{"DEADBEEF", 3, false},
};

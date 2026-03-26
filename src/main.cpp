#include <Arduino.h>
#include <math.h>
#include <M5Dial.h>
#include <WiFi.h>
#include <Wire.h>
#include <SparkFun_Qwiic_Keypad_Arduino_Library.h>

#include "config.h"

namespace {

WiFiClient wtClient;
KEYPAD keypad;

uint16_t activeLocoAddress = LOCO_ADDRESS;
bool activeLocoIsLong = LOCO_IS_LONG_ADDRESS;
String locoId;
bool locoSelected = false;
bool locoAcquired = false;
bool directionForward = true;
int speed126 = 0;
int throttleDemandSpeed = 0;
bool functionState[29] = {false};
bool encoderUpdatingSpeed = false;
bool brakeHoldActive = false;
bool brakeRecovering = false;
int brakeRecoveryTargetSpeed = 0;
unsigned long lastBrakeStepMs = 0;
unsigned long lastBrakeTouchMs = 0;
bool brakeStoppedToZero = false;
bool estopLatched = false;

bool keypadAddressMode = false;
String keypadAddressBuffer;
bool keypadTurnoutMode = false;
String keypadTurnoutBuffer;
String lastRfidUid;
unsigned long lastRfidMs = 0;

unsigned long lastHeartbeatMs = 0;
unsigned long lastServerActivityMs = 0;

String statusLine = "Booting...";
String infoLine = "";
bool uiDirty = true;
unsigned long lastUserActivityMs = 0;
uint8_t backlightMode = 0; // 0=active, 1=dim, 2=off
bool wifiSuspendedForPower = false;

bool encoderStateInitialized = false;
int32_t encoderLastSignedRaw = 0;
int32_t encoderRawAccumulator = 0;
unsigned long encoderLastMoveMs = 0;
bool encoderResyncPending = false;
int8_t encoderStableDir = 0;
int8_t encoderPendingDir = 0;
uint8_t encoderPendingDirCount = 0;
unsigned long encoderLastAcceptedStepMs = 0;
int8_t encoderIntentDir = 0;
int32_t encoderOppositeRawAccum = 0;

void setStatus(const String& s);
void setInfo(const String& s);
void sendWt(const String& line);
void setSpeed(int newSpeed);
void clearEmergencyStopLatch();
void ensureLocoAcquired();
void applyStopAction(const char* statusText);
void noteUserActivity();
void updatePowerState();
void suspendWiFiForPower();
void resumeWiFiFromPower();

void beep() {
  M5Dial.Speaker.tone(1200, 140);
}

void beepRfidAddressChange() {
  M5Dial.Speaker.tone(1650, 140);
}

void noteUserActivity() {
  lastUserActivityMs = millis();
  if (backlightMode != 0) {
    M5Dial.Display.setBrightness(DISPLAY_BRIGHTNESS_ACTIVE);
    backlightMode = 0;
    uiDirty = true;
    if (wifiSuspendedForPower) {
      resumeWiFiFromPower();
    }
  }
}

void suspendWiFiForPower() {
  if (SERIAL_OUTPUT_ONLY || !ENABLE_WIFI_POWER_GATING_WHEN_DISPLAY_OFF || wifiSuspendedForPower) {
    return;
  }

  wtClient.stop();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  wifiSuspendedForPower = true;
  setInfo("WiFi sleep");
}

void resumeWiFiFromPower() {
  if (SERIAL_OUTPUT_ONLY || !wifiSuspendedForPower) {
    return;
  }

  WiFi.mode(WIFI_STA);
  wifiSuspendedForPower = false;
  setInfo("WiFi wake");
}

void updatePowerState() {
  const unsigned long idleMs = millis() - lastUserActivityMs;
  uint8_t target = 0;
  if (idleMs >= DISPLAY_OFF_AFTER_MS) {
    target = 2;
  } else if (idleMs >= DISPLAY_DIM_AFTER_MS) {
    target = 1;
  }

  if (target == backlightMode) {
    return;
  }

  backlightMode = target;
  if (target == 0) {
    M5Dial.Display.setBrightness(DISPLAY_BRIGHTNESS_ACTIVE);
    if (wifiSuspendedForPower) {
      resumeWiFiFromPower();
    }
  } else if (target == 1) {
    M5Dial.Display.setBrightness(DISPLAY_BRIGHTNESS_DIM);
    if (wifiSuspendedForPower) {
      resumeWiFiFromPower();
    }
  } else {
    M5Dial.Display.setBrightness(0);
    suspendWiFiForPower();
  }
}

void syncEncoderToSpeed() {
  const int32_t targetSteps = speed126 / ENCODER_SPEED_STEP;
  const int32_t rawTarget = targetSteps * ENCODER_RAW_COUNTS_PER_STEP * ENCODER_DIRECTION_SIGN;
  M5Dial.Encoder.write(rawTarget);
  // Ignore one encoder delta after write() so we don't interpret the sync as user rotation.
  encoderResyncPending = true;
}

String buildLocoId(uint16_t address, bool isLong) {
  return String(isLong ? "L" : "S") + String(address);
}

void updateLocoLabel() {
  if (!locoSelected) {
    locoId = "-";
    return;
  }
  locoId = buildLocoId(activeLocoAddress, activeLocoIsLong);
}

void setActiveLoco(uint16_t address, bool isLong, bool autoAcquire, const String& source) {
  if (address == 0 || address > 9999) {
    setStatus("Invalid loco address");
    return;
  }

  const bool addressChanged = (!locoSelected || activeLocoAddress != address || activeLocoIsLong != isLong);

  if (locoAcquired) {
    sendWt(String("M0-") + locoId + "<;>r");
    locoAcquired = false;
  }

  activeLocoAddress = address;
  activeLocoIsLong = isLong;
  locoSelected = true;
  updateLocoLabel();
  if (addressChanged && source.startsWith("RFID")) {
    beepRfidAddressChange();
  }
  setStatus(String("Loco set ") + locoId + " via " + source);

  if (autoAcquire) {
    sendWt(String("M0+") + locoId + "<;>" + locoId);
    locoAcquired = true;
    setStatus(String("Loco acquired ") + locoId + " via " + source);
  }
}

void setStatus(const String& s) {
  statusLine = s;
  uiDirty = true;
  Serial.println(s);
}

void setInfo(const String& s) {
  if (infoLine == s) {
    return;
  }
  infoLine = s;
  uiDirty = true;
}

void sendWt(const String& line) {
  Serial.print("WT> ");
  Serial.println(line);

  if (SERIAL_OUTPUT_ONLY) {
    return;
  }

  if (!wtClient.connected()) {
    Serial.println("! WiThrottle not connected");
    return;
  }
  wtClient.print(line);
  wtClient.print('\n');
  lastServerActivityMs = millis();
}

void drawUi(bool force = false) {
  if (!force && !uiDirty) {
    return;
  }
  uiDirty = false;

  M5Dial.Display.fillScreen(BLACK);
  M5Dial.Display.setTextColor(WHITE, BLACK);
  const int16_t screenW = M5Dial.Display.width();

  auto drawCentered = [&](int16_t y, uint8_t textSize, const String& text) {
    M5Dial.Display.setTextSize(textSize);
    const int16_t x = (screenW - M5Dial.Display.textWidth(text)) / 2;
    M5Dial.Display.setCursor(max<int16_t>(0, x), y);
    M5Dial.Display.print(text);
  };

  const int16_t cx = screenW / 2;
  const int16_t cy = M5Dial.Display.height() / 2;
  const int16_t ringR = 114;
  const uint16_t ringColor = brakeHoldActive ? RED : (brakeRecovering ? ORANGE : DARKGREY);
  M5Dial.Display.drawCircle(cx, cy, ringR, ringColor);
  M5Dial.Display.drawCircle(cx, cy, ringR - 1, ringColor);

  const uint16_t activeBarColor = directionForward ? 0x07FF : 0xFD20;
  const uint16_t idleBarColor = 0x18C3;
  const int16_t barOuterR = 106;
  const int16_t barInnerR = 99;
  const int barSegments = 180;
  const float filledFraction = static_cast<float>(speed126) / 126.0f;
  const int filledSegments = static_cast<int>(filledFraction * barSegments + 0.5f);

  // Draw a continuous circular bar from top (-90 deg), filled proportionally to speed.
  for (int i = 0; i < barSegments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(barSegments);
    const float theta = (-90.0f + 360.0f * t) * DEG_TO_RAD;
    const int16_t xOuter = static_cast<int16_t>(cx + cosf(theta) * barOuterR);
    const int16_t yOuter = static_cast<int16_t>(cy + sinf(theta) * barOuterR);
    const int16_t xInner = static_cast<int16_t>(cx + cosf(theta) * barInnerR);
    const int16_t yInner = static_cast<int16_t>(cy + sinf(theta) * barInnerR);
    const uint16_t color = (i < filledSegments) ? activeBarColor : idleBarColor;
    M5Dial.Display.drawLine(xInner, yInner, xOuter, yOuter, color);
  }

  if (estopLatched) {
    M5Dial.Display.fillRoundRect(40, 8, screenW - 80, 18, 8, RED);
    M5Dial.Display.setTextColor(WHITE, RED);
    drawCentered(11, 1, "E-STOP LATCHED");
    M5Dial.Display.setTextColor(WHITE, BLACK);
  }

  const String addrText = locoSelected ? locoId : "-";
  M5Dial.Display.setTextSize(2);
  const int16_t addrW = M5Dial.Display.textWidth(addrText) + 16;
  const int16_t addrX = (screenW - addrW) / 2;
  M5Dial.Display.fillRoundRect(addrX, 50, addrW, 18, 4, WHITE);
  M5Dial.Display.setTextColor(BLACK, WHITE);
  M5Dial.Display.setCursor(addrX + 8, 52);
  M5Dial.Display.print(addrText);
  M5Dial.Display.setTextColor(WHITE, BLACK);

  M5Dial.Display.setTextSize(4);
  drawCentered(86, 4, String(speed126));

  // Draw a font-independent direction arrow so it renders on all built-in fonts.
  const int16_t ay = 150;
  const int16_t shaftHalf = 20;
  if (directionForward) {
    M5Dial.Display.drawLine(cx - shaftHalf, ay, cx + shaftHalf, ay, WHITE);
    M5Dial.Display.drawLine(cx - shaftHalf, ay - 1, cx + shaftHalf, ay - 1, WHITE);
    M5Dial.Display.fillTriangle(cx + shaftHalf + 1, ay, cx + shaftHalf - 7, ay - 7, cx + shaftHalf - 7,
                                ay + 7, WHITE);
  } else {
    M5Dial.Display.drawLine(cx + shaftHalf, ay, cx - shaftHalf, ay, WHITE);
    M5Dial.Display.drawLine(cx + shaftHalf, ay - 1, cx - shaftHalf, ay - 1, WHITE);
    M5Dial.Display.fillTriangle(cx - shaftHalf - 1, ay, cx - shaftHalf + 7, ay - 7, cx - shaftHalf + 7,
                                ay + 7, WHITE);
  }

  // Function latch chips split left/right of speed. Common functions get letter icons.
  // Draw simple bitmapped icons for functions (no text labels).
  auto drawFnChip = [&](int fn, int16_t x, int16_t y) {
    const bool on = functionState[fn];
    const uint16_t fill = on ? 0x07E0 : 0x2104;      // Green or gray fill
    const uint16_t border = on ? 0xAFE5 : 0x7BEF;    // Bright or dim border
    const uint16_t iconColor = on ? BLACK : 0xC618;  // Black or muted gold
    
    M5Dial.Display.fillRoundRect(x - 10, y - 8, 20, 16, 4, fill);
    M5Dial.Display.drawRoundRect(x - 10, y - 8, 20, 16, 4, border);
    
    // Draw icon based on function number using simple line/circle drawing.
    switch (fn) {
      case 0: { // Light: small circle with rays
        M5Dial.Display.fillCircle(x, y, 2, iconColor);  // bulb center
        M5Dial.Display.drawLine(x - 4, y - 4, x - 3, y - 5, iconColor); // top-left ray
        M5Dial.Display.drawLine(x + 4, y - 4, x + 3, y - 5, iconColor); // top-right ray
        break;
      }
      case 1: { // Bell: inverted V with dots
        M5Dial.Display.drawLine(x - 3, y - 3, x, y, iconColor);
        M5Dial.Display.drawLine(x, y, x + 3, y - 3, iconColor);
        M5Dial.Display.fillCircle(x - 1, y + 1, 1, iconColor); // left dot
        M5Dial.Display.fillCircle(x + 1, y + 1, 1, iconColor); // right dot
        break;
      }
      case 2: { // Horn: angle bracket >
        M5Dial.Display.drawLine(x - 4, y - 3, x + 2, y, iconColor);
        M5Dial.Display.drawLine(x + 2, y, x - 4, y + 3, iconColor);
        M5Dial.Display.drawLine(x - 2, y - 2, x + 3, y + 1, iconColor); // double-line effect
        break;
      }
      case 3: { // Smoke: small clouds (3 overlapping circles)
        M5Dial.Display.drawCircle(x - 2, y, 2, iconColor);
        M5Dial.Display.drawCircle(x, y + 1, 2, iconColor);
        M5Dial.Display.drawCircle(x + 2, y, 2, iconColor);
        break;
      }
      default: { // Generic: just show number
        M5Dial.Display.setTextSize(1);
        M5Dial.Display.setTextColor(iconColor, fill);
        const String num = String(fn);
        M5Dial.Display.setCursor(x - 2, y - 3);
        M5Dial.Display.print(num);
        break;
      }
    }
  };

  // Arrange function icons in two curved columns following the ring shape.
  // Left column: angles 150-210° (left arc), Right column: angles -30 to 30° (right arc)
  // Radius 80 brings them closer to center while following the ring curve.
  const int16_t ringCx = 120;
  const int16_t ringCy = 120;
  const int16_t curveRadius = 80;
  
  // Left column: F0, F1, F2, F3, E-Stop
  const float leftAngles[5] = {150.0f, 165.0f, 180.0f, 195.0f, 210.0f};
  for (int i = 0; i < 5; ++i) {
    const float angle = leftAngles[i] * DEG_TO_RAD;
    const int16_t x = static_cast<int16_t>(ringCx + curveRadius * cosf(angle));
    const int16_t y = static_cast<int16_t>(ringCy + curveRadius * sinf(angle));
    if (i < 4) {
      drawFnChip(i, x, y);
    } else {
      // Draw E-Stop at position 4 (210°)
      const uint16_t estopFill = estopLatched ? RED : 0x2104;
      const uint16_t estopBorder = estopLatched ? 0xFB00 : 0x7BEF;
      const uint16_t estopIcon = estopLatched ? WHITE : 0xC618;
      M5Dial.Display.fillRoundRect(x - 10, y - 8, 20, 16, 4, estopFill);
      M5Dial.Display.drawRoundRect(x - 10, y - 8, 20, 16, 4, estopBorder);
      M5Dial.Display.setTextSize(1);
      M5Dial.Display.setTextColor(estopIcon, estopFill);
      M5Dial.Display.setCursor(x - 2, y - 3);
      M5Dial.Display.print("E");
    }
  }
  
  // Right column: F4-F8
  const float rightAngles[5] = {-30.0f, -15.0f, 0.0f, 15.0f, 30.0f};
  for (int i = 0; i < 5; ++i) {
    const float angle = rightAngles[i] * DEG_TO_RAD;
    const int16_t x = static_cast<int16_t>(ringCx + curveRadius * cosf(angle));
    const int16_t y = static_cast<int16_t>(ringCy + curveRadius * sinf(angle));
    drawFnChip(4 + i, x, y);
  }

  M5Dial.Display.setTextColor(WHITE, BLACK);

  drawCentered(176, 1, statusLine);
  drawCentered(194, 1, infoLine);
}

void setSpeed(int newSpeed) {
  if (newSpeed < 0) {
    newSpeed = 0;
  }
  if (newSpeed > 126) {
    newSpeed = 126;
  }

  // Keep throttle at zero until the operator explicitly clears the latch.
  if (estopLatched && newSpeed > 0) {
    setStatus("E-Stop latched: press BtnA to clear");
    return;
  }

  if (newSpeed == speed126) {
    return;
  }

  speed126 = newSpeed;
  uiDirty = true;
  if (newSpeed > 0) {
    ensureLocoAcquired();
  }
  if (!encoderUpdatingSpeed) {
    throttleDemandSpeed = newSpeed;
    syncEncoderToSpeed();
  }
  if (locoAcquired) {
    sendWt(String("M0A") + locoId + "<;>V" + String(speed126));
  }
}

void setDirection(bool forward) {
  if (directionForward == forward) {
    return;
  }
  directionForward = forward;
  uiDirty = true;
  ensureLocoAcquired();
  if (locoAcquired) {
    sendWt(String("M0A") + locoId + "<;>R" + String(directionForward ? 1 : 0));
  }
}

void toggleDirection() {
  setDirection(!directionForward);
}

void emergencyStop() {
  estopLatched = true;
  setSpeed(0);
  ensureLocoAcquired();
  if (locoAcquired) {
    sendWt(String("M0A") + locoId + "<;>X");
  }
  setStatus("E-Stop sent (latched)");
  beep();
}

void clearEmergencyStopLatch() {
  if (!estopLatched) {
    setStatus("E-Stop not latched");
    return;
  }
  estopLatched = false;
  setStatus("E-Stop latch cleared");
}

void applyStopAction(const char* statusText) {
  brakeHoldActive = false;
  brakeRecovering = false;
  brakeRecoveryTargetSpeed = 0;
  throttleDemandSpeed = 0;
  encoderUpdatingSpeed = true;
  setSpeed(0);
  encoderUpdatingSpeed = false;
  setStatus(statusText);
}

void setFunction(uint8_t fn, bool on) {
  if (fn > 28) {
    return;
  }
  functionState[fn] = on;
  uiDirty = true;
  ensureLocoAcquired();
  if (locoAcquired) {
    sendWt(String("M0A") + locoId + "<;>f" + String(on ? 1 : 0) + String(fn));
  }
}

void toggleFunction(uint8_t fn) {
  if (fn > 28) {
    return;
  }
  setFunction(fn, !functionState[fn]);
}

void acquireLoco() {
  if (!locoSelected) {
    setStatus("Select loco first");
    return;
  }
  sendWt(String("M0+") + locoId + "<;>" + locoId);
  locoAcquired = true;
  setStatus("Loco acquire requested");
}

void ensureLocoAcquired() {
  if (!locoSelected || locoAcquired) {
    return;
  }
  acquireLoco();
}

void releaseLoco() {
  sendWt(String("M0-") + locoId + "<;>r");
  locoAcquired = false;
  setStatus("Loco released");
}

void parseServerLine(const String& line) {
  if (line.isEmpty()) {
    return;
  }

  Serial.print("< ");
  Serial.println(line);

  if (line.startsWith("*")) {
    setInfo("Heartbeat window: " + line.substring(1) + "s");
    return;
  }

  if (line.startsWith("HM") || line.startsWith("Hm")) {
    setStatus(line.substring(2));
    return;
  }

  const int idx = line.indexOf("<;>V");
  if (idx > 0) {
    const int v = line.substring(idx + 4).toInt();
    if (v >= 0 && v <= 126) {
      speed126 = v;
      uiDirty = true;
    }
    return;
  }

  const int ridx = line.indexOf("<;>R");
  if (ridx > 0) {
    directionForward = line.substring(ridx + 4).toInt() != 0;
    uiDirty = true;
    return;
  }
}

void readServer() {
  if (SERIAL_OUTPUT_ONLY) {
    return;
  }

  static String rx;

  while (wtClient.connected() && wtClient.available()) {
    const char c = static_cast<char>(wtClient.read());
    if (c == '\n' || c == '\r') {
      if (!rx.isEmpty()) {
        parseServerLine(rx);
        rx = "";
      }
      continue;
    }
    rx += c;
    if (rx.length() > 300) {
      rx = "";
    }
  }
}

void connectWiFi() {
  if (SERIAL_OUTPUT_ONLY) {
    setStatus("Serial debug only mode");
    return;
  }

  setStatus("Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 30000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(ENABLE_WIFI_MODEM_SLEEP);
    if (ENABLE_WIFI_LOW_TX_POWER) {
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
    }
    setStatus(String("WiFi OK: ") + WiFi.localIP().toString());
  } else {
    setStatus("WiFi connect failed");
  }
}

void connectWiThrottle() {
  if (SERIAL_OUTPUT_ONLY) {
    setStatus("Serial debug only mode");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  setStatus("Connecting WiThrottle...");
  if (!wtClient.connect(WITHROTTLE_HOST, WITHROTTLE_PORT)) {
    setStatus("WiThrottle connect failed");
    return;
  }

  const String uniqueId = String(THROTTLE_ID_PREFIX) + WiFi.macAddress();

  sendWt(String("N") + THROTTLE_NAME);
  sendWt(String("HU") + uniqueId);
  sendWt("*+");

  if (locoSelected) {
    acquireLoco();
    setDirection(directionForward);
    setSpeed(speed126);
  }

  setStatus("WiThrottle connected");
}

void handleEncoder() {
  const int32_t signedRaw = M5Dial.Encoder.read() * ENCODER_DIRECTION_SIGN;
  if (!encoderStateInitialized) {
    encoderLastSignedRaw = signedRaw;
    encoderStateInitialized = true;
    return;
  }

  if (encoderResyncPending) {
    encoderLastSignedRaw = signedRaw;
    encoderRawAccumulator = 0;
    encoderLastMoveMs = millis();
    encoderIntentDir = 0;
    encoderOppositeRawAccum = 0;
    encoderStableDir = 0;
    encoderPendingDir = 0;
    encoderPendingDirCount = 0;
    encoderResyncPending = false;
    return;
  }

  const int32_t rawDelta = signedRaw - encoderLastSignedRaw;
  encoderLastSignedRaw = signedRaw;
  if (rawDelta == 0) {
    return;
  }
  noteUserActivity();

  const int8_t rawDir = (rawDelta > 0) ? 1 : -1;
  if (encoderIntentDir == 0) {
    encoderIntentDir = rawDir;
  }
  if (rawDir != encoderIntentDir) {
    encoderOppositeRawAccum += abs(rawDelta);
    if (encoderOppositeRawAccum < ENCODER_REVERSAL_RAW_THRESHOLD) {
      return;
    }
    encoderIntentDir = rawDir;
    encoderOppositeRawAccum = 0;
    encoderStableDir = rawDir;
    encoderPendingDir = 0;
    encoderPendingDirCount = 0;
    encoderRawAccumulator = 0;
  } else {
    encoderOppositeRawAccum = 0;
  }

  const unsigned long now = millis();
  unsigned long moveDtMs = 1000;
  if (encoderLastAcceptedStepMs != 0) {
    moveDtMs = now - encoderLastAcceptedStepMs;
  }
  encoderLastMoveMs = now;

  uint32_t adaptiveDebounceMs = ENCODER_STEP_DEBOUNCE_MS;
  int adaptiveMaxSteps = ENCODER_MAX_STEPS_PER_UPDATE;
  if (moveDtMs <= ENCODER_FAST_WINDOW_MS) {
    adaptiveDebounceMs = max<uint32_t>(1, ENCODER_STEP_DEBOUNCE_MS / 3);
    adaptiveMaxSteps += ENCODER_FAST_EXTRA_STEPS;
  } else if (moveDtMs <= ENCODER_MEDIUM_WINDOW_MS) {
    adaptiveDebounceMs = max<uint32_t>(1, ENCODER_STEP_DEBOUNCE_MS / 2);
    adaptiveMaxSteps += ENCODER_MEDIUM_EXTRA_STEPS;
  }

  encoderRawAccumulator += rawDelta;
  int32_t stepDelta = encoderRawAccumulator / ENCODER_RAW_COUNTS_PER_STEP;
  encoderRawAccumulator -= stepDelta * ENCODER_RAW_COUNTS_PER_STEP;
  if (stepDelta == 0) {
    return;
  }

  if (abs(stepDelta) < ENCODER_HYSTERESIS_COUNTS) {
    return;
  }

  if (stepDelta > adaptiveMaxSteps) {
    stepDelta = adaptiveMaxSteps;
  } else if (stepDelta < -adaptiveMaxSteps) {
    stepDelta = -adaptiveMaxSteps;
  }

  const int8_t dir = (stepDelta > 0) ? 1 : -1;
  if (encoderStableDir == 0) {
    encoderStableDir = dir;
  }
  if (dir != encoderStableDir) {
    if (encoderPendingDir != dir) {
      encoderPendingDir = dir;
      encoderPendingDirCount = 1;
    } else {
      ++encoderPendingDirCount;
    }
    if (encoderPendingDirCount < ENCODER_DIR_CHANGE_CONFIRM_STEPS) {
      return;
    }
    encoderStableDir = dir;
    encoderPendingDir = 0;
    encoderPendingDirCount = 0;
  } else {
    encoderPendingDir = 0;
    encoderPendingDirCount = 0;
  }

  if ((now - encoderLastAcceptedStepMs) < adaptiveDebounceMs && abs(stepDelta) <= 1) {
    return;
  }
  encoderLastAcceptedStepMs = now;

  // One encoder click changes one speed step.
  int newDemand = throttleDemandSpeed + static_cast<int>(stepDelta);
  if (newDemand < 0) {
    newDemand = 0;
  } else if (newDemand > 126) {
    newDemand = 126;
  }

  if (newDemand == throttleDemandSpeed) {
    return;
  }

  throttleDemandSpeed = newDemand;
  
  if (brakeHoldActive) {
    brakeRecoveryTargetSpeed = throttleDemandSpeed;
    return;
  }
  if (brakeRecovering) {
    brakeRecoveryTargetSpeed = throttleDemandSpeed;
    return;
  }

  encoderUpdatingSpeed = true;
  setSpeed(throttleDemandSpeed);
  encoderUpdatingSpeed = false;
}

void handleTouchBrake() {
  auto t = M5Dial.Touch.getDetail();
  const bool touching = (t.state == m5::touch_state_t::touch ||
                         t.state == m5::touch_state_t::touch_begin ||
                         t.state == m5::touch_state_t::hold ||
                         t.state == m5::touch_state_t::hold_begin ||
                         t.state == m5::touch_state_t::drag ||
                         t.state == m5::touch_state_t::drag_begin);
  const unsigned long now = millis();
  if (touching || t.state == m5::touch_state_t::touch_end || t.state == m5::touch_state_t::hold_end) {
    noteUserActivity();
  }

  if (touching && !brakeHoldActive && (now - lastBrakeTouchMs) > BRAKE_TOUCH_DEBOUNCE_MS) {
    brakeHoldActive = true;
    brakeRecovering = false;
    brakeStoppedToZero = false;
    brakeRecoveryTargetSpeed = throttleDemandSpeed;
    lastBrakeStepMs = now;
    lastBrakeTouchMs = now;
    setStatus("Brake hold");
  }

  if (brakeHoldActive && touching) {
    brakeRecoveryTargetSpeed = throttleDemandSpeed;
    if ((now - lastBrakeStepMs) >= BRAKE_TICK_MS) {
      lastBrakeStepMs = now;
      encoderUpdatingSpeed = true;
      setSpeed(speed126 - BRAKE_DECEL_PER_TICK);
      encoderUpdatingSpeed = false;
      if (speed126 <= 0) {
        brakeStoppedToZero = true;
      }
    }
    return;
  }

  if (brakeHoldActive && !touching) {
    if (brakeStoppedToZero && speed126 == 0) {
      brakeStoppedToZero = false;
      applyStopAction("Brake hold stop");
      return;
    }
    brakeStoppedToZero = false;
    brakeHoldActive = false;
    brakeRecovering = true;
    lastBrakeStepMs = now;
    setStatus("Brake release");
  }

  if (brakeRecovering && !brakeHoldActive) {
    brakeRecoveryTargetSpeed = throttleDemandSpeed;
    if (speed126 == brakeRecoveryTargetSpeed) {
      brakeRecovering = false;
      return;
    }
    if ((now - lastBrakeStepMs) >= BRAKE_TICK_MS) {
      lastBrakeStepMs = now;
      const int dir = (speed126 < brakeRecoveryTargetSpeed) ? 1 : -1;
      encoderUpdatingSpeed = true;
      setSpeed(speed126 + dir * BRAKE_ACCEL_PER_TICK);
      encoderUpdatingSpeed = false;
    }
  }
}

void handleButton() {
  if (M5Dial.BtnA.wasPressed()) {
    noteUserActivity();
    if (estopLatched) {
      clearEmergencyStopLatch();
      return;
    }
    if (speed126 > 0) {
      applyStopAction("Button stop");
      return;
    }
    toggleDirection();
    setStatus(directionForward ? "Direction FWD" : "Direction REV");
  }
}

void handleKeypad() {
  keypad.updateFIFO();
  const uint8_t k = keypad.getButton();
  if (k == 0x00 || k == 0xFF) {
    return;
  }
  noteUserActivity();

  const char key = static_cast<char>(k);
  setInfo(String("Key: ") + key);

  if (keypadTurnoutMode) {
    if (key >= '0' && key <= '9') {
      if (keypadTurnoutBuffer.length() < 8) {
        keypadTurnoutBuffer += key;
      }
      setStatus(String("Turnout: ") + keypadTurnoutBuffer);
      return;
    }

    if (key == '*') {
      if (keypadTurnoutBuffer.isEmpty()) {
        keypadTurnoutMode = false;
        setStatus("Turnout mode canceled");
        return;
      }
      sendWt(String("PTA2") + keypadTurnoutBuffer);
        beep();
      setStatus(String("Turnout flip ") + keypadTurnoutBuffer);
      keypadTurnoutMode = false;
      keypadTurnoutBuffer = "";
      return;
    }

    if (key == '#') {
      keypadTurnoutMode = false;
      keypadTurnoutBuffer = "";
      setStatus("Turnout mode canceled");
      return;
    }

    return;
  }

  if (keypadAddressMode) {
    if (key >= '0' && key <= '9') {
      if (keypadAddressBuffer.length() < 4) {
        keypadAddressBuffer += key;
      }
      setStatus(String("Loco input: ") + keypadAddressBuffer);
      return;
    }

    if (key == '*') {
      keypadAddressMode = false;
      keypadAddressBuffer = "";
      setStatus("Loco mode canceled");
      return;
    }

    if (key == '#') {
      if (keypadAddressBuffer.isEmpty()) {
        keypadAddressMode = false;
        setStatus("Addr input canceled");
        return;
      }
      const uint16_t addr = static_cast<uint16_t>(keypadAddressBuffer.toInt());
      const bool isLong = addr > 127;
      keypadAddressMode = false;
      setActiveLoco(addr, isLong, true, "KEYPAD");
      keypadAddressBuffer = "";
      return;
    }

    return;
  }

  switch (key) {
    case '1':
      toggleFunction(1);
      break;
    case '2':
      toggleFunction(2);
      break;
    case '3':
      toggleFunction(3);
      break;
    case '4':
      toggleFunction(4);
      break;
    case '5':
      toggleFunction(5);
      break;
    case '6':
      toggleFunction(6);
      break;
    case '7':
      toggleFunction(7);
      break;
    case '8':
      toggleFunction(8);
      break;
    case '9':
      emergencyStop();
      break;
    case '0':
      toggleFunction(0);
      break;
    case '*':
      keypadTurnoutMode = true;
      keypadTurnoutBuffer = "";
      setStatus("Turnout mode: digits then *=flip");
      break;
    case '#':
      keypadAddressMode = true;
      keypadAddressBuffer = "";
      setStatus("Loco mode: digits then #=select");
      break;
    default:
      break;
  }
}

bool resolveRfidLoco(const String& uidHex, uint16_t& outAddress, bool& outIsLong) {
  for (size_t i = 0; i < (sizeof(RFID_LOCO_MAP) / sizeof(RFID_LOCO_MAP[0])); ++i) {
    if (uidHex.equalsIgnoreCase(RFID_LOCO_MAP[i].uidHex)) {
      outAddress = RFID_LOCO_MAP[i].address;
      outIsLong = RFID_LOCO_MAP[i].isLong;
      return true;
    }
  }

  // Fallback: deterministic UID -> address conversion for unmapped tags.
  uint32_t hash = 0;
  for (size_t i = 0; i < uidHex.length(); ++i) {
    hash = (hash * 33u) ^ static_cast<uint8_t>(uidHex[i]);
  }
  outAddress = static_cast<uint16_t>((hash % 9999u) + 1u);
  outIsLong = outAddress > 127;
  return false;
}

void handleRfid() {
  if (!(M5Dial.Rfid.PICC_IsNewCardPresent() && M5Dial.Rfid.PICC_ReadCardSerial())) {
    return;
  }
  noteUserActivity();

  String uid;
  for (byte i = 0; i < M5Dial.Rfid.uid.size; i++) {
    if (M5Dial.Rfid.uid.uidByte[i] < 16) {
      uid += '0';
    }
    uid += String(M5Dial.Rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  // Ignore rapid repeats of the same tag.
  if (uid == lastRfidUid && (millis() - lastRfidMs) < 1500) {
    M5Dial.Rfid.PICC_HaltA();
    return;
  }
  lastRfidUid = uid;
  lastRfidMs = millis();

  uint16_t mappedAddress = 0;
  bool mappedIsLong = false;
  const bool mapped = resolveRfidLoco(uid, mappedAddress, mappedIsLong);

  Serial.printf("RFID UID %s -> %s%u (%s)\n", uid.c_str(), mappedIsLong ? "L" : "S", mappedAddress,
                mapped ? "mapped" : "derived");
  setActiveLoco(mappedAddress, mappedIsLong, true, mapped ? "RFID" : "RFID*" );
  M5Dial.Rfid.PICC_HaltA();
}

void printDebugHelp() {
  Serial.println("Debug commands:");
  Serial.println("  help                 - show this help");
  Serial.println("  status               - print WiFi/WiThrottle state");
  Serial.println("  acq                  - acquire configured loco");
  Serial.println("  rel                  - release configured loco");
  Serial.println("  estop                - send emergency stop");
  Serial.println("  clear                - clear E-Stop latch");
  Serial.println("  speed <0-126>        - set speed");
  Serial.println("  dir <f|r|t>          - forward/reverse/toggle");
  Serial.println("  fn <0-28> <on|off|t> - function on/off/toggle");
  Serial.println("  wt                   - reconnect WiThrottle");
  Serial.println("  wifi                 - reconnect WiFi");
  Serial.println("  send <raw>           - send raw WiThrottle line");
  Serial.println("  loco <addr> <s|l>    - set active loco and acquire");
  if (SERIAL_OUTPUT_ONLY) {
    Serial.println("  mode                 - SERIAL_OUTPUT_ONLY active");
  }
}

void printDebugStatus() {
  Serial.printf("Mode: %s\n", SERIAL_OUTPUT_ONLY ? "serial-only" : "wifi-withrottle");
  if (SERIAL_OUTPUT_ONLY) {
    Serial.printf("WiThrottle: serial-output-only\n");
    Serial.printf("Loco: %s (%s)\n", locoId.c_str(), locoAcquired ? "acquired" : "free");
    Serial.printf("Speed: %d Dir: %s\n", speed126, directionForward ? "FWD" : "REV");
    return;
  }

  Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  }
  Serial.printf("WiThrottle: %s\n", wtClient.connected() ? "connected" : "disconnected");
  Serial.printf("Loco: %s (%s)\n", locoId.c_str(), locoAcquired ? "acquired" : "free");
  Serial.printf("Speed: %d Dir: %s\n", speed126, directionForward ? "FWD" : "REV");
}

void handleSerialCommand(const String& in) {
  noteUserActivity();
  String cmd = in;
  cmd.trim();
  if (cmd.isEmpty()) {
    return;
  }

  Serial.print("# ");
  Serial.println(cmd);

  if (cmd.equalsIgnoreCase("help") || cmd == "?") {
    printDebugHelp();
    return;
  }

  if (cmd.equalsIgnoreCase("status")) {
    printDebugStatus();
    return;
  }

  if (cmd.equalsIgnoreCase("acq")) {
    acquireLoco();
    return;
  }

  if (cmd.equalsIgnoreCase("rel")) {
    releaseLoco();
    return;
  }

  if (cmd.equalsIgnoreCase("estop")) {
    emergencyStop();
    return;
  }

  if (cmd.equalsIgnoreCase("clear") || cmd.equalsIgnoreCase("estop clear")) {
    clearEmergencyStopLatch();
    return;
  }

  if (cmd.equalsIgnoreCase("wt")) {
    if (SERIAL_OUTPUT_ONLY) {
      Serial.println("! SERIAL_OUTPUT_ONLY is active");
      return;
    }
    connectWiThrottle();
    return;
  }

  if (cmd.equalsIgnoreCase("wifi")) {
    if (SERIAL_OUTPUT_ONLY) {
      Serial.println("! SERIAL_OUTPUT_ONLY is active");
      return;
    }
    connectWiFi();
    return;
  }

  if (cmd.startsWith("send ")) {
    const String raw = cmd.substring(5);
    sendWt(raw);
    return;
  }

  if (cmd.startsWith("speed ")) {
    setSpeed(cmd.substring(6).toInt());
    return;
  }

  if (cmd.startsWith("dir ")) {
    const String arg = cmd.substring(4);
    if (arg.equalsIgnoreCase("f")) {
      setDirection(true);
      return;
    }
    if (arg.equalsIgnoreCase("r")) {
      setDirection(false);
      return;
    }
    if (arg.equalsIgnoreCase("t")) {
      toggleDirection();
      return;
    }
  }

  if (cmd.startsWith("fn ")) {
    char buf[80] = {0};
    cmd.toCharArray(buf, sizeof(buf));
    int fn = -1;
    char state[16] = {0};
    if (sscanf(buf, "fn %d %15s", &fn, state) == 2) {
      if (fn < 0 || fn > 28) {
        Serial.println("! Function must be 0..28");
        return;
      }
      String stateArg = String(state);
      stateArg.toLowerCase();
      if (stateArg == "on") {
        setFunction(static_cast<uint8_t>(fn), true);
        return;
      }
      if (stateArg == "off") {
        setFunction(static_cast<uint8_t>(fn), false);
        return;
      }
      if (stateArg == "t") {
        toggleFunction(static_cast<uint8_t>(fn));
        return;
      }
    }
  }

  if (cmd.startsWith("loco ")) {
    char buf[80] = {0};
    cmd.toCharArray(buf, sizeof(buf));
    int addr = -1;
    char type[8] = {0};
    if (sscanf(buf, "loco %d %7s", &addr, type) == 2) {
      String typeArg = String(type);
      typeArg.toLowerCase();
      const bool isLong = (typeArg == "l");
      if (addr > 0 && addr <= 9999 && (typeArg == "s" || typeArg == "l")) {
        setActiveLoco(static_cast<uint16_t>(addr), isLong, true, "SERIAL");
        return;
      }
    }
    Serial.println("! Usage: loco <1-9999> <s|l>");
    return;
  }

  Serial.println("! Unknown command. Type 'help'.");
}

void readSerialCommands() {
  static String rx;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (!rx.isEmpty()) {
        handleSerialCommand(rx);
        rx = "";
      }
    } else {
      rx += c;
      if (rx.length() > 120) {
        rx = "";
      }
    }
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  M5Dial.begin(true, true);
  setCpuFrequencyMhz(CPU_FREQ_MHZ);
  M5Dial.Speaker.begin();
  M5Dial.Speaker.setVolume(240);
  M5Dial.Display.setRotation(0);
  M5Dial.Display.setBrightness(DISPLAY_BRIGHTNESS_ACTIVE);
  lastUserActivityMs = millis();
  backlightMode = 0;

  updateLocoLabel();

  Wire.begin();
  if (keypad.begin(Wire, KEYPAD_I2C_ADDR)) {
    setStatus("Qwiic keypad connected");
  } else {
    setStatus("Qwiic keypad NOT found");
  }

  M5Dial.Encoder.write(0);
  syncEncoderToSpeed();
  drawUi(true);

  connectWiFi();
  connectWiThrottle();
  printDebugHelp();
  drawUi(true);
}

void loop() {
  M5Dial.update();
  updatePowerState();

  if (!SERIAL_OUTPUT_ONLY && !wifiSuspendedForPower) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    if (WiFi.status() == WL_CONNECTED && !wtClient.connected()) {
      connectWiThrottle();
    }
  }

  readSerialCommands();
  handleEncoder();
  handleTouchBrake();
  handleButton();
  handleKeypad();
  handleRfid();
  readServer();

  if (!SERIAL_OUTPUT_ONLY && wtClient.connected() && (millis() - lastHeartbeatMs) > 2000) {
    sendWt("*");
    lastHeartbeatMs = millis();
  }

  if (!SERIAL_OUTPUT_ONLY && (millis() - lastServerActivityMs) > 15000 && wtClient.connected()) {
    setInfo("Waiting for server data");
  }

  if (uiDirty && backlightMode != 2) {
    drawUi();
  }
  if (backlightMode == 0) {
    delay(LOOP_DELAY_ACTIVE_MS);
  } else if (backlightMode == 1) {
    delay(LOOP_DELAY_DIM_MS);
  } else {
    delay(LOOP_DELAY_OFF_MS);
  }
}

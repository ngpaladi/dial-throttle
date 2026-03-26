#include <Arduino.h>
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

bool keypadAddressMode = false;
String keypadAddressBuffer;
bool keypadTurnoutMode = false;
String keypadTurnoutBuffer;
String lastRfidUid;
unsigned long lastRfidMs = 0;

unsigned long lastHeartbeatMs = 0;
unsigned long lastStatusDrawMs = 0;
unsigned long lastServerActivityMs = 0;

String statusLine = "Booting...";
String infoLine = "";

void setStatus(const String& s);
void sendWt(const String& line);
void setSpeed(int newSpeed);

void syncEncoderToSpeed() {
  const int32_t targetSteps = speed126 / ENCODER_SPEED_STEP;
  const int32_t rawTarget = targetSteps * ENCODER_RAW_COUNTS_PER_STEP * ENCODER_DIRECTION_SIGN;
  M5Dial.Encoder.write(rawTarget);
}

String buildLocoId(uint16_t address, bool isLong) {
  return String(isLong ? "L" : "S") + String(address);
}

void updateLocoLabel() {
  locoId = buildLocoId(activeLocoAddress, activeLocoIsLong);
}

void setActiveLoco(uint16_t address, bool isLong, bool autoAcquire, const String& source) {
  if (address == 0 || address > 9999) {
    setStatus("Invalid loco address");
    return;
  }

  if (locoAcquired) {
    sendWt(String("M0-") + locoId + "<;>r");
    locoAcquired = false;
  }

  activeLocoAddress = address;
  activeLocoIsLong = isLong;
  updateLocoLabel();
  setStatus(String("Loco set ") + locoId + " via " + source);

  if (autoAcquire) {
    sendWt(String("M0+") + locoId + "<;>" + locoId);
    locoAcquired = true;
    setStatus(String("Loco acquired ") + locoId + " via " + source);
  }
}

void setStatus(const String& s) {
  statusLine = s;
  Serial.println(s);
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
  const unsigned long now = millis();
  if (!force && (now - lastStatusDrawMs) < 150) {
    return;
  }
  lastStatusDrawMs = now;

  M5Dial.Display.fillScreen(BLACK);
  M5Dial.Display.setTextColor(WHITE, BLACK);
  const int16_t screenW = M5Dial.Display.width();

  auto drawCentered = [&](int16_t y, uint8_t textSize, const String& text) {
    M5Dial.Display.setTextSize(textSize);
    const int16_t x = (screenW - M5Dial.Display.textWidth(text)) / 2;
    M5Dial.Display.setCursor(max<int16_t>(0, x), y);
    M5Dial.Display.print(text);
  };

  // Keep content in the central area so round-edge clipping does not cut text.
  drawCentered(26, 2, "WiThrottle");
  drawCentered(56, 2, String("Loco: ") + locoId);
  drawCentered(86, 2, String("Speed: ") + String(speed126));
  drawCentered(116, 2, String("Dir: ") + (directionForward ? "FWD" : "REV"));
  drawCentered(146, 2, String("State: ") + (locoAcquired ? "ACQ" : "FREE"));

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
  if (newSpeed == speed126) {
    return;
  }

  speed126 = newSpeed;
  if (!encoderUpdatingSpeed) {
    throttleDemandSpeed = newSpeed;
    syncEncoderToSpeed();
  }
  if (locoAcquired) {
    sendWt(String("M0A") + locoId + "<;>V" + String(speed126));
  }
}

void setDirection(bool forward) {
  directionForward = forward;
  if (locoAcquired) {
    sendWt(String("M0A") + locoId + "<;>R" + String(directionForward ? 1 : 0));
  }
}

void toggleDirection() {
  setDirection(!directionForward);
}

void emergencyStop() {
  setSpeed(0);
  if (locoAcquired) {
    sendWt(String("M0A") + locoId + "<;>X");
    setStatus("E-Stop sent");
  }
}

void setFunction(uint8_t fn, bool on) {
  if (fn > 28) {
    return;
  }
  functionState[fn] = on;
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
  sendWt(String("M0+") + locoId + "<;>" + locoId);
  locoAcquired = true;
  setStatus("Loco acquire requested");
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
    infoLine = "Heartbeat window: " + line.substring(1) + "s";
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
    }
    return;
  }

  const int ridx = line.indexOf("<;>R");
  if (ridx > 0) {
    directionForward = line.substring(ridx + 4).toInt() != 0;
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

  acquireLoco();
  setDirection(directionForward);
  setSpeed(speed126);

  setStatus("WiThrottle connected");
}

void handleEncoder() {
  static bool initialized = false;
  static int32_t lastSignedRaw = 0;
  static int32_t rawAccumulator = 0;

  const int32_t signedRaw = M5Dial.Encoder.read() * ENCODER_DIRECTION_SIGN;
  if (!initialized) {
    lastSignedRaw = signedRaw;
    initialized = true;
    return;
  }

  const int32_t rawDelta = signedRaw - lastSignedRaw;
  lastSignedRaw = signedRaw;
  if (rawDelta == 0) {
    return;
  }

  rawAccumulator += rawDelta;
  int32_t stepDelta = rawAccumulator / ENCODER_RAW_COUNTS_PER_STEP;
  rawAccumulator -= stepDelta * ENCODER_RAW_COUNTS_PER_STEP;
  if (stepDelta == 0) {
    return;
  }

  if (abs(stepDelta) < ENCODER_HYSTERESIS_COUNTS) {
    return;
  }

  int newDemand = throttleDemandSpeed + static_cast<int>(stepDelta) * ENCODER_SPEED_STEP;
  if (newDemand < 0) {
    newDemand = 0;
  }
  if (newDemand > 126) {
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

  if (touching && !brakeHoldActive && (now - lastBrakeTouchMs) > BRAKE_TOUCH_DEBOUNCE_MS) {
    brakeHoldActive = true;
    brakeRecovering = false;
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
    }
    return;
  }

  if (brakeHoldActive && !touching) {
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
    toggleDirection();
  }
}

void handleKeypad() {
  keypad.updateFIFO();
  const uint8_t k = keypad.getButton();
  if (k == 0x00 || k == 0xFF) {
    return;
  }

  const char key = static_cast<char>(k);
  infoLine = String("Key: ") + key;

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
      toggleFunction(0); // Headlight
      break;
    case '2':
      toggleFunction(1); // Bell
      break;
    case '3':
      toggleFunction(2); // Horn
      break;
    case '4':
      emergencyStop();
      break;
    case '5':
      setSpeed(0);
      break;
    case '6':
      toggleDirection();
      break;
    case '7':
      releaseLoco();
      break;
    case '8':
      acquireLoco();
      break;
    case '9':
      toggleFunction(5);
      break;
    case '0':
      toggleFunction(8);
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
  M5Dial.Display.setRotation(0);
  M5Dial.Display.setBrightness(120);

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

  if (!SERIAL_OUTPUT_ONLY) {
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
    infoLine = "Waiting for server data";
  }

  drawUi();
  delay(10);
}

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Keypad.h>
#include <time.h>

// PRIVATE SETTINGS

const char* wifiName = "Big 14";
const char* wifiPassword = "Tk2k03!!";

const char* dashboardUsername = "admin";
const char* dashboardPassword = "2026";

const char* appsScriptURL =
  "https://script.google.com/macros/s/AKfycbwHZClneAXJgW0b_tgE5CV2OWRwBc2fRyv2D-zhUd2l1Ag349Ke0RZiEaB5k1Qtbspa/exec";

const char* secretToken =
  "2026Gradboy";

const char* cameraIP = "172.20.10.5";

const char* ntpServer = "pool.ntp.org";

const char* timeZone =
  "EST5EDT,M3.2.0/2,M11.1.0/2";

// WEB SERVER

WebServer server(80);

// SENSOR AND OUTPUT PINS

const int frontDoorPin = 16;
const int livingRoomPin = 17;
const int backDoorPin = 18;
const int garageDoorPin = 4;

// PIR motion sensor
const int motionPin = 19;

// Alarm output
const int buzzerPin = 5;

// Status LEDs
const int greenLED = 21;
const int yellowLED = 22;
const int redLED = 23;

// Active-LOW buzzer module
const int BUZZER_ON = LOW;
const int BUZZER_OFF = HIGH;

// KEYPAD SETUP

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {
  25, 26, 27, 14
};

byte colPins[COLS] = {
  12, 13, 32, 33
};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

// SECURITY SETTINGS

const String correctPIN = "1234";
String enteredPIN = "";

const unsigned long EXIT_DELAY_MS = 10000;

// SYSTEM STATES

enum SystemState {
  DISARMED,
  EXIT_DELAY_STATE,
  ARMED,
  ALARM
};

enum ArmMode {
  NO_ARM_MODE,
  STAY_MODE,
  AWAY_MODE
};

SystemState systemState = DISARMED;

ArmMode armMode = NO_ARM_MODE;
ArmMode pendingArmMode = NO_ARM_MODE;

// SENSOR STATES

bool frontDoorOpen = false;
bool livingRoomOpen = false;
bool backDoorOpen = false;
bool garageDoorOpen = false;
bool motionDetected = false;

String triggeredZone = "None";

// TIMING VARIABLES

unsigned long exitStartTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastCountdownPrint = 0;
unsigned long lastWiFiCheck = 0;

bool yellowLEDState = false;
bool redLEDState = false;
 
// EVENT LOG

const int MAX_EVENTS = 20;

String eventLog[MAX_EVENTS];
int eventCount = 0;

// FUNCTION DECLARATIONS

void connectToWiFi();
void checkWiFiConnection();

void setupTime();
String getTimeOnly();
String getDateOnly();
String getDateTime();

void setupWebServer();
bool requireDashboardLogin();
void handleDashboard();

void handleRemoteAway();
void handleRemoteStay();
void handleRemoteDisarm();
void handleClearLog();
void redirectToDashboard();

void readSensors();
void readKeypad();
void processPIN(ArmMode requestedMode);

void startExitDelay(ArmMode requestedMode);
void cancelArming();
void printExitCountdown();

void checkArmedSensors();
void triggerAlarm(const String &sensorName);
void disarmSystem();

void sendEmailAlert(
  const String &eventName,
  const String &modeName,
  const String &sensorName
);

String urlEncode(const String &value);

void updateOutputs();
void logEvent(const String &message);

String getStateText();
String getStateClass();
String getArmModeText();

String openClosedText(bool zoneOpen);
String zoneColorClass(bool zoneOpen);

String buildZoneCard(
  const String &zoneName,
  const String &statusText,
  const String &colorClass
);
 
// SETUP 

void setup() {
  Serial.begin(115200);

  pinMode(frontDoorPin, INPUT_PULLUP);
  pinMode(livingRoomPin, INPUT_PULLUP);
  pinMode(backDoorPin, INPUT_PULLUP);
  pinMode(garageDoorPin, INPUT_PULLUP);

  pinMode(motionPin, INPUT);

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, BUZZER_OFF);

  pinMode(greenLED, OUTPUT);
  pinMode(yellowLED, OUTPUT);
  pinMode(redLED, OUTPUT);

  updateOutputs();

  connectToWiFi();
  setupTime();
  setupWebServer();

  logEvent("SYSTEM POWERED ON");
  logEvent("SYSTEM DISARMED");

  Serial.println();
  Serial.println("================================");
  Serial.println("SMART HOME SECURITY SYSTEM");
  Serial.println("================================");
  Serial.println("1234# = ARM AWAY");
  Serial.println("1234* = ARM STAY");
  Serial.println("* clears a partial or wrong PIN");

  Serial.print("Dashboard: http://");
  Serial.println(WiFi.localIP());

  Serial.print("Camera: http://");
  Serial.println(cameraIP);
}
 
// MAIN LOOP
 
void loop() {
  server.handleClient();

  readKeypad();
  readSensors();
  checkWiFiConnection();

  switch (systemState) {

    case DISARMED:
      digitalWrite(buzzerPin, BUZZER_OFF);
      break;

    case EXIT_DELAY_STATE:
      digitalWrite(buzzerPin, BUZZER_OFF);

      printExitCountdown();

      if (
        millis() - exitStartTime >=
        EXIT_DELAY_MS
      ) {
        systemState = ARMED;

        armMode = pendingArmMode;
        pendingArmMode = NO_ARM_MODE;

        if (armMode == AWAY_MODE) {
          logEvent("AWAY MODE ARMED");
        }
        else {
          logEvent("STAY MODE ARMED");
        }
      }

      break;

    case ARMED:
      checkArmedSensors();
      break;

    case ALARM:
      digitalWrite(buzzerPin, BUZZER_ON);
      break;
  }

  updateOutputs();

  delay(20);
}

// READ ALL SENSORS 

void readSensors() {

  frontDoorOpen =
    digitalRead(frontDoorPin) == HIGH;

  livingRoomOpen =
    digitalRead(livingRoomPin) == HIGH;

  backDoorOpen =
    digitalRead(backDoorPin) == HIGH;

  garageDoorOpen =
    digitalRead(garageDoorPin) == HIGH;

  motionDetected =
    digitalRead(motionPin) == HIGH;
}
 
// CHECK ARMED ZONES
 
void checkArmedSensors() {

  if (frontDoorOpen) {
    logEvent("FRONT DOOR OPEN");
    triggerAlarm("Front Door");
  }

  else if (livingRoomOpen) {
    logEvent("LIVING ROOM WINDOW OPEN");
    triggerAlarm("Living Room Window");
  }

  else if (backDoorOpen) {
    logEvent("BACK DOOR OPEN");
    triggerAlarm("Back Door");
  }

  else if (garageDoorOpen) {
    logEvent("GARAGE DOOR OPEN");
    triggerAlarm("Garage Door");
  }

  // Motion is active only in Away mode.
  else if (
    armMode == AWAY_MODE &&
    motionDetected
  ) {
    logEvent("MOTION DETECTED");
    triggerAlarm("Living Room Motion");
  }
}
 
// WI-FI CONNECTION 

void connectToWiFi() {
  WiFi.mode(WIFI_STA);

  WiFi.begin(
    wifiName,
    wifiPassword
  );

  Serial.print("Connecting to Wi-Fi");

  unsigned long started = millis();

  while (
    WiFi.status() != WL_CONNECTED
  ) {
    delay(500);
    Serial.print(".");

    if (
      millis() - started > 20000
    ) {
      Serial.println();
      Serial.println(
        "Wi-Fi connection timed out."
      );

      return;
    }
  }

  Serial.println();
  Serial.println("Wi-Fi connected.");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// WI-FI RECONNECTION

void checkWiFiConnection() {
  if (
    millis() - lastWiFiCheck <
    10000
  ) {
    return;
  }

  lastWiFiCheck = millis();

  if (
    WiFi.status() != WL_CONNECTED
  ) {
    Serial.println(
      "Wi-Fi disconnected. Reconnecting..."
    );

    WiFi.disconnect();

    WiFi.begin(
      wifiName,
      wifiPassword
    );
  }
}

// REAL-TIME CLOCK 

void setupTime() {
  if (
    WiFi.status() != WL_CONNECTED
  ) {
    return;
  }

  configTzTime(
    timeZone,
    ntpServer
  );

  struct tm timeInfo;

  unsigned long started = millis();

  while (
    !getLocalTime(&timeInfo)
  ) {
    delay(500);

    if (
      millis() - started > 15000
    ) {
      return;
    }
  }

  Serial.print("Current time: ");
  Serial.println(getDateTime());
}

String getTimeOnly() {
  struct tm timeInfo;

  if (
    !getLocalTime(&timeInfo)
  ) {
    return "Time unavailable";
  }

  char buffer[20];

  strftime(
    buffer,
    sizeof(buffer),
    "%I:%M:%S %p",
    &timeInfo
  );

  String result = String(buffer);

  if (result.startsWith("0")) {
    result.remove(0, 1);
  }

  return result;
}

String getDateOnly() {
  struct tm timeInfo;

  if (
    !getLocalTime(&timeInfo)
  ) {
    return "Date unavailable";
  }

  char buffer[40];

  strftime(
    buffer,
    sizeof(buffer),
    "%A, %B %d, %Y",
    &timeInfo
  );

  return String(buffer);
}

String getDateTime() {
  return
    getDateOnly() +
    " " +
    getTimeOnly();
}

// WEB SERVER SETUP 

void setupWebServer() {
  server.on(
    "/",
    HTTP_GET,
    handleDashboard
  );

  server.on(
    "/arm-away",
    HTTP_POST,
    handleRemoteAway
  );

  server.on(
    "/arm-stay",
    HTTP_POST,
    handleRemoteStay
  );

  server.on(
    "/disarm",
    HTTP_POST,
    handleRemoteDisarm
  );

  server.on(
    "/clear-log",
    HTTP_POST,
    handleClearLog
  );

  server.onNotFound([]() {
    if (!requireDashboardLogin()) {
      return;
    }

    server.send(
      404,
      "text/plain",
      "Page not found"
    );
  });

  server.begin();

  Serial.println("Web server started.");
}

// DASHBOARD LOGIN 

bool requireDashboardLogin() {
  if (
    server.authenticate(
      dashboardUsername,
      dashboardPassword
    )
  ) {
    return true;
  }

  server.requestAuthentication();

  return false;
}

// DASHBOARD PAGE 

void handleDashboard() {
  if (!requireDashboardLogin()) {
    return;
  }

  String stateText = getStateText();
  String stateClass = getStateClass();
  String modeText = getArmModeText();

  String motionStatus;
  String motionClass;

  if (
    systemState == ARMED &&
    armMode == STAY_MODE
  ) {
    motionStatus = "IGNORED IN STAY";
    motionClass = "ignored";
  }
  else {
    motionStatus =
      motionDetected
      ? "MOTION DETECTED"
      : "CLEAR";

    motionClass =
      motionDetected
      ? "danger"
      : "good";
  }

  String alarmStatus =
    systemState == ALARM
    ? "ACTIVE"
    : "OFF";

  String alarmClass =
    systemState == ALARM
    ? "danger"
    : "good";

  String wifiStatus =
    WiFi.status() == WL_CONNECTED
    ? "CONNECTED"
    : "OFFLINE";

  String wifiClass =
    WiFi.status() == WL_CONNECTED
    ? "good"
    : "danger";

  String html = R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">

<meta
  name="viewport"
  content="width=device-width, initial-scale=1.0"
>

<meta http-equiv="refresh" content="2">

<title>
Smart Home Security
</title>

<style>

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  background: #08111f;
  color: white;
  font-family: Arial, Helvetica, sans-serif;
}

.page {
  width: 100%;
  max-width: 1100px;
  margin: auto;
  padding: 20px;
}

.topbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 20px;

  background: #111c2e;
  border: 1px solid #26354d;
  border-radius: 18px;

  padding: 18px 22px;
  margin-bottom: 18px;
}

.brand h1 {
  margin: 0;
  font-size: 27px;
}

.brand p {
  margin: 5px 0 0;
  color: #94a3b8;
}

.datetime {
  text-align: right;
}

.date {
  color: #94a3b8;
  font-size: 14px;
}

.time {
  font-size: 25px;
  font-weight: bold;
  margin-top: 5px;
}

.status-banner {
  padding: 22px;
  text-align: center;
  border-radius: 18px;
  margin-bottom: 18px;

  font-weight: bold;
  font-size: 31px;
}

.disarmed {
  background: #166534;
}

.exit {
  background: #a16207;
}

.stay {
  background: #1d4ed8;
}

.away {
  background: #991b1b;
}

.alarm {
  background: #dc2626;
  animation: alarmFlash 0.6s infinite;
}

@keyframes alarmFlash {
  50% {
    opacity: 0.45;
  }
}

.mode-line {
  margin-top: 9px;
  font-size: 16px;
  font-weight: normal;
}

.grid {
  display: grid;
  grid-template-columns: 1.15fr 0.85fr;
  gap: 18px;
  align-items: start;
}

.card {
  background: #111c2e;
  border: 1px solid #26354d;
  border-radius: 18px;

  padding: 20px;
  margin-bottom: 18px;

  box-shadow:
    0 7px 20px
    rgba(0, 0, 0, 0.25);
}

.card h2 {
  margin-top: 0;
  margin-bottom: 16px;
  font-size: 21px;
}

.camera-frame {
  width: 100%;
  background: black;

  border-radius: 14px;
  overflow: hidden;

  border: 2px solid #334155;
}

.camera-frame img {
  display: block;
  width: 100%;

  min-height: 260px;
  max-height: 520px;

  object-fit: contain;
  background: black;
}

.camera-alarm {
  border: 3px solid #ef4444;

  box-shadow:
    0 0 25px
    rgba(239, 68, 68, 0.85);
}

.camera-link {
  display: block;

  margin-top: 12px;
  padding: 13px;

  text-align: center;
  text-decoration: none;

  color: white;
  background: #0369a1;

  border-radius: 10px;
  font-weight: bold;
}

.zone-list {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}

.zone {
  background: #1e293b;
  border-radius: 13px;
  padding: 15px;
  border: 1px solid #334155;
}

.zone-name {
  color: #cbd5e1;
  font-size: 14px;
}

.zone-value {
  margin-top: 7px;
  font-size: 18px;
  font-weight: bold;
}

.good {
  color: #4ade80;
}

.danger {
  color: #f87171;
}

.ignored {
  color: #facc15;
}

.controls button,
.clear-button {
  width: 100%;

  border: none;
  border-radius: 11px;

  padding: 15px;
  margin-top: 10px;

  color: white;
  font-size: 17px;
  font-weight: bold;

  cursor: pointer;
}

.away-button {
  background: #b91c1c;
}

.stay-button {
  background: #1d4ed8;
}

.disarm-button {
  background: #15803d;
}

.clear-button {
  background: #475569;
}

.event {
  background: #1e293b;

  border-left:
    4px solid #38bdf8;

  border-radius: 9px;

  padding: 11px 13px;
  margin-top: 9px;

  font-size: 14px;
}

.triggered-zone {
  margin-top: 10px;
  font-size: 17px;
  font-weight: normal;
}

.footer {
  text-align: center;
  color: #64748b;
  font-size: 13px;
  padding: 8px;
}

@media (max-width: 800px) {

  .grid {
    grid-template-columns: 1fr;
  }

  .topbar {
    flex-direction: column;
    text-align: center;
  }

  .datetime {
    text-align: center;
  }
}

@media (max-width: 520px) {

  .zone-list {
    grid-template-columns: 1fr;
  }

  .status-banner {
    font-size: 24px;
  }

  .page {
    padding: 12px;
  }
}

</style>

</head>

<body>

<div class="page">

<div class="topbar">

<div class="brand">

<h1>
Smart Home Security
</h1>

<p>
ESP32 Security and Monitoring System
</p>

</div>

<div class="datetime">

<div class="date">
)rawliteral";

  html += getDateOnly();

  html += R"rawliteral(
</div>

<div class="time">
)rawliteral";

  html += getTimeOnly();

  html += R"rawliteral(
</div>

</div>

</div>

<div class="status-banner )rawliteral";

  html += stateClass;

  html += R"rawliteral(">
)rawliteral";

  html += stateText;

  html += R"rawliteral(

<div class="mode-line">

Current Mode:
)rawliteral";

  html += modeText;

  html += R"rawliteral(
</div>
)rawliteral";

  if (systemState == ALARM) {
    html +=
      "<div class='triggered-zone'>"
      "Triggered By: " +
      triggeredZone +
      "</div>";
  }

  html += R"rawliteral(
</div>

<div class="grid">

<div>

<div class="card">

<h2>
Live Security Camera
</h2>

<div class="camera-frame )rawliteral";

  if (systemState == ALARM) {
    html += "camera-alarm";
  }

  html += R"rawliteral(">

<img
  src="http://)rawliteral";

  html += cameraIP;

  html += R"rawliteral(:81/stream"
  alt="ESP32-CAM live stream"
>

</div>

<a
  class="camera-link"
  href="http://)rawliteral";

  html += cameraIP;

  html += R"rawliteral("
  target="_blank"
>

OPEN FULL CAMERA CONTROLS

</a>

</div>

<div class="card">

<h2>
Recent Events
</h2>
)rawliteral";

  if (eventCount == 0) {
    html +=
      "<div class='event'>"
      "No events recorded."
      "</div>";
  }

  for (
    int i = 0;
    i < eventCount;
    i++
  ) {
    html +=
      "<div class='event'>";

    html += eventLog[i];

    html += "</div>";
  }

  html += R"rawliteral(

<form
  action="/clear-log"
  method="POST"
>

<button
  class="clear-button"
  type="submit"
>

CLEAR EVENT LOG

</button>

</form>

</div>

</div>

<div>

<div class="card">

<h2>
Security Zones
</h2>

<div class="zone-list">
)rawliteral";

  html += buildZoneCard(
    "Zone 1 — Front Door",
    openClosedText(frontDoorOpen),
    zoneColorClass(frontDoorOpen)
  );

  html += buildZoneCard(
    "Zone 2 — Living Room Window",
    openClosedText(livingRoomOpen),
    zoneColorClass(livingRoomOpen)
  );

  html += buildZoneCard(
    "Zone 3 — Back Door",
    openClosedText(backDoorOpen),
    zoneColorClass(backDoorOpen)
  );

  html += buildZoneCard(
    "Zone 4 — Garage Door",
    openClosedText(garageDoorOpen),
    zoneColorClass(garageDoorOpen)
  );

  html += buildZoneCard(
    "Zone 5 — Living Room Motion",
    motionStatus,
    motionClass
  );

  html += buildZoneCard(
    "Alarm Output",
    alarmStatus,
    alarmClass
  );

  html += buildZoneCard(
    "Wi-Fi Connection",
    wifiStatus,
    wifiClass
  );

  html += R"rawliteral(
</div>

</div>

<div class="card controls">

<h2>
System Controls
</h2>

<form
  action="/arm-away"
  method="POST"
>

<button
  class="away-button"
  type="submit"
>

ARM AWAY

</button>

</form>

<form
  action="/arm-stay"
  method="POST"
>

<button
  class="stay-button"
  type="submit"
>

ARM STAY

</button>

</form>

<form
  action="/disarm"
  method="POST"
>

<button
  class="disarm-button"
  type="submit"
>

DISARM SYSTEM

</button>

</form>

</div>

<div class="card">

<h2>
System Information
</h2>

<div class="zone">

<div class="zone-name">
Main Controller
</div>

<div class="zone-value good">
ESP32-WROOM-32
</div>

</div>

<div
  class="zone"
  style="margin-top: 12px;"
>

<div class="zone-name">
Camera
</div>

<div class="zone-value good">
ESP32-CAM CONFIGURED
</div>

</div>

<div
  class="zone"
  style="margin-top: 12px;"
>

<div class="zone-name">
Notification Service
</div>

<div class="zone-value good">
EMAIL ALERTS ENABLED
</div>

</div>

</div>

</div>

</div>

<div class="footer">

Local smart-security dashboard hosted by ESP32

</div>

</div>

</body>

</html>
)rawliteral";

  server.send(
    200,
    "text/html",
    html
  );
}

// DASHBOARD ZONE CARD BUILDER

String buildZoneCard(
  const String &zoneName,
  const String &statusText,
  const String &colorClass
) {
  String card = "";

  card += "<div class='zone'>";

  card +=
    "<div class='zone-name'>" +
    zoneName +
    "</div>";

  card +=
    "<div class='zone-value " +
    colorClass +
    "'>";

  card += statusText;

  card += "</div>";
  card += "</div>";

  return card;
}

String openClosedText(
  bool zoneOpen
) {
  return
    zoneOpen
    ? "OPEN"
    : "CLOSED";
}

String zoneColorClass(
  bool zoneOpen
) {
  return
    zoneOpen
    ? "danger"
    : "good";
}

// REMOTE DASHBOARD CONTROLS 

void handleRemoteAway() {
  if (!requireDashboardLogin()) {
    return;
  }

  if (systemState == DISARMED) {
    logEvent(
      "REMOTE ARM AWAY COMMAND"
    );

    startExitDelay(
      AWAY_MODE
    );
  }

  redirectToDashboard();
}

void handleRemoteStay() {
  if (!requireDashboardLogin()) {
    return;
  }

  if (systemState == DISARMED) {
    logEvent(
      "REMOTE ARM STAY COMMAND"
    );

    startExitDelay(
      STAY_MODE
    );
  }

  redirectToDashboard();
}

void handleRemoteDisarm() {
  if (!requireDashboardLogin()) {
    return;
  }

  if (systemState != DISARMED) {
    logEvent(
      "REMOTE DISARM COMMAND"
    );

    disarmSystem();
  }

  redirectToDashboard();
}

void handleClearLog() {
  if (!requireDashboardLogin()) {
    return;
  }

  eventCount = 0;

  logEvent(
    "EVENT LOG CLEARED"
  );

  redirectToDashboard();
}

void redirectToDashboard() {
  server.sendHeader(
    "Location",
    "/"
  );

  server.send(303);
}

// KEYPAD READING

void readKeypad() {
  char key = keypad.getKey();

  if (!key) {
    return;
  }

  if (
    key >= '0' &&
    key <= '9'
  ) {
    if (
      enteredPIN.length() < 8
    ) {
      enteredPIN += key;
    }

    Serial.print("PIN: ");

    for (
      unsigned int i = 0;
      i < enteredPIN.length();
      i++
    ) {
      Serial.print("*");
    }

    Serial.println();

    return;
  }

  if (key == '#') {
    processPIN(AWAY_MODE);
    return;
  }

  if (key == '*') {
    if (
      enteredPIN == correctPIN
    ) {
      processPIN(STAY_MODE);
    }
    else {
      enteredPIN = "";

      Serial.println(
        "PIN CLEARED"
      );
    }

    return;
  }
}

// PIN PROCESSING 

void processPIN(
  ArmMode requestedMode
) {
  if (
    enteredPIN != correctPIN
  ) {
    logEvent(
      "INCORRECT PIN ATTEMPT"
    );

    enteredPIN = "";

    return;
  }

  if (systemState == DISARMED) {
    startExitDelay(
      requestedMode
    );
  }

  else if (
    systemState ==
    EXIT_DELAY_STATE
  ) {
    cancelArming();
  }

  else if (
    systemState == ARMED ||
    systemState == ALARM
  ) {
    disarmSystem();
  }

  enteredPIN = "";
}

// EXIT DELAY 

void startExitDelay(
  ArmMode requestedMode
) {
  pendingArmMode =
    requestedMode;

  systemState =
    EXIT_DELAY_STATE;

  exitStartTime =
    millis();

  lastCountdownPrint = 0;

  triggeredZone = "None";

  if (
    requestedMode == AWAY_MODE
  ) {
    logEvent(
      "AWAY MODE EXIT DELAY STARTED"
    );
  }
  else {
    logEvent(
      "STAY MODE EXIT DELAY STARTED"
    );
  }
}

void cancelArming() {
  systemState = DISARMED;

  armMode = NO_ARM_MODE;
  pendingArmMode = NO_ARM_MODE;

  triggeredZone = "None";

  digitalWrite(
    buzzerPin,
    BUZZER_OFF
  );

  logEvent("ARMING CANCELLED");
  logEvent("SYSTEM DISARMED");
}

void printExitCountdown() {
  unsigned long elapsed =
    millis() - exitStartTime;

  unsigned long
    secondsRemaining;

  if (
    elapsed >= EXIT_DELAY_MS
  ) {
    secondsRemaining = 0;
  }
  else {
    secondsRemaining =
      (
        EXIT_DELAY_MS -
        elapsed +
        999
      ) / 1000;
  }

  if (
    millis() -
    lastCountdownPrint >= 1000
    ||
    lastCountdownPrint == 0
  ) {
    lastCountdownPrint =
      millis();

    Serial.print("Arming in ");
    Serial.print(secondsRemaining);
    Serial.println(" seconds");
  }
}

// ALARM CONTROL

void triggerAlarm(
  const String &sensorName
) {
  if (systemState == ALARM) {
    return;
  }

  systemState = ALARM;

  triggeredZone = sensorName;

  digitalWrite(
    buzzerPin,
    BUZZER_ON
  );

  logEvent(
    "ALARM TRIGGERED: " +
    sensorName
  );

  sendEmailAlert(
    "Alarm Triggered",
    getArmModeText(),
    sensorName
  );
}

void disarmSystem() {
  bool alarmWasActive =
    systemState == ALARM;

  systemState = DISARMED;

  armMode = NO_ARM_MODE;
  pendingArmMode = NO_ARM_MODE;

  digitalWrite(
    buzzerPin,
    BUZZER_OFF
  );

  if (alarmWasActive) {
    logEvent(
      "ALARM RESET BY CORRECT PIN"
    );
  }

  triggeredZone = "None";

  logEvent("SYSTEM DISARMED");
}

// EMAIL ALERTS

void sendEmailAlert(
  const String &eventName,
  const String &modeName,
  const String &sensorName
) {
  if (
    WiFi.status() != WL_CONNECTED
  ) {
    logEvent(
      "EMAIL NOT SENT: WI-FI OFFLINE"
    );

    return;
  }

  String requestURL =
    String(appsScriptURL);

  requestURL +=
    "?token=" +
    urlEncode(secretToken);

  requestURL +=
    "&event=" +
    urlEncode(eventName);

  requestURL +=
    "&mode=" +
    urlEncode(modeName);

  requestURL +=
    "&sensor=" +
    urlEncode(sensorName);

  requestURL +=
    "&time=" +
    urlEncode(getDateTime());

  WiFiClientSecure secureClient;

  secureClient.setInsecure();

  HTTPClient http;

  http.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );

  if (
    !http.begin(
      secureClient,
      requestURL
    )
  ) {
    logEvent(
      "EMAIL REQUEST COULD NOT START"
    );

    return;
  }

  int responseCode =
    http.GET();

  if (
    responseCode >= 200 &&
    responseCode < 400
  ) {
    logEvent(
      "EMAIL ALERT SENT"
    );
  }
  else {
    logEvent(
      "EMAIL SEND FAILED: HTTP " +
      String(responseCode)
    );
  }

  http.end();
}

// URL ENCODING

String urlEncode(
  const String &value
) {
  String encoded = "";

  const char
    hexCharacters[] =
    "0123456789ABCDEF";

  for (
    unsigned int i = 0;
    i < value.length();
    i++
  ) {
    unsigned char character =
      value.charAt(i);

    if (
      (
        character >= 'a' &&
        character <= 'z'
      )
      ||
      (
        character >= 'A' &&
        character <= 'Z'
      )
      ||
      (
        character >= '0' &&
        character <= '9'
      )
      ||
      character == '-'
      ||
      character == '_'
      ||
      character == '.'
      ||
      character == '~'
    ) {
      encoded +=
        (char)character;
    }
    else {
      encoded += '%';

      encoded +=
        hexCharacters[
          (character >> 4) &
          0x0F
        ];

      encoded +=
        hexCharacters[
          character & 0x0F
        ];
    }
  }

  return encoded;
}

// LED AND BUZZER OUTPUTS

void updateOutputs() {
  if (systemState == DISARMED) {
    digitalWrite(greenLED, HIGH);
    digitalWrite(yellowLED, LOW);
    digitalWrite(redLED, LOW);
    digitalWrite(buzzerPin, BUZZER_OFF);
  }

  else if (
    systemState ==
    EXIT_DELAY_STATE
  ) {
    digitalWrite(greenLED, LOW);
    digitalWrite(redLED, LOW);
    digitalWrite(buzzerPin, BUZZER_OFF);

    if (
      millis() -
      lastBlinkTime >= 500
    ) {
      lastBlinkTime = millis();

      yellowLEDState =
        !yellowLEDState;

      digitalWrite(
        yellowLED,
        yellowLEDState
      );
    }
  }

  else if (
    systemState == ARMED &&
    armMode == AWAY_MODE
  ) {
    digitalWrite(greenLED, LOW);
    digitalWrite(yellowLED, LOW);
    digitalWrite(redLED, HIGH);
    digitalWrite(buzzerPin, BUZZER_OFF);
  }

  else if (
    systemState == ARMED &&
    armMode == STAY_MODE
  ) {
    digitalWrite(greenLED, LOW);
    digitalWrite(yellowLED, LOW);
    digitalWrite(buzzerPin, BUZZER_OFF);

    if (
      millis() -
      lastBlinkTime >= 1000
    ) {
      lastBlinkTime = millis();

      redLEDState =
        !redLEDState;

      digitalWrite(
        redLED,
        redLEDState
      );
    }
  }

  else if (systemState == ALARM) {
    digitalWrite(greenLED, LOW);
    digitalWrite(yellowLED, LOW);
    digitalWrite(buzzerPin, BUZZER_ON);

    if (
      millis() -
      lastBlinkTime >= 250
    ) {
      lastBlinkTime = millis();

      redLEDState =
        !redLEDState;

      digitalWrite(
        redLED,
        redLEDState
      );
    }
  }
}

// EVENT LOGGING 

void logEvent(
  const String &message
) {
  String completedEvent =
    "[" +
    getTimeOnly() +
    "] " +
    message;

  Serial.println(completedEvent);

  for (
    int i = MAX_EVENTS - 1;
    i > 0;
    i--
  ) {
    eventLog[i] =
      eventLog[i - 1];
  }

  eventLog[0] =
    completedEvent;

  if (
    eventCount < MAX_EVENTS
  ) {
    eventCount++;
  }
}

// DASHBOARD STATUS HELPERS

String getStateText() {
  if (systemState == DISARMED) {
    return "SYSTEM DISARMED";
  }

  if (
    systemState ==
    EXIT_DELAY_STATE
  ) {
    if (
      pendingArmMode ==
      STAY_MODE
    ) {
      return
        "ARMING STAY MODE";
    }

    return
      "ARMING AWAY MODE";
  }

  if (systemState == ARMED) {
    if (armMode == STAY_MODE) {
      return
        "STAY MODE ARMED";
    }

    return
      "AWAY MODE ARMED";
  }

  if (systemState == ALARM) {
    return "ALARM ACTIVE";
  }

  return "UNKNOWN";
}

String getStateClass() {
  if (systemState == DISARMED) {
    return "disarmed";
  }

  if (
    systemState ==
    EXIT_DELAY_STATE
  ) {
    return "exit";
  }

  if (
    systemState == ARMED &&
    armMode == STAY_MODE
  ) {
    return "stay";
  }

  if (
    systemState == ARMED &&
    armMode == AWAY_MODE
  ) {
    return "away";
  }

  if (systemState == ALARM) {
    return "alarm";
  }

  return "disarmed";
}

String getArmModeText() {
  if (
    systemState ==
    EXIT_DELAY_STATE
  ) {
    if (
      pendingArmMode ==
      STAY_MODE
    ) {
      return "Stay";
    }

    if (
      pendingArmMode ==
      AWAY_MODE
    ) {
      return "Away";
    }
  }

  if (armMode == STAY_MODE) {
    return "Stay";
  }

  if (armMode == AWAY_MODE) {
    return "Away";
  }

  return "None";
}
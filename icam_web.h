#ifndef ICAM_WEB_H
#define ICAM_WEB_H

// ICAM read-only web extension, not a replacement for icam.ino.
// Include AFTER all your original globals, BEFORE setup().
// The original automation functions remain unchanged.
// Required additional calls are documented in guide.html and README.md.
// Hardware compilation/testing is required before classroom use.

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

WebServer icamHttp(80);
bool icamFilesystemOK = false;
bool icamWasConnected = false;
unsigned long icamLastDatabaseWrite = 0;
const unsigned long ICAM_DATABASE_INTERVAL_MS = 10000UL; // one row every 10 seconds
const size_t ICAM_DATABASE_MAX_BYTES = 180000; // keep the log bounded for LittleFS
const char* ICAM_DATABASE_PATH = "/icam_readings.csv";

const char* icamDatabaseHeader() {
  return "uptime_ms,temperature_c,humidity_pct,dht_ok,student_count,occupied,ldr_raw,ambient_light_detected,mq2_raw,mq2_adc_voltage,current_a,current_ma,fan_on,light_relay_on,buzzer_on,gas_unsafe\n";
}

void icamEnsureDatabase() {
  if (!icamFilesystemOK) return;
  if (!LittleFS.exists(ICAM_DATABASE_PATH)) {
    File file = LittleFS.open(ICAM_DATABASE_PATH, FILE_WRITE);
    if (file) { file.print(icamDatabaseHeader()); file.close(); }
  }
}

// Append a local, offline CSV database row. No internet or external service is used.
void icamStoreReading(unsigned long now) {
  if (!icamFilesystemOK || now - icamLastDatabaseWrite < ICAM_DATABASE_INTERVAL_MS) return;
  icamLastDatabaseWrite = now;

  File existing = LittleFS.open(ICAM_DATABASE_PATH, "r");
  if (!existing) {
    icamEnsureDatabase();
  } else {
    size_t bytes = existing.size();
    existing.close();
    // Keep storage bounded. When full, start a fresh CSV session rather than
    // allowing the filesystem to fill and interfere with dashboard serving.
    if (bytes >= ICAM_DATABASE_MAX_BYTES) {
      File resetFile = LittleFS.open(ICAM_DATABASE_PATH, FILE_WRITE);
      if (resetFile) { resetFile.print(icamDatabaseHeader()); resetFile.close(); }
    }
  }

  File file = LittleFS.open(ICAM_DATABASE_PATH, FILE_APPEND);
  if (!file) {
    Serial.println("ICAM database: could not append CSV row.");
    return;
  }
  file.print(now); file.print(',');
  if (dhtOK) file.print(String(temperature, 1));
  file.print(',');
  if (dhtOK) file.print(String(humidity, 1));
  file.print(','); file.print(dhtOK ? 1 : 0);
  file.print(','); file.print(studentCount);
  file.print(','); file.print(studentCount >= OCCUPIED_THRESHOLD ? 1 : 0);
  file.print(','); file.print(ldrRaw);
  file.print(','); file.print(lightOn ? 1 : 0);
  file.print(','); file.print(mq2Raw);
  file.print(','); file.print(String(mq2AdcVoltage, 3));
  file.print(','); file.print(String(currentAmps, 3));
  file.print(','); file.print(String(currentMilliAmps, 0));
  file.print(','); file.print(digitalRead(FAN_RELAY) == RELAY_ON ? 1 : 0);
  file.print(','); file.print(digitalRead(LED_RELAY) == RELAY_ON ? 1 : 0);
  file.print(','); file.print(digitalRead(BUZZER_PIN) == BUZZER_ON ? 1 : 0);
  file.print(','); file.println(gasUnsafe ? 1 : 0);
  file.close();
}

void icamDownloadDatabase() {
  if (!icamFilesystemOK) {
    icamHttp.send(503, "text/plain", "LittleFS is not mounted; cannot access the local database.");
    return;
  }
  icamEnsureDatabase();
  File file = LittleFS.open(ICAM_DATABASE_PATH, "r");
  if (!file) {
    icamHttp.send(500, "text/plain", "Could not open ICAM CSV database.");
    return;
  }
  icamHttp.sendHeader("Content-Disposition", "attachment; filename=icam_readings.csv");
  icamHttp.sendHeader("Cache-Control", "no-store");
  icamHttp.streamFile(file, "text/csv; charset=utf-8");
  file.close();
}

void icamServeFile(const char* path, const char* mime) {
  if (!icamFilesystemOK) {
    icamHttp.send(503, "text/plain", "LittleFS unavailable. Upload the data folder using the correct partition layout. No formatting was attempted.");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    icamHttp.send(404, "text/plain", "Dashboard file missing. Upload index.html, styles.css, app.js, and guide.html to LittleFS.");
    return;
  }
  icamHttp.sendHeader("Cache-Control", "no-cache");
  icamHttp.streamFile(file, mime);
  file.close();
}

void icamSendStatus() {
  // No analogRead, sensor sampling, pin changes, or automation here.
  // Read the final firmware's existing variables and GPIO output state.
  String json;
  json.reserve(640);
  json = "{\"source\":\"icam-esp32\",\"mode\":\"AUTO\"";
  json += ",\"uptimeMs\":"; json += String(millis());
  json += ",\"dhtOK\":"; json += dhtOK ? "true" : "false";
  json += ",\"temperature\":"; json += dhtOK ? String(temperature, 1) : "null";
  json += ",\"humidity\":"; json += dhtOK ? String(humidity, 1) : "null";
  json += ",\"studentCount\":"; json += String(studentCount);
  json += ",\"occupied\":"; json += (studentCount >= OCCUPIED_THRESHOLD) ? "true" : "false";
  json += ",\"ldrRaw\":"; json += String(ldrRaw);
  // lightOn in the original firmware is ambient detection, NOT LED relay state.
  json += ",\"ambientLightDetected\":"; json += lightOn ? "true" : "false";
  json += ",\"lightRelayOn\":"; json += (digitalRead(LED_RELAY) == RELAY_ON) ? "true" : "false";
  json += ",\"fanOn\":"; json += (digitalRead(FAN_RELAY) == RELAY_ON) ? "true" : "false";
  json += ",\"mq2Raw\":"; json += String(mq2Raw);
  json += ",\"mq2AdcVoltage\":"; json += String(mq2AdcVoltage, 3);
  json += ",\"currentAmps\":"; json += String(currentAmps, 3);
  json += ",\"currentMilliAmps\":"; json += String(currentMilliAmps, 0);
  json += ",\"gasUnsafe\":"; json += gasUnsafe ? "true" : "false";
  json += ",\"buzzerOn\":"; json += (digitalRead(BUZZER_PIN) == BUZZER_ON) ? "true" : "false";
  json += ",\"fanThresholdC\":"; json += String(TEMP_THRESHOLD, 1);
  json += ",\"gasThresholdRaw\":"; json += String(GAS_THRESHOLD);
  json += ",\"occupiedThreshold\":"; json += String(OCCUPIED_THRESHOLD);
  json += "}";
  icamHttp.sendHeader("Cache-Control", "no-store");
  icamHttp.send(200, "application/json", json);
}

void icamWebBegin() {
  // Mount the existing LittleFS partition without formatting it.
  icamFilesystemOK = LittleFS.begin(false);
  if (!icamFilesystemOK) {
    Serial.println("ICAM web: LittleFS mount failed; check partition and upload the data folder.");
  }

  icamHttp.on("/", HTTP_GET, []() {
    icamHttp.sendHeader("Location", "/index.html?source=device");
    icamHttp.sendHeader("Cache-Control", "no-store");
    icamHttp.send(302, "text/plain", "Open the ICAM dashboard.");
  });
  icamHttp.on("/index.html", HTTP_GET, []() { icamServeFile("/index.html", "text/html; charset=utf-8"); });
  icamHttp.on("/styles.css", HTTP_GET, []() { icamServeFile("/styles.css", "text/css; charset=utf-8"); });
  icamHttp.on("/app.js", HTTP_GET, []() { icamServeFile("/app.js", "application/javascript; charset=utf-8"); });
  icamHttp.on("/guide.html", HTTP_GET, []() { icamServeFile("/guide.html", "text/html; charset=utf-8"); });
  icamHttp.on("/icam-logo.png", HTTP_GET, []() { icamServeFile("/icam-logo.png", "image/png"); });
  icamHttp.on("/icam-logo.jpg", HTTP_GET, []() { icamServeFile("/icam-logo.jpg", "image/jpeg"); });
  icamHttp.on("/api/status", HTTP_GET, icamSendStatus);
  icamHttp.on("/api/log.csv", HTTP_GET, icamDownloadDatabase);
  icamHttp.on("/favicon.ico", HTTP_GET, []() { icamHttp.send(204, "text/plain", ""); });
  icamHttp.onNotFound([]() {
    icamHttp.send(404, "text/plain", "Not found. ICAM exposes dashboard files and GET /api/status only.");
  });

  icamEnsureDatabase();

  // Standalone Access Point: no college Wi-Fi, router, mobile data or internet required.
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  const char* apName = "ICAM-Classroom";
  const char* apPassword = "ICAM@2026"; // Change this if desired; minimum 8 characters.
  bool apStarted = WiFi.softAP(apName, apPassword, 1, false, 4);

  if (apStarted) {
    Serial.println();
    Serial.println("================================");
    Serial.println("ICAM OFFLINE DASHBOARD READY");
    Serial.print("Wi-Fi network: "); Serial.println(apName);
    Serial.print("Wi-Fi password: "); Serial.println(apPassword);
    Serial.print("Open in browser: http://"); Serial.println(WiFi.softAPIP());
    Serial.println("No internet or router required.");
    Serial.println("================================");
  } else {
    Serial.println("ICAM web: Failed to start access point.");
  }

  icamHttp.begin();
  Serial.println("ICAM web server started.");
}

void icamWebLoop() {
  // Save a persistent local CSV snapshot every 10 seconds, then serve clients.
  // No router, internet, or cloud database is used.
  icamStoreReading(millis());
  icamHttp.handleClient();
}

#endif
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

// Wi-Fi credentials
const char* ssid = "mari";
const char* password = "azer2499";

// Supabase config
const char* host = "bklwumyyghsypidqnjsn.supabase.co";
const int httpsPort = 443;
const char* apiKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImJrbHd1bXl5Z2hzeXBpZHFuanNuIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NDkwMjUyMzMsImV4cCI6MjA2NDYwMTIzM30.1rrsZhANWdOWeoNIDw7fDItoPqUUjYnPyz8mQJVyM7k";

// Device ID
String deviceId = "82e00ea2-08bc-494a-81e9-a568e0de9e38";

// Pin for smoke sensor (D0)
const int smokePin = 16;
// Pin for buzzer (D1)
const int buzzerPin = 5;

// Buzzer settings
const int buzzerFrequency = 1000;  // 1kHz tone

WiFiClientSecure client;
unsigned long lastConnectionAttempt = 0;
const unsigned long connectionTimeout = 5000;

const unsigned long checkInterval = 1000;       // Check every 1s
const unsigned long clearCooldown = 10000;      // 10s of clean air before reporting "false"

bool smokeActive = false;
unsigned long lastSmokeDetectedTime = 0;
unsigned long lastSmokeClearTime = 0;

void beepOnce() {
  tone(buzzerPin, buzzerFrequency);
  delay(500);
  noTone(buzzerPin);
}

void beepTwice() {
  for (int i = 0; i < 2; i++) {
    tone(buzzerPin, buzzerFrequency);
    delay(300);
    noTone(buzzerPin);
    delay(200);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(smokePin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  noTone(buzzerPin);  // Ensure buzzer is off initially

  WiFiManager wifiManager;
  Serial.println("Trying to connect to WiFi...");

  wifiManager.setTimeout(30);  // Config portal timeout
  wifiManager.autoConnect("ESP8266_Config_AP");

  // Give 10 seconds max to connect
  int waitTime = 0;
  while (WiFi.status() != WL_CONNECTED && waitTime < 10000) {
    delay(500);
    waitTime += 500;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi.");
    beepTwice();  // Beep twice to indicate WiFi connection failure
    delay(1000);
    ESP.restart();
  }

  Serial.println("WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  beepOnce();

  client.setInsecure(); // Disable SSL verification
}

void loop() {
  static unsigned long lastCheck = 0;
  static unsigned long lastBeep = 0;
  static bool isBeeping = false;
  unsigned long currentMillis = millis();

  if (currentMillis - lastCheck >= checkInterval) {
    lastCheck = currentMillis;

    bool smokeDetected = digitalRead(smokePin) == LOW;

    if (smokeDetected) {
      // Reset clear timer
      lastSmokeClearTime = currentMillis;

      // If not already active, respond immediately
      if (!smokeActive) {
        Serial.println("Smoke status changed: DETECTED");
        // Start beeping immediately
        smokeActive = true;
        lastBeep = currentMillis;
        isBeeping = true;
        tone(buzzerPin, buzzerFrequency);  // Start continuous beep
        
        // Then update status
        if (updateDeviceStatus(true)) {
          Serial.println("Status updated successfully.");
          logSmokeEvent(true);
        } else {
          Serial.println("Failed to update status.");
        }
      }
      // Keep updating the detection time while smoke is present
      lastSmokeDetectedTime = currentMillis;
    } else {
      // If smoke was active and clear for cooldown period
      if (smokeActive && currentMillis - lastSmokeDetectedTime >= clearCooldown) {
        Serial.println("Smoke status changed: CLEAR");
        if (updateDeviceStatus(false)) {
          Serial.println("Status updated successfully.");
          smokeActive = false;
          noTone(buzzerPin);  // Turn off buzzer when smoke clears
          isBeeping = false;
        } else {
          Serial.println("Failed to update status.");
        }
      }
    }
  }

  // Handle buzzer beeping
  if (smokeActive) {
    if (isBeeping) {
      if (currentMillis - lastBeep >= 3000) {  // After 5 seconds of beeping
        noTone(buzzerPin);  // Stop beeping
        isBeeping = false;
        lastBeep = currentMillis;
      }
    } else {
      if (currentMillis - lastBeep >= 400) {  // After 500ms of silence
        tone(buzzerPin, buzzerFrequency);  // Start beeping again
        isBeeping = true;
        lastBeep = currentMillis;
      }
    }
  }
}

bool updateDeviceStatus(bool status) {
  if (!client.connected()) {
    if (millis() - lastConnectionAttempt < connectionTimeout) return false;
    lastConnectionAttempt = millis();

    if (!client.connect(host, httpsPort)) {
      Serial.println("Connection failed");
      return false;
    }
  }

  String url = "/rest/v1/device?id=eq." + deviceId;
  String json = "{\"status\": " + String(status ? "true" : "false") + "}";

  client.printf("PATCH %s HTTP/1.1\r\n", url.c_str());
  client.printf("Host: %s\r\n", host);
  client.println("Content-Type: application/json");
  client.printf("apikey: %s\r\n", apiKey);
  client.println("Authorization: Bearer " + String(apiKey));
  client.println("Prefer: return=representation");
  client.printf("Content-Length: %d\r\n", json.length());
  client.println("Connection: keep-alive\r\n");
  client.println(json);

  bool success = false;
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
    if (line.startsWith("HTTP/1.1 2")) success = true;
  }

  while (client.available()) client.read();
  return success;
}

bool logSmokeEvent(bool status) {
  if (!client.connected()) {
    if (millis() - lastConnectionAttempt < connectionTimeout) return false;
    lastConnectionAttempt = millis();

    if (!client.connect(host, httpsPort)) {
      Serial.println("Connection failed (logSmokeEvent)");
      return false;
    }
  }

  String url = "/rest/v1/smoke_logs";
  String json = "{";
  json += "\"device_id\": \"" + deviceId + "\",";
  json += "\"status\": " + String(status ? "true" : "false");
  json += "}";

  client.printf("POST %s HTTP/1.1\r\n", url.c_str());
  client.printf("Host: %s\r\n", host);
  client.println("Content-Type: application/json");
  client.printf("apikey: %s\r\n", apiKey);
  client.println("Authorization: Bearer " + String(apiKey));
  client.println("Prefer: return=representation");
  client.printf("Content-Length: %d\r\n", json.length());
  client.println("Connection: keep-alive\r\n");
  client.println(json);

  bool success = false;
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
    if (line.startsWith("HTTP/1.1 2")) success = true;
  }

  while (client.available()) client.read();
  return success;
}

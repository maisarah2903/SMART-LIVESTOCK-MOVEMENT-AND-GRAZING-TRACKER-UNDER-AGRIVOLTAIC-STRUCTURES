#define BLYNK_TEMPLATE_ID "TMPL6OFYQQH2g"
#define BLYNK_TEMPLATE_NAME "Livetsock Alert"
#define BLYNK_AUTH_TOKEN "VjGQfcu8swWGzlPH6NetJgTaueDmms7s"

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>

// --- Pin Definitions ---
#define SS_PIN 21
#define RST_PIN 22
#define BUZZER_PIN 5
#define PIR_PIN 15
#define LED_PIN 2  // Optional indicator LED

// --- WiFi Credentials ---
const char* ssid = "Kiss me";
const char* password = "Emiliya0608";

// --- Google Apps Script Web App URL ---
const char* googleScriptURL = "https://script.google.com/macros/s/AKfycbxD5KiDTbjqEZrjp-UnGANamEv02w2h5WgdEpwEX4VdppjwhGpxXGmC6e03fjZ2H7Kzgg/exec";

// --- Global Objects ---
MFRC522 rfid(SS_PIN, RST_PIN);
BlynkTimer timer;

// --- PIR Motion Variables ---
bool motionActive = false;
unsigned long lastMotionTime = 0;
unsigned long lastNoMotionTime = 0;
const unsigned long motionInterval = 4000; // 3s buzz + 1s off
const unsigned long pirDelayTime = 5000;
unsigned long pirReadyTime;

// --- RFID Debounce ---
String lastDetectedTag = "";
unsigned long lastDetectionTime = 0;
const unsigned long DEBOUNCE_TIME = 3000;

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  SPI.begin();
  rfid.PCD_Init();

  // WiFi connection
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());

  // Blynk connection
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
  timer.setInterval(1000L, checkBlynkConnection);

  // PIR warm-up
  pirReadyTime = millis() + pirDelayTime;
}

void checkBlynkConnection() {
  if (!Blynk.connected()) {
    Serial.println("Reconnecting to Blynk...");
    Blynk.connect();
  }
}

void sendToGoogleSheets(String tagUID) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // Accept all SSL certs
  HTTPClient http;

  String url = String(googleScriptURL) + "?type=rfid&tag=" + tagUID;
  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Sheets response: " + response);
  } else {
    Serial.println("HTTP error: " + http.errorToString(httpCode));
  }
  http.end();
}

void triggerLivestockAlert(String tagUID) {
  digitalWrite(LED_PIN, HIGH);

  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, tagUID);
    Blynk.logEvent("livestock_alert", "Tag: " + tagUID);
    Serial.println("Blynk notified");
  }

  sendToGoogleSheets(tagUID);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
}

void handlePIRMotion() {
  unsigned long currentTime = millis();

  if (currentTime >= pirReadyTime) {
    bool pirState = digitalRead(PIR_PIN);

    if (pirState == HIGH) {
      if (!motionActive) {
        Serial.println("Motion detected!");
        motionActive = true;
        lastMotionTime = 0;
      }

      if (currentTime - lastMotionTime >= motionInterval) {
        Serial.println("Buzzing due to motion...");
        digitalWrite(BUZZER_PIN, HIGH);
        delay(3000);
        digitalWrite(BUZZER_PIN, LOW);
        lastMotionTime = currentTime;
      }

      lastNoMotionTime = currentTime;
    }

    if (pirState == LOW && motionActive) {
      if (currentTime - lastNoMotionTime >= 3000) {
        Serial.println("No motion. Buzzer OFF");
        digitalWrite(BUZZER_PIN, LOW);
        motionActive = false;
      }
    }
  } else {
    Serial.println("PIR warming up...");
    delay(500);
  }
}

void handleRFID() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uidStr = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      uidStr += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
      uidStr += String(rfid.uid.uidByte[i], HEX);
    }
    uidStr.toUpperCase();
    Serial.println("UID Tag: " + uidStr);

    if (uidStr != lastDetectedTag || millis() - lastDetectionTime > DEBOUNCE_TIME) {
      triggerLivestockAlert(uidStr);
      lastDetectedTag = uidStr;
      lastDetectionTime = millis();
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    delay(1000);
  }
}

void loop() {
  Blynk.run();
  timer.run();
  handlePIRMotion();
  handleRFID();
}

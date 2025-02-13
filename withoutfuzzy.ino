#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "DHT.h"

// WiFi credentials
const char* ssid = "bayhaqi";              // Replace with your Wi-Fi SSID
const char* password = "bayhukakimblakur";    // Replace with your Wi-Fi password

// ThingsBoard credentials
#define TOKEN "PLbRVM9VFjYR6JRQQFxc"   // Replace with your device access token
#define THINGSBOARD_SERVER "demo.thingsboard.io" // ThingsBoard server address

WiFiClient espClient;
PubSubClient client(espClient);

// Sensor and relay pins
const int soilMoisturePin = 33;
const int waterSensorPin = 32;
const int relayPin = 5;
const int DHTPin = 27;
#define DHTTYPE DHT11

DHT dht(DHTPin, DHTTYPE);

// Variables for monitoring
bool manualOverride = false;
unsigned long startTime;
unsigned long monitoringDuration = 30000; // 30 seconds
bool monitoringComplete = false;

// Watering counters and levels
int wateringCount = 0;
double initialWaterLevel = 0;
double finalWaterLevel = 0;
double waterUsed = 0;
unsigned long pumpStartTime = 0;
unsigned long pumpTotalTime = 0;

void setup() {
  Serial.begin(115200);

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  // Initialize Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected to WiFi. IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize MQTT
  client.setServer(THINGSBOARD_SERVER, 1883);
  client.setCallback(callback);
  connectToMQTT();

  // Initialize DHT sensor
  dht.begin();

  // Initialize monitoring variables
  startTime = millis();
  initialWaterLevel = readWaterLevel();
}

void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to ThingsBoard...");
    if (client.connect("ESP32_Client", TOKEN, "")) {
      Serial.println("Connected to ThingsBoard!");
      client.subscribe("v1/devices/me/rpc/request/+");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

double readSoilMoisture() {
  double soilMoistureValue = analogRead(soilMoisturePin);
  return (100 - ((soilMoistureValue / 4095.00) * 100));
}

double readWaterLevel() {
  double waterValue = analogRead(waterSensorPin);
  if (waterValue >= 2350) {
    return 100;
  }
  return ((waterValue / 2350.00) * 100);
}

float readTemperature() {
  return dht.readTemperature();
}

void controlPump(double soilMoisture, float temperature) {
  unsigned long pumpDuration = 0;

  if (soilMoisture < 70) { // Dry soil
    if (temperature > 30) {
      pumpDuration = 2500; // Long duration (2.5 seconds)
    } else if (temperature >= 18 && temperature <= 30) {
      pumpDuration = 1500; // Moderate duration (1.5 seconds)
    } else {
      pumpDuration = 1000; // Short duration (1 second)
    }
  }

  if (pumpDuration > 0) {
    pumpStartTime = millis();
    digitalWrite(relayPin, HIGH);
    delay(pumpDuration);
    digitalWrite(relayPin, LOW);
    pumpTotalTime += pumpDuration;
    wateringCount++;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("Message arrived: " + message);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, message);
  if (!error) {
    const char* method = doc["method"];
    const char* state = doc["params"]["state"];

    if (strcmp(method, "setPumpState") == 0) {
      if (strcmp(state, "on") == 0) {
        manualOverride = true;
        digitalWrite(relayPin, HIGH);
        Serial.println("Manual override: Pump turned ON");
      } else if (strcmp(state, "off") == 0) {
        manualOverride = false;
        digitalWrite(relayPin, LOW);
        Serial.println("Manual override: Pump turned OFF");
      }
    }
  } else {
    Serial.print("Failed to parse message: ");
    Serial.println(error.f_str());
  }
}

void loop() {
  if (monitoringComplete) {
    Serial.println("Monitoring selesai. Sistem berhenti.");
    while (true); // Hentikan loop
  }

  unsigned long currentTime = millis();
  if (currentTime - startTime <= monitoringDuration) {
    double soilMoisture = readSoilMoisture();
    float temperature = readTemperature();
    double waterLevel = readWaterLevel();

    if (!manualOverride) {
      controlPump(soilMoisture, temperature);
    }

    Serial.print("Soil Moisture: ");
    Serial.print(soilMoisture);
    Serial.print("%, Temperature: ");
    Serial.print(temperature);
    Serial.print("C, Current Water Level: ");
    Serial.print(waterLevel);
    Serial.println("%");
  } else {
    finalWaterLevel = readWaterLevel();
    waterUsed = initialWaterLevel - finalWaterLevel;

    Serial.println("\n=== Monitoring Completed (30 seconds) ===");
    Serial.print("Total Watering Count: ");
    Serial.println(wateringCount);
    Serial.print("Initial Water Level: ");
    Serial.print(initialWaterLevel);
    Serial.println("%");
    Serial.print("Final Water Level: ");
    Serial.print(finalWaterLevel);
    Serial.println("%");
    Serial.print("Water Used: ");
    Serial.print(waterUsed);
    Serial.println("%");
    Serial.print("Total Pump Operation Time: ");
    Serial.print(pumpTotalTime / 1000.0);
    Serial.println(" seconds");

    monitoringComplete = true; // Set flag untuk menghentikan loop
  }

  delay(1000); // Stabilkan loop setiap 1 detik
}
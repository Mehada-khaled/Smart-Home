#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <FirebaseESP8266.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h" 
#include <ESP8266WebServer.h>

#define IR_PIN D1
#define LED_PIN D7
#define DHT_PIN D5
#define ANALOG_PIN A0
#define BUZZER_PIN D2
#define FAN_PIN D6
#define ANALOG_THRESHOLD 820

const char* ssid = "DESKTOP-OE8H2T3 2416"; // WiFi SSID
const char* password = "1j635V0+"; // WiFi Password
const char* broker = "192.168.137.1"; // MQTT broker address
const int port = 1883; // MQTT broker port
const char* topic = "home/sensors"; // MQTT topic name
const char* DATABASE_URL = "https://iot-lab-2f2ee-default-rtdb.firebaseio.com/";
const char* API_KEY = "AIzaSyBiHdqqlotLkYhuYARVu5MCawfglQZsVAs";

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;
bool fanStatus = LOW;
bool buzzerStatus = LOW;
bool ledStatus = LOW;
float temp;
int humidity;
int gassensor;
int irvalue;
int threshold = 2000;

unsigned long fanToggleTime = 0;
unsigned long buzzerToggleTime = 0;
unsigned long ledToggleTime = 0;
const unsigned long toggleDuration = 5000; // 5 seconds

ESP8266WebServer server(80);
DHT dht(DHT_PIN, DHT11);

void handleRoot();
void handleBuzzerOn();
void handleBuzzerOff();
void handleFanOn();
void handleFanOff();
void handleLedOn();
void handleLedOff();
void handleNotFound();

WiFiClient espClient; // Create an object of the WiFiClient class
PubSubClient client(espClient); // Create an MQTT client instance

void setup() {
  Serial.begin(115200); // Initialize serial communication at baudrate of 115200
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(DHT_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
  dht.begin();
  WiFi.begin(ssid, password); // Attempt to connect to the Wi-Fi network
  while (WiFi.status() != WL_CONNECTED) { // Wait until the NodeMCU is successfully connected
    delay(1000); // Wait 1 second before rechecking Wi-Fi connection status
    Serial.println("Connecting to WiFi..."); // A message indicating an attempt to connect to Wi-Fi
  }
  Serial.println("Connected to WiFi."); // A message indicating a successful connection
  client.setServer(broker, port); // Connect to the MQTT broker

  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32_Client")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("SignUp OK");
    signupOK = true;
  } else {
    Serial.printf("SignUp Error: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  server.on("/", handleRoot);
  server.on("/actuators/fan/on", handleFanOn);
  server.on("/actuators/fan/off", handleFanOff);
  server.on("/actuators/buzzer/on", handleBuzzerOn);
  server.on("/actuators/buzzer/off", handleBuzzerOff);
  server.on("/actuators/led/on", handleLedOn);
  server.on("/actuators/led/off", handleLedOff);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("NodeMCU Web Server Started");
}

void loop() {
  server.handleClient();
  if (!client.connected()) {
    while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      if (client.connect("ESP32_Client")) {
        Serial.println("connected");
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        delay(5000);
      }
    }
  }

  unsigned long currentMillis = millis();

  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();
    
    gassensor = analogRead(ANALOG_PIN);
    Serial.print("Gas Sensor: ");
    Serial.println(gassensor);

    temp = dht.readTemperature();
    Serial.print("Temperature: ");
    Serial.println(temp);

    humidity = dht.readHumidity();
    Serial.print("Humidity: ");
    Serial.println(humidity);

    irvalue = digitalRead(IR_PIN);
    Serial.print("IR Sensor: ");
    Serial.println(irvalue);

    // Check if the 10-second timer has expired for each actuator
    if (currentMillis - fanToggleTime >= toggleDuration) {
      if (temp >= 23) {
        fanStatus = HIGH;
      } else {
        fanStatus = LOW;
      }
      digitalWrite(FAN_PIN, fanStatus);
    }

    if (currentMillis - buzzerToggleTime >= toggleDuration) {
      if (gassensor >= ANALOG_THRESHOLD) {
        buzzerStatus = HIGH;
      } else {
        buzzerStatus = LOW;
      }
      digitalWrite(BUZZER_PIN, buzzerStatus);
    }

    if (currentMillis - ledToggleTime >= toggleDuration) {
      if (irvalue == 0) {
        ledStatus = HIGH;
      } else {
        ledStatus = LOW;
      }
      digitalWrite(LED_PIN, ledStatus);
    }

    updateFirebase("/temp", temp);
    updateFirebase("/humidity", humidity);
    updateFirebase("/gas", gassensor);
    updateFirebase("/ir", irvalue);
  }

  // Replace these with your actual sensor readings
  float temperature = getTemperatureReading();
  int gasLevel = getGasLevel();
  int humidityLevel = getHumidityLevel();

  // Check if the readings are valid numbers
  if (isnan(temperature)) {
    temp = 25;
  }

  if (isnan(gasLevel)) {
    gassensor = 100;
  }

  if (isnan(humidityLevel)) {
    humidity = 50;
  }

  // Create a JSON object to hold the sensor data
  StaticJsonDocument<200> doc;
  doc["temp"] = temp;
  doc["gas"] = gassensor;
  doc["humidity"] = humidity;

  // Convert JSON object to string
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);

  // Publish the JSON string
  client.publish(topic, jsonBuffer);
  Serial.print("Published message: ");
  Serial.println(jsonBuffer);

  delay(1000); // Short delay to avoid rapid publishing
}

void updateFirebase(String path, float value) {
  Serial.println("Updating Firebase...");
  if (Firebase.setFloat(fbdo, path, value)) {
    Serial.print("Updated ");
    Serial.print(path);
    Serial.print(": ");
    Serial.println(value);
  } else {
    Serial.println(fbdo.errorReason());
  }
}

float getTemperatureReading() {
  temp = dht.readTemperature();
  updateFirebase("/temp", temp);
  return temp;
}

int getGasLevel() {
  gassensor = analogRead(ANALOG_PIN);
  updateFirebase("/gas", gassensor);
  return gassensor;
}

int getHumidityLevel() {
  humidity = dht.readHumidity();
  updateFirebase("/humidity", humidity);
  return humidity;
}

void handleRoot() {
  temp = dht.readTemperature();
  humidity = dht.readHumidity();
  gassensor = analogRead(ANALOG_PIN);
  irvalue = digitalRead(IR_PIN);
  updateFirebase("/temp", temp);
  updateFirebase("/humidity", humidity);
  updateFirebase("/gas", gassensor);
  updateFirebase("/ir", irvalue);
  server.send(200, "text/html", getHtml(temp, gassensor, humidity, irvalue));
}

void handleFanOn() {
  fanStatus = HIGH;
  digitalWrite(FAN_PIN, fanStatus);
  fanToggleTime = millis(); // Record the current time
  handleRoot();
}

void handleFanOff() {
  fanStatus = LOW;
  digitalWrite(FAN_PIN, fanStatus);
  fanToggleTime = millis(); // Record the current time
  handleRoot();
}

void handleBuzzerOn() {
  buzzerStatus = HIGH;
  digitalWrite(BUZZER_PIN, buzzerStatus);
  buzzerToggleTime = millis(); // Record the current time
  handleRoot();
}

void handleBuzzerOff() {
  buzzerStatus = LOW;
  digitalWrite(BUZZER_PIN, buzzerStatus);
  buzzerToggleTime = millis(); // Record the current time
  handleRoot();
}

void handleLedOn() {
  ledStatus = HIGH;
  digitalWrite(LED_PIN, ledStatus);
  ledToggleTime = millis(); // Record the current time
  handleRoot();
}

void handleLedOff() {
  ledStatus = LOW;
  digitalWrite(LED_PIN, ledStatus);
  ledToggleTime = millis(); // Record the current time
  handleRoot();
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}

String getHtml(float temperature, int gasLevel, int humidity, int irValue) {
  String html = "<html><body><h1>ESP8266 Web Server</h1>";
  html += "<p>Temperature: " + String(temperature) + " &deg;C</p>";
  html += "<p>Gas Level: " + String(gasLevel) + "</p>";
  html += "<p>Humidity: " + String(humidity) + " %</p>";
  html += "<p>IR Sensor: " + String(irValue) + "</p>";
  html += "<h3>Fan</h3>";
  html += "<p><a href=\"/actuators/fan/on\"><button>On</button></a>&nbsp;<a href=\"/actuators/fan/off\"><button>Off</button></a></p>";
  html += "<h3>Buzzer</h3>";
  html += "<p><a href=\"/actuators/buzzer/on\"><button>On</button></a>&nbsp;<a href=\"/actuators/buzzer/off\"><button>Off</button></a></p>";
  html += "<h3>LED</h3>";
  html += "<p><a href=\"/actuators/led/on\"><button>On</button></a>&nbsp;<a href=\"/actuators/led/off\"><button>Off</button></a></p>";
  html += "</body></html>";
  return html;
}

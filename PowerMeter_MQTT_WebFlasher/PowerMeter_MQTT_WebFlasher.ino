#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HLW8012.h>
#include <WebServer.h>
#include <Update.h>

#define SERIAL_BAUDRATE 115200

//GPIO
#define RELAY_PIN 25
#define SEL_PIN 26
#define CF1_PIN 13
#define CF_PIN 34
#define LED_PIN 32

//HLW8012 Config
#define UPDATE_TIME 2000        // ms
#define CURRENT_MODE HIGH
#define CURRENT_RESISTOR 0.001
#define VOLTAGE_RESISTOR_UPSTREAM (5 * 470000)  // 2280k
#define VOLTAGE_RESISTOR_DOWNSTREAM (1000)      // 1k

//HLW8012 Variables
double activePower, voltage, current, apparentPower, powerFactor;
char bufferActivePower[12];
char bufferVoltage[12];
char bufferCurrent[12];
char bufferApparentPower[12];
char bufferPowerFactor[12];
unsigned long prevMillis;

//Wifi Config
// const char* ssid = "V";
// const char* password = "";

//MQTT Config
const char* mqtt_server = "broker.emqx.io";
const char* TOPIC_activePower = "powermeter/02/activePower";
const char* TOPIC_voltage = "powermeter/02/voltage";
const char* TOPIC_current = "powermeter/02/current";
const char* TOPIC_apparentPower = "powermeter/02/apparentPower";
const char* TOPIC_powerFactor = "powermeter/02/powerFactor";
const char* TOPIC_relay = "powermeter/02/relay";
const char* TOPIC_json = "powermeter/02/json";
int intervalPengiriman = 5; // satuan detik

//Objects
WiFiClient espClient;
PubSubClient mqtt(espClient);
HLW8012 hlw8012;

WebServer server(80);

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
  <body>
    <h3>ESP32 Web Update</h3>
    <form method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="update" />
      <input type="submit" value="Update" />
    </form>
  </body>
</html>
)HTML";

//Wifi Setup func
void setup_wifi() {
  delay(10);
  // Serial.println();
  // Serial.print("Connecting to ");
  // Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  // WiFi.begin(ssid, password);

  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(500);
  //   Serial.print(".");
  // }
  // Serial.println("");
  // Serial.println("WiFi Terhubung");
  // Serial.println("IP address: ");
  // Serial.println(WiFi.localIP());

  WiFiManager wfm;
  wfm.setDebugOutput(true);
  wfm.resetSettings();
  // wfm.setConfigPortalTimeout(180);

  if (!wfm.autoConnect("ESP32TEST_AP", "password")) {
    // Did not connect, print error message
    Serial.println("failed to connect and hit timeout");
 
    // Reset and try again
    ESP.restart();
    delay(1000);
  }

  // Connected!
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  String localIP = WiFi.localIP().toString();
  WiFiManagerParameter ESPLocalIP("LocalIP", "WIFI LOCAL IP", localIP.c_str(), 50, "read-only");
  wfm.addParameter(&ESPLocalIP);  
  
  // // Print custom text box value to serial monitor
  Serial.print("Custom text box entry: ");
  Serial.println(ESPLocalIP.getValue());
}

//HLW8012 Callibration
void unblockingDelay(unsigned long mseconds) {
  unsigned long timeout = millis();
  while ((millis() - timeout) < mseconds) delay(1);
}

void calibrate() {
  hlw8012.getActivePower();

  hlw8012.setMode(MODE_CURRENT);
  unblockingDelay(2000);
  hlw8012.getCurrent();

  hlw8012.setMode(MODE_VOLTAGE);
  unblockingDelay(2000);
  hlw8012.getVoltage();

  //Load expected
  // Contoh beban: solder 70W / 230V
  hlw8012.expectedActivePower(60.0);
  hlw8012.expectedVoltage(220.0);
  hlw8012.expectedCurrent(60.0 / 220.0);

  Serial.print("[HLW] New current multiplier : ");
  Serial.println(hlw8012.getCurrentMultiplier());
  Serial.print("[HLW] New voltage multiplier : ");
  Serial.println(hlw8012.getVoltageMultiplier());
  Serial.print("[HLW] New power multiplier   : ");
  Serial.println(hlw8012.getPowerMultiplier());
  Serial.println();
}

//Feedback ish from MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Pesan Masuk dari topic [");
  Serial.print(topic);
  Serial.print("] :");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if ((char)payload[0] == '0') {
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("[RELAY] NYALA");
  } 
  else if((char)payload[0] == '1') {
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("[RELAY] MATI");
  }
}

//MQTT Reconnect
unsigned long lastMQTTAttempt = 0;

void reconnect() {
  if (mqtt.connected()) return;

  if (millis() - lastMQTTAttempt < 5000) return;

  lastMQTTAttempt = millis();

  Serial.print("Attempting MQTT connection...");

  String clientId = "ESP32Client-";
  clientId += String(random(0xffff), HEX);

  if (mqtt.connect(clientId.c_str())) {
    Serial.println("connected!");
    mqtt.subscribe(TOPIC_relay);
    Serial.println("Subscribe ke topic: " + String(TOPIC_relay));
  } 
  else {
    Serial.print("Failed, rc=");
    Serial.println(mqtt.state());
  }
}

//Data Publish to MQTT
void publish_data() {
  sprintf(bufferActivePower, "%.3f", activePower);
  mqtt.publish(TOPIC_activePower , bufferActivePower);

  sprintf(bufferVoltage, "%.1f", voltage);
  mqtt.publish(TOPIC_voltage, bufferVoltage);

  sprintf(bufferCurrent, "%.3f", current);
  mqtt.publish(TOPIC_current, bufferCurrent);

  sprintf(bufferApparentPower, "%.3f", apparentPower);
  mqtt.publish(TOPIC_apparentPower, bufferApparentPower);

  sprintf(bufferPowerFactor, "%.3f", powerFactor);
  mqtt.publish(TOPIC_powerFactor, bufferPowerFactor);
}

void publish_json() {
  char json[256];
  JsonDocument doc;

  sprintf(bufferActivePower, "%.3f", activePower);
  sprintf(bufferVoltage, "%.1f", voltage);
  sprintf(bufferCurrent, "%.3f", current);
  sprintf(bufferApparentPower, "%.3f", apparentPower);
  sprintf(bufferPowerFactor, "%.3f", powerFactor);

  doc["activePower"] = bufferActivePower;
  doc["voltage"] = bufferVoltage;
  doc["current"] = bufferCurrent;
  doc["apparentPower"] = bufferApparentPower;
  doc["powerFactor"] = bufferPowerFactor;

  serializeJson(doc, json);
  mqtt.publish(TOPIC_json, json);
}

void setup() {
  //WIFI Flash ***Dont  Touch***
  Serial.begin(SERIAL_BAUDRATE);
  setup_wifi();
  // while (WiFi.status() != WL_CONNECTED) { delay(500); }
  // Serial.println(WiFi.localIP());
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", INDEX_HTML);
  });
  server.on("/update", HTTP_POST, []() {
    bool ok = !Update.hasError();
    server.send(200, "text/plain", ok ? "OK" : "FAIL");
    delay(100);
    if (ok) ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      Update.end(true);
    }
  });
  
  server.begin();

  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(callback);

  //GPIO Setup
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);   // Relay default OFF
  digitalWrite(LED_PIN, LOW);   // LED default OFF

  //HLW8012 Setup
  hlw8012.begin(
    CF_PIN,
    CF1_PIN,
    SEL_PIN,
    CURRENT_MODE,
    false,
    500000
  );

  hlw8012.setResistors(
    CURRENT_RESISTOR,
    VOLTAGE_RESISTOR_UPSTREAM,
    VOLTAGE_RESISTOR_DOWNSTREAM
  );

  calibrate();
}

void loop() {
  server.handleClient();
  
  reconnect();

  if (mqtt.connected()) {
    mqtt.loop();
  }

  unsigned long currentMillis = millis();
  if (currentMillis - prevMillis >= intervalPengiriman*1000) {
    activePower = hlw8012.getActivePower();
    voltage = hlw8012.getVoltage();
    current = hlw8012.getCurrent();
    apparentPower = hlw8012.getApparentPower();
    powerFactor = (int)(100 * hlw8012.getPowerFactor());
    
    Serial.println("activePower: " + String(activePower));
    Serial.println("voltage" + String(voltage));
    Serial.println("current: " + String(current));
    Serial.println("apparentPower" + String(apparentPower));
    Serial.println("powerFactor: " + String(powerFactor));
    Serial.println();

    hlw8012.toggleMode();

    //publish_json();
     if (mqtt.connected()) {
      // publish_data();
      publish_json();
    }

    prevMillis = currentMillis;
  }
  //millis(5);
}
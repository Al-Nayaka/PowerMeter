#include <Arduino.h>
#include "HLW8012.h"


#define SERIAL_BAUDRATE 115200


// ================= GPIO =================
#define RELAY_PIN 25
#define SEL_PIN 26
#define CF1_PIN 13
#define CF_PIN 34
#define LED_PIN 32


// ================= HLW8012 CONFIG =================
#define UPDATE_TIME 2000        // ms
#define CURRENT_MODE HIGH


#define CURRENT_RESISTOR 0.001
#define VOLTAGE_RESISTOR_UPSTREAM (5 * 470000)  // 2280k
#define VOLTAGE_RESISTOR_DOWNSTREAM (1000)      // 1k


HLW8012 hlw8012;


// ================= SERIAL COMMAND =================
String serialCommand = "";


// ================= HELPER =================
void unblockingDelay(unsigned long mseconds) {
  unsigned long timeout = millis();
  while ((millis() - timeout) < mseconds) delay(1);
}


// ================= SERIAL HANDLER =================
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();


    if (c == '\n' || c == '\r') {
      serialCommand.trim();
      serialCommand.toUpperCase();


      if (serialCommand == "NYALA") {
        digitalWrite(RELAY_PIN, LOW);
        digitalWrite(LED_PIN, HIGH);
        Serial.println("[RELAY] NYALA");
      }
      else if (serialCommand == "MATI") {
        digitalWrite(RELAY_PIN, HIGH);
        digitalWrite(LED_PIN, LOW);
        Serial.println("[RELAY] MATI");
      }
      else if (serialCommand.length() > 0) {
        Serial.println("[ERROR] Perintah tidak dikenal");
      }


      serialCommand = "";
    }
    else {
      serialCommand += c;
    }
  }
}


// ================= CALIBRATION (OPTIONAL) =================
void calibrate() {


  hlw8012.getActivePower();


  hlw8012.setMode(MODE_CURRENT);
  unblockingDelay(2000);
  hlw8012.getCurrent();


  hlw8012.setMode(MODE_VOLTAGE);
  unblockingDelay(2000);
  hlw8012.getVoltage();


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


// ================= SETUP =================
void setup() {

  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  Serial.println("=== HLW8012 + Relay Serial Control ===");


  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);   // Relay default OFF
  digitalWrite(LED_PIN, LOW);   // LED default OFF


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


  Serial.print("[HLW] Default current multiplier : ");
  Serial.println(hlw8012.getCurrentMultiplier());
  Serial.print("[HLW] Default voltage multiplier : ");
  Serial.println(hlw8012.getVoltageMultiplier());
  Serial.print("[HLW] Default power multiplier   : ");
  Serial.println(hlw8012.getPowerMultiplier());
  Serial.println();

  calibrate();

  Serial.println("Ketik: NYALA atau MATI lalu tekan ENTER");
}


// ================= LOOP =================
void loop() {


  handleSerial();


  static unsigned long last = millis();


  if ((millis() - last) > UPDATE_TIME) {


    last = millis();


    Serial.print("[HLW] Active Power (W)    : ");
    Serial.println(hlw8012.getActivePower());


    Serial.print("[HLW] Voltage (V)         : ");
    Serial.println(hlw8012.getVoltage());


    Serial.print("[HLW] Current (A)         : ");
    Serial.println(hlw8012.getCurrent());


    Serial.print("[HLW] Apparent Power (VA) : ");
    Serial.println(hlw8012.getApparentPower());


    Serial.print("[HLW] Power Factor (%)    : ");
    Serial.println((int)(100 * hlw8012.getPowerFactor()));


    Serial.println();


    hlw8012.toggleMode();
  }
}

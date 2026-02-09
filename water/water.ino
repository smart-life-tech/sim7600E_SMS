/*********************************************************** 
 * SafeWater Guardian with Push Button SMS Test
 * LilyGo A7670G (Telcel SIM)
 * Sensors: Turbidity, TDS, pH
 * Traffic Light: Red=Alert, Yellow=Sending, Green=Safe
 ************************************************************/

#define TINY_GSM_MODEM_SIM7080  // A7670G uses SIM7080 library
#define TINY_GSM_DEBUG Serial
#include <TinyGsmClient.h>

/* ================== MODEM PINS ================== */
#define MODEM_TX        17  // Changed from 27 (Yellow conflict)
#define MODEM_RX        16  // Changed from 26 (Red conflict)
#define MODEM_PWRKEY    4
#define MODEM_FLIGHT    25
#define MODEM_STATUS    34

#define MODEM_BAUD      115200
#define modemSerial     Serial1
TinyGsm modem(modemSerial);

/* ================== SENSOR PINS ================== */
#define PIN_TURB  33
#define PIN_TDS   13   // your TDS pin
#define PIN_PH    34

/* ================== PUSH BUTTON ================== */
#define BUTTON_PIN 14   // Keyestudio button S pin

/* ================== TRAFFIC LIGHT PINS ================== */
#define RED_PIN     26
#define YELLOW_PIN  27
#define GREEN_PIN   12

/* ================== SMS RECIPIENTS ================== */
const char *recipients[] = {
  "+19563227945",
  "+19568784196"
};

/* ================== THRESHOLDS ================== */
#define TURB_SAFE_MAX     2300
#define TURB_CAUTION_MAX  2700

#define TDS_SAFE_MAX      500
#define TDS_CAUTION_MAX   900

#define PH_LOW_DANGER     500
#define PH_HIGH_DANGER    3500

/* ================== GLOBALS ================== */
int lastSignal = -1;
unsigned long lastSignalCheck = 0;

/* ================== FUNCTIONS ================== */

int readAnalogAvg(int pin, int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(5);
  }
  return sum / samples;
}

String getWarningLevel(int turb, int tds, int ph) {
  if (turb > TURB_CAUTION_MAX || tds > TDS_CAUTION_MAX ||
      ph < PH_LOW_DANGER || ph > PH_HIGH_DANGER) {
    return "DANGER";
  }
  if (turb > TURB_SAFE_MAX || tds > TDS_SAFE_MAX) {
    return "CAUTION";
  }
  return "SAFE";
}

bool ensureNetworkConnected(uint32_t timeoutMs = 90000) {
  Serial.println("\n=== CHECKING NETWORK ===");
  
  // Check signal first
  int16_t rssi = modem.getSignalQuality();
  Serial.print("Signal quality (RSSI): ");
  Serial.print(rssi);
  Serial.println(rssi == 99 ? " (no signal)" : rssi < 10 ? " (weak)" : " (ok)");
  
  if (rssi == 99 || rssi == 0) {
    Serial.println(" No signal detected - check antenna/SIM");
    return false;
  }
  
  // Get registration status via AT command
  Serial.print("GSM Registration: ");
  modem.sendAT("+CREG?");
  modem.waitResponse(2000);
  
  // Try network registration
  Serial.println("Waiting for network registration...");
  unsigned long start = millis();
  bool connected = false;
  
  while (millis() - start < timeoutMs) {
    if (modem.isNetworkConnected()) {
      connected = true;
      break;
    }
    Serial.print(".");
    delay(2000);
  }
  Serial.println();
  
  if (!connected) {
    Serial.println(" Network registration failed");
    Serial.println("Current operator selection:");
    modem.sendAT("+COPS?");
    modem.waitResponse(5000);
    return false;
  }
  
  Serial.print("✓ Registered to: ");
  Serial.println(modem.getOperator());
  Serial.print("✓ Signal: ");
  Serial.println(modem.getSignalQuality());
  return true;
}

bool checkSignal(uint32_t timeoutMs = 5000) {
  int16_t rssi = modem.getSignalQuality();
  
  if (rssi == 99 || rssi == 0) {
    Serial.print("⚠ Signal lost (RSSI: ");
    Serial.print(rssi);
    Serial.println(")");
    return false;
  }
  
  if (lastSignal != rssi) {
    Serial.print("Signal: ");
    Serial.println(rssi);
    lastSignal = rssi;
  }
  
  return true;
}

void setTrafficLight(int red, int yellow, int green) {
  digitalWrite(RED_PIN, red);
  digitalWrite(YELLOW_PIN, yellow);
  digitalWrite(GREEN_PIN, green);
}

void sendSMS(const char *message) {
  setTrafficLight(0, 1, 0);  // Yellow = sending
  
  for (int i = 0; i < 2; i++) {
    bool sent = false;
    for (int attempt = 1; attempt <= 3 && !sent; attempt++) {
      Serial.print("Sending SMS to ");
      Serial.print(recipients[i]);
      Serial.print(" (attempt ");
      Serial.print(attempt);
      Serial.println(")");

      sent = modem.sendSMS(recipients[i], message);
      Serial.println(sent ? "✓ SMS sent" : "✗ SMS failed");

      if (!sent) {
        delay(5000);
      }
    }
    delay(2000);
  }
  
  setTrafficLight(0, 0, 1);  // Green = idle
}

void logModemStatus() {
  Serial.println("\n=== MODEM STATUS ===");
  
  Serial.print("Modem info: ");
  String info = modem.getModemInfo();
  Serial.println(info.length() > 0 ? info : "No response");

  Serial.print("SIM status: ");
  int simStatus = modem.getSimStatus();
  Serial.print(simStatus);
  Serial.println(simStatus == 1 ? " (READY)" : simStatus == 2 ? " (PIN required)" : " (ERROR)");
  
  if (simStatus != 1) {
    Serial.println(" SIM not ready!");
  }

  Serial.print("IMEI: ");
  Serial.println(modem.getIMEI());
  
  Serial.print("Operator: ");
  String op = modem.getOperator();
  Serial.println(op.length() > 0 ? op : "Not registered");
  
  Serial.println("==================\n");
}

void setup() {
  Serial.begin(115200);
  Serial.println("starting modem...");
  delay(1000);

  pinMode(RED_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Changed to INPUT_PULLUP
  
  setTrafficLight(0, 1, 0);  // Yellow = initialization

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_TURB, ADC_11db);
  analogSetPinAttenuation(PIN_TDS, ADC_11db);
  analogSetPinAttenuation(PIN_PH,  ADC_11db);

  modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1200);
  digitalWrite(MODEM_PWRKEY, HIGH);

  pinMode(MODEM_FLIGHT, OUTPUT);
  digitalWrite(MODEM_FLIGHT, LOW); // LOW = RF enabled (flight mode off)

  Serial.println("\n=== INITIALIZING MODEM ===");
  Serial.println("Restarting modem...");
  if (!modem.restart()) {
    Serial.println(" Modem restart failed!");
  } else {
    Serial.println(" Modem restarted");
  }
  
  delay(2000);
  
  Serial.println("\nSetting network mode to auto (GSM+LTE)...");
  modem.setNetworkMode(51);  // 51=GSM+LTE auto
  delay(1000);
  
  // Telcel APN configuration
  Serial.println("Configuring Telcel APN...");
  modem.sendAT("+CGDCONT=1,\"IP\",\"internet.itelcel.com\"");
  if (modem.waitResponse() == 1) {
    Serial.println(" APN set to internet.itelcel.com");
  } else {
    Serial.println(" APN setting failed");
  }
  
  delay(1000);
  
  Serial.println("\n--- AT Command Diagnostics ---");
  
  Serial.print("SIM PIN check: ");
  modem.sendAT("+CPIN?");
  modem.waitResponse(2000);
  
  Serial.print("Signal quality: ");
  modem.sendAT("+CSQ");
  modem.waitResponse(2000);
  
  Serial.print("Network registration (GSM): ");
  modem.sendAT("+CREG?");
  modem.waitResponse(2000);
  
  Serial.print("Network registration (GPRS): ");
  modem.sendAT("+CGREG?");
  modem.waitResponse(2000);
  
  Serial.print("Network registration (LTE): ");
  modem.sendAT("+CEREG?");
  modem.waitResponse(2000);
  
  Serial.print("Operator selection: ");
  modem.sendAT("+COPS?");
  modem.waitResponse(5000);
  
  Serial.println("--- End Diagnostics ---\n");

  logModemStatus();

  if (!ensureNetworkConnected()) {
    Serial.println(" Network not connected - SMS will not work!");
    Serial.println("Troubleshooting:");
    Serial.println("1. Check SIM card is inserted correctly");
    Serial.println("2. Verify SIM has active service with Telcel");
    Serial.println("3. Check antenna is connected");
    Serial.println("4. Try SIM in phone to confirm it works");
    setTrafficLight(1, 0, 0);  // Red = error
  } else {
    Serial.println(" Network connected successfully! ");
    setTrafficLight(0, 0, 1);  // Green = ready
  }

  Serial.println("Setting SMS text mode...");
  modem.sendAT("+CMGF=1"); // SMS text mode
  if (modem.waitResponse() == 1) {
    Serial.println(" SMS mode configured");
  }
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║   System Ready                 ║");
  Serial.println("╚════════════════════════════════╝\n");
}

/* ================== LOOP ================== */
void loop() {
  // Check signal regularly
  if (millis() - lastSignalCheck > 30000) {  // Every 30 seconds
    checkSignal();
    lastSignalCheck = millis();
  }
  
  int turb = readAnalogAvg(PIN_TURB);
  int tds  = readAnalogAvg(PIN_TDS);
  int ph   = readAnalogAvg(PIN_PH);

  String status = getWarningLevel(turb, tds, ph);

  Serial.println("-------------");
  Serial.print("Turbidity: "); Serial.println(turb);
  Serial.print("TDS: "); Serial.println(tds);
  Serial.print("pH: "); Serial.println(ph);
  Serial.print("STATUS: "); Serial.println(status);

  // ===== BUTTON TEST SMS =====
  if (digitalRead(BUTTON_PIN) == LOW) {  // Changed from HIGH (active LOW with INPUT_PULLUP)
    Serial.println("\n>>> Button pressed - sending test SMS");
    setTrafficLight(0, 1, 0);  // Yellow

    char testMsg[240];
    snprintf(testMsg, sizeof(testMsg),
      "SafeWater Guardian TEST\n"
      "Status: %s\n"
      "Turbidity: %d\n"
      "TDS: %d\n"
      "pH: %d",
      status.c_str(), turb, tds, ph);

    if (ensureNetworkConnected()) {
      sendSMS(testMsg);
    }

    delay(3000); // debounce
  }

  // ===== AUTOMATIC ALERT =====
  if (status != "SAFE") {
    setTrafficLight(1, 0, 0);  // Red alert
    delay(1000);

    char message[240];
    snprintf(message, sizeof(message),
      "SafeWater Guardian ALERT\n"
      "Status: %s\n"
      "Turbidity: %d\n"
      "TDS: %d\n"
      "pH: %d",
      status.c_str(), turb, tds, ph);

    if (ensureNetworkConnected()) {
      sendSMS(message);
    }

    setTrafficLight(0, 0, 1);  // Back to green
  } else {
    setTrafficLight(0, 0, 1);  // Green = safe
  }

  delay(10000); // check every 10 seconds
}


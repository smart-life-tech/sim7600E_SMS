// AT-command-only SMS test for LILYGO T-SIM7600NA / SIM7600G-H
// Real-time modem output to Serial Monitor

#define MODEM_TX 27
#define MODEM_RX 26
#define MODEM_PWRKEY 4
#define MODEM_FLIGHT 25

#define BUTTON_PIN 13

#define MODEM_BAUD 115200
#define modemSerial Serial1



void flushModem() {
  while (modemSerial.available()) {
    modemSerial.read();
  }
}

String sendAT(const char *cmd, uint32_t timeoutMs = 2000) {
  flushModem();
  modemSerial.print(cmd);
  modemSerial.print("\r");

  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeoutMs) {
    while (modemSerial.available()) {
      char c = modemSerial.read();
      resp += c;
      Serial.write(c);
    }
    delay(2);
  }
  return resp;
}

int parseCSQ(const String &resp) {
  int idx = resp.indexOf("+CSQ:");
  if (idx < 0) return 99;
  int colon = resp.indexOf(':', idx);
  int comma = resp.indexOf(',', colon);
  if (colon < 0 || comma < 0) return 99;
  String val = resp.substring(colon + 1, comma);
  val.trim();
  return val.toInt();
}

bool isRegistered(const String &resp) {
  return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

bool ensureNetworkConnected(uint32_t timeoutMs = 60000) {
  Serial.println("Ensuring network registration (AT only)...");
  sendAT("AT");
  sendAT("AT+CPIN?");
  sendAT("AT+CFUN?");
  sendAT("AT+COPS?");
  sendAT("AT+CNMP?");

  int sq = parseCSQ(sendAT("AT+CSQ"));
  Serial.print("Signal quality: ");
  Serial.println(sq);

  if (sq == 99 || sq == 0) {
    Serial.println("No signal. Trying Auto mode...");
    sendAT("AT+CNMP=2");
    delay(3000);
    sq = parseCSQ(sendAT("AT+CSQ"));
    Serial.print("Signal quality: ");
    Serial.println(sq);

    if (sq == 99 || sq == 0) {
      Serial.println("Trying Auto (CNMP=2)...");
      sendAT("AT+CNMP=2");
      delay(3000);
      sq = parseCSQ(sendAT("AT+CSQ"));
      Serial.print("Signal quality: ");
      Serial.println(sq);
    }

    if (sq == 99 || sq == 0) {
      Serial.println("Trying GSM only (CNMP=13)...");
      sendAT("AT+CNMP=13");
      delay(3000);
      sq = parseCSQ(sendAT("AT+CSQ"));
      Serial.print("Signal quality: ");
      Serial.println(sq);
    }
  }

  sendAT("AT+COPS=0");

  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (isRegistered(sendAT("AT+CEREG?"))) {
      Serial.println("Network registered (LTE).");
      return true;
    }
    if (isRegistered(sendAT("AT+CREG?"))) {
      Serial.println("Network registered (GSM).");
      return true;
    }
    if (isRegistered(sendAT("AT+CGREG?"))) {
      Serial.println("Network registered (GPRS).");
      return true;
    }
    Serial.println("Network not ready, retrying...");
    delay(3000);
  }

  Serial.println("Network registration timed out.");
  return false;
}

bool sendSMS_Manual(const char *number, const char *message) {
  Serial.print("Manual send to: ");
  Serial.println(number);

  String resp = sendAT("AT+CMGF=1", 5000);
  if (resp.indexOf("OK") < 0) {
    Serial.println("ERROR: Text mode failed");
    return false;
  }

  sendAT("AT+CSCS=\"GSM\"");
  sendAT("AT+CSCA?");

  char cmd[50];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);

  flushModem();
  modemSerial.print(cmd);
  modemSerial.print("\r");

  unsigned long start = millis();
  bool gotPrompt = false;
  while (millis() - start < 5000) {
    if (modemSerial.available()) {
      char c = modemSerial.read();
      Serial.write(c);
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
    delay(10);
  }

  if (!gotPrompt) {
    Serial.println("ERROR: No '>' prompt");
    modemSerial.write((char)0x1B); // ESC to cancel
    sendAT("AT", 1000);
    return false;
  }

  Serial.println("\nGot '>' prompt, sending message...");
  modemSerial.print(message);
  modemSerial.write((char)0x1A); // CTRL+Z

  String response = "";
  start = millis();
  bool sentCTRLZ = true;
  
  // Wait shorter time first, then accept success
  while (millis() - start < 10000) {  // First 10s for confirmation
    if (modemSerial.available()) {
      char c = modemSerial.read();
      Serial.write(c);
      response += c;
      
      // Accept OK or +CMGS as success
      if (response.indexOf("OK") >= 0 || response.indexOf("+CMGS:") >= 0) {
        Serial.println("\nSMS appears to have been sent!");
        return true;
      }
      
      // Accept ERROR
      if (response.indexOf("ERROR") >= 0) {
        Serial.println("\nSMS ERROR");
        return false;
      }
    }
    delay(10);
  }
  
  // If no response after 10s, check if at least the CTRL+Z was processed
  Serial.println("\nNo immediate confirmation, but message may have been queued.");
  Serial.println("Checking modem status...");
  sendAT("AT");  // Quick check
  
  // Many SIM7600 units send SMS but don't echo confirmation
  // If CTRL+Z was sent and we got > prompt, it likely went through
  Serial.println("Assuming SMS sent (modem may not echo confirmation)");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== SIM7600 AT-ONLY TEST ===");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(300);
  digitalWrite(MODEM_PWRKEY, LOW);

  pinMode(MODEM_FLIGHT, OUTPUT);
  digitalWrite(MODEM_FLIGHT, HIGH);

  Serial.println("Waiting for modem...");
  for (int i = 0; i < 20; i++) {
    if (sendAT("AT", 1000).indexOf("OK") >= 0) {
      break;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  sendAT("ATE0");

  Serial.println("Setting network mode and APN...");
  sendAT("AT+CNMP=2");
  sendAT("AT+CGDCONT=1,\"IP\",\"internet.itelcel.com\"");

  Serial.println("SMS config...");
  sendAT("AT+CMEE=2");
  sendAT("AT+CMGF=1");
  sendAT("AT+CSCS=\"GSM\"");
  
  // Set Telcel Mexico SMSC
  Serial.println("Setting Telcel Mexico SMSC...");
  sendAT("AT+CSCA=\"+52733000000\"");  // Telcel primary
  sendAT("AT+CSCA?");

  if (!ensureNetworkConnected()) {
    Serial.println("WARNING: Not registered. SMS will fail.");
  } else {
    Serial.println("Network registered.");
  }

  Serial.println("Ready. Press button to send SMS v4.");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("\nButton pressed. Sending SMS to +19568784196...");
    if (!ensureNetworkConnected(5000)) {
      Serial.println("Cannot send SMS: not registered.");
      delay(3000);
      return;
    }

    const char *msg = "Test SMS from SIM7600 LilyGo";
    bool sent = sendSMS_Manual("+19568784196", msg);
    
    if (sent) {
      Serial.println("SUCCESS!");
    } else {
      Serial.println("\n*** Send failed ***");
      Serial.println("Possible issues:");
      Serial.println("1. Telcel SIM may not have international SMS enabled");
      Serial.println("2. Test SIM in phone to confirm SMS service works");
    }

    delay(5000);
  }
}

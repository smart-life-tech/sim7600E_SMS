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
  Serial.print("\n[SMS] === SENDING TO: ");
  Serial.print(number);
  Serial.println(" ===");

  // STEP 1: Set local SMS center (SMSC) - following exact protocol
  Serial.println("[SMS] Step 1: Setting SMSC...");
  String smsc_set = sendAT("AT+CSCA=\"+5294100001410\"", 3000);
  Serial.println(smsc_set);
  
  if (smsc_set.indexOf("OK") < 0) {
    Serial.println("[SMS] WARNING: SMSC set may have failed, continuing...");
  }
  
  // Verify SMSC was set
  String smsc_check = sendAT("AT+CSCA?", 2000);
  Serial.print("[SMS] Current SMSC: ");
  Serial.println(smsc_check);

  // STEP 2: Set text mode
  Serial.println("[SMS] Step 2: Setting TEXT mode...");
  String textMode = sendAT("AT+CMGF=1", 3000);
  if (textMode.indexOf("OK") < 0) {
    Serial.println("[SMS] ERROR: Text mode failed");
    return false;
  }
  Serial.println("[SMS] Text mode OK");

  // STEP 3: Set character set
  Serial.println("[SMS] Step 3: Setting character set...");
  sendAT("AT+CSCS=\"GSM\"", 2000);

  // STEP 4: Send CMGS with phone number
  Serial.println("[SMS] Step 4: Sending AT+CMGS command...");
  char cmd[70];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
  
  Serial.print("[SMS] Command: ");
  Serial.println(cmd);
  
  flushModem();
  modemSerial.print(cmd);
  modemSerial.print("\r");

  // STEP 5: Wait for > prompt
  Serial.println("[SMS] Step 5: Waiting for '>' prompt...");
  unsigned long start = millis();
  bool gotPrompt = false;
  
  while (millis() - start < 10000) {
    if (modemSerial.available()) {
      char c = modemSerial.read();
      Serial.write(c);
      
      if (c == '>') {
        gotPrompt = true;
        Serial.println("\n[SMS] *** GOT > PROMPT ***");
        break;
      }
    }
    delay(5);
  }

  if (!gotPrompt) {
    Serial.println("[SMS] ERROR: No '>' prompt - aborting");
    modemSerial.write((char)0x1B); // Send ESC to cancel
    delay(200);
    sendAT("AT", 1000);
    return false;
  }

  // STEP 6: Send the message text
  Serial.println("[SMS] Step 6: Sending message text...");
  delay(300);
  
  int msgLen = strlen(message);
  Serial.print("[SMS] Sending ");
  Serial.print(msgLen);
  Serial.println(" characters:");
  
  // Send all message characters
  modemSerial.print(message);
  Serial.print(message);
  Serial.println();
  
  delay(300);

  // STEP 7: Send CTRL+Z (0x1A hex) to submit
  Serial.println("[SMS] Step 7: Sending CTRL+Z (0x1A)...");
  modemSerial.write(0x1A);  // Send as raw byte 0x1A
  Serial.println("[SMS] CTRL+Z sent");

  // STEP 8: Wait for +CMGS response or OK
  Serial.println("[SMS] Step 8: Waiting for confirmation...");
  String response = "";
  start = millis();
  
  while (millis() - start < 25000) {  // Wait up to 25 seconds
    if (modemSerial.available()) {
      char c = modemSerial.read();
      Serial.write(c);
      response += c;
      
      // Look for +CMGS: (message ID response)
      if (response.indexOf("+CMGS:") >= 0) {
        Serial.println("\n[SMS] *** SUCCESS - Got +CMGS response ***");
        return true;
      }
      
      // Also accept bare OK
      if (response.indexOf("OK") >= 0 && response.indexOf("+CMGS:") < 0) {
        Serial.println("\n[SMS] *** SUCCESS - Got OK ***");
        return true;
      }
      
      // Check for error
      if (response.indexOf("ERROR") >= 0) {
        Serial.println("\n[SMS] *** FAILED - Got ERROR response ***");
        return false;
      }
    }
    delay(10);
  }
  
  // Timeout after 25 seconds
  Serial.println("\n[SMS] *** TIMEOUT - No confirmation after 25 seconds ***");
  Serial.println("[SMS] Sending AT to check modem state...");
  delay(300);
  sendAT("AT", 2000);
  
  return false;
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
  
  // Set Telcel Mexico SMSC for SIM7600NA
  Serial.println("Setting Telcel Mexico SMSC for SIM7600NA...");
  // SIM7600NA may use different SMSC format
  // Try the actual Telcel service center numbers
  sendAT("AT+CSCA=\"+5294100001410\"");  // Telcel main
  delay(200);
  String csca = sendAT("AT+CSCA?", 2000);
  Serial.println("Current SMSC setting:");
  Serial.println(csca);

  if (!ensureNetworkConnected()) {
    Serial.println("WARNING: Not registered. SMS will fail.");
  } else {
    Serial.println("Network registered.");
  }

  Serial.println("Ready. Press button to send SMS (SIM7600NA).");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("\n=== BUTTON PRESSED (SIM7600NA) ===");
    
    // Re-check network before sending
    int sq = parseCSQ(sendAT("AT+CSQ"));
    Serial.print("Current signal: ");
    Serial.println(sq);
    
    if (!ensureNetworkConnected(10000)) {
      Serial.println("Cannot send SMS: not registered.");
      delay(3000);
      return;
    }

    const char *msg = "Test SMS from SIM7600 LilyGo";
    const char *number = "+19568784196";
    
    Serial.print("Attempting SMS to: ");
    Serial.println(number);
    
    // Try text mode first
    bool sent = sendSMS_Manual(number, msg);
    
    if (sent) {
      Serial.println("\n*** SUCCESS - SMS SUBMITTED (Text Mode) ***");
    } else {
      Serial.println("\n*** Text mode failed, trying PDU mode... ***");
      // Try PDU mode as fallback for SIM7600NA
      sent = sendSMS_PDU(number, msg);
      
      if (sent) {
        Serial.println("\n*** SUCCESS - SMS SUBMITTED (PDU Mode) ***");
      } else {
        Serial.println("\n*** FAILED - SMS NOT SENT (both modes) ***");
        Serial.println("Diagnostics for SIM7600NA:");
        Serial.println("1. Verify Telcel SIM has international SMS enabled");
        Serial.println("2. Check if phone can send to +19568784196");
        Serial.println("3. Try Mexican number: +52 format");
        Serial.println("4. Check modem power (needs 2A at 5V)");
      }
    }

    // Prevent repeated presses
    delay(5000);
  }
}

bool sendSMS_PDU(const char *number, const char *message) {
  Serial.println("[SMS-PDU] Switching to PDU mode...");
  
  // Switch to PDU mode
  String resp = sendAT("AT+CMGF=0", 5000);
  if (resp.indexOf("OK") < 0) {
    Serial.println("[SMS-PDU] ERROR: Could not switch to PDU mode");
    return false;
  }
  
  Serial.println("[SMS-PDU] PDU mode enabled");
  
  // For now, switch back and report
  sendAT("AT+CMGF=1", 2000);  // Back to text mode
  
  Serial.println("[SMS-PDU] PDU mode not fully implemented yet");
  return false;
}

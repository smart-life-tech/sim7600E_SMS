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
  Serial.print("\n[SMS] Sending to: ");
  Serial.println(number);

  // Step 1: Text mode
  String resp = sendAT("AT+CMGF=1", 5000);
  if (resp.indexOf("OK") < 0) {
    Serial.println("[SMS] ERROR: Text mode failed");
    return false;
  }
  Serial.println("[SMS] Text mode: OK");

  // Step 2: Character set
  sendAT("AT+CSCS=\"GSM\"");
  
  // Step 3: Check SMSC
  String smsc_resp = sendAT("AT+CSCA?", 2000);
  Serial.print("[SMS] SMSC: ");
  Serial.println(smsc_resp);

  // Step 4: Send CMGS command
  char cmd[60];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
  
  Serial.print("[SMS] Sending: ");
  Serial.println(cmd);
  
  flushModem();
  modemSerial.print(cmd);
  modemSerial.print("\r");

  // Step 5: Wait for > prompt
  unsigned long start = millis();
  bool gotPrompt = false;
  String promptResp = "";
  
  while (millis() - start < 5000) {
    if (modemSerial.available()) {
      char c = modemSerial.read();
      Serial.write(c);
      promptResp += c;
      
      if (c == '>') {
        gotPrompt = true;
        Serial.println("\n[SMS] GOT > PROMPT");
        break;
      }
    }
    delay(10);
  }

  if (!gotPrompt) {
    Serial.println("[SMS] ERROR: No > prompt received");
    Serial.println("[SMS] Response was:");
    Serial.println(promptResp);
    modemSerial.write((char)0x1B); // ESC to cancel
    delay(100);
    sendAT("AT", 1000);
    return false;
  }

  // Step 6: Send message text
  Serial.println("[SMS] NOW SENDING MESSAGE TEXT...");
  delay(200);  // Wait after > before sending text
  
  int msgLen = strlen(message);
  Serial.print("[SMS] Message length: ");
  Serial.println(msgLen);
  Serial.print("[SMS] Message content: ");
  Serial.println(message);
  
  // Send message character by character with feedback
  for (int i = 0; i < msgLen; i++) {
    modemSerial.write((uint8_t)message[i]);
    Serial.write(message[i]);
  }
  Serial.println();
  Serial.println("[SMS] Message text sent");
  
  delay(300);  // Let modem process
  
  // Step 7: Send CTRL+Z (0x1A)
  Serial.println("[SMS] SENDING CTRL+Z (0x1A)...");
  modemSerial.write(0x1A);
  Serial.println("[SMS] CTRL+Z sent");

  // Step 8: Wait for completion
  String response = "";
  start = millis();
  
  Serial.println("[SMS] Waiting for response (max 15s)...");
  while (millis() - start < 15000) {
    if (modemSerial.available()) {
      char c = modemSerial.read();
      Serial.write(c);
      response += c;
      
      // Check for success indicators
      if (response.indexOf("OK") >= 0) {
        Serial.println("\n[SMS] *** SUCCESS - Got OK ***");
        return true;
      }
      
      if (response.indexOf("+CMGS:") >= 0) {
        Serial.println("\n[SMS] *** SUCCESS - Got +CMGS ***");
        return true;
      }
      
      // Check for error
      if (response.indexOf("ERROR") >= 0) {
        Serial.println("\n[SMS] *** FAILED - Got ERROR ***");
        return false;
      }
    }
    delay(5);
  }
  
  // Timeout
  Serial.println("\n[SMS] *** TIMEOUT - No response after 15 seconds ***");
  Serial.println("[SMS] Sending AT to recover...");
  delay(500);
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
  
  // Set Telcel Mexico SMSC
  Serial.println("Setting Telcel Mexico SMSC...");
  sendAT("AT+CSCA=\"+52733000000\"");  // Telcel primary
  sendAT("AT+CSCA?");

  if (!ensureNetworkConnected()) {
    Serial.println("WARNING: Not registered. SMS will fail.");
  } else {
    Serial.println("Network registered.");
  }

  Serial.println("Ready. Press button to send SMS v4.6 test.");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("\n=== BUTTON PRESSED ===");
    
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
    
    bool sent = sendSMS_Manual(number, msg);
    
    if (sent) {
      Serial.println("\n*** SUCCESS - SMS SUBMITTED ***");
    } else {
      Serial.println("\n*** FAILED - SMS NOT SENT ***");
      Serial.println("Diagnostics:");
      Serial.println("- Check Telcel SIM has international SMS enabled");
      Serial.println("- Verify with your phone first: can you text +19568784196?");
      Serial.println("- Check recipient number format (US numbers may be blocked)");
    }

    // Prevent repeated presses
    delay(5000);
  }
}

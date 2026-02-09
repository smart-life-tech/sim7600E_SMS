#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_DEBUG Serial
#include <TinyGsmClient.h>

#define UART_BAUD 115200

#define MODEM_TX 27
#define MODEM_RX 26
#define MODEM_PWRKEY 4
#define MODEM_DTR 32
#define MODEM_RI 33
#define MODEM_FLIGHT 25
#define MODEM_STATUS 34

#define LED_PIN 12
#define BUZZER_PIN 21 //  Moved from pin 27 to avoid modem conflict
#define BUTTON_PIN 13 // Button on GPIO 13

#define MODEM_BAUD 115200
#define modemSerial Serial1
TinyGsm modem(modemSerial);

float lat, lon = 0.0;
bool fetch = false;

//  Two recipient numbers
const char *recipients[] = {
    "+19568784196",
    "+19563227945"  // Fixed: removed extra 1 at beginning
};

void printAt(const char *label, const char *cmd, uint32_t timeoutMs = 2000)
{
    Serial.print(label);
    modem.sendAT(cmd);
    modem.waitResponse(timeoutMs);
}

// Ensure the modem is actually registered before sending SMS
bool ensureNetworkConnected(uint32_t timeoutMs = 60000)
{
    Serial.println("Ensuring network registration...");
    printAt("SIM status: ", "+CPIN?");
    printAt("RF state: ", "+CFUN?");
    printAt("Reg (LTE): ", "+CEREG?");
    printAt("Reg (GSM): ", "+CREG?");
    printAt("Reg (GPRS): ", "+CGREG?");
    printAt("Operator: ", "+COPS?");
    printAt("Network mode: ", "+CNMP?");

    int16_t sq = modem.getSignalQuality();
    Serial.print("Signal quality: ");
    Serial.println(sq);

    if (sq == 99 || sq == 0)
    {
        Serial.println("No signal. Forcing RF on and trying network modes...");
        modem.sendAT("+CFUN=1");
        modem.waitResponse(1000);

        Serial.println("Trying LTE only (CNMP=38)...");
        modem.sendAT("+CNMP=38");
        modem.waitResponse(1000);
        delay(3000);

        sq = modem.getSignalQuality();
        Serial.print("Signal quality: ");
        Serial.println(sq);

        if (sq == 99 || sq == 0)
        {
            Serial.println("Trying Auto (CNMP=2)...");
            modem.sendAT("+CNMP=2");
            modem.waitResponse(1000);
            delay(3000);
        }

        sq = modem.getSignalQuality();
        Serial.print("Signal quality: ");
        Serial.println(sq);

        if (sq == 99 || sq == 0)
        {
            Serial.println("Trying GSM only (CNMP=13)...");
            modem.sendAT("+CNMP=13");
            modem.waitResponse(1000);
            delay(3000);
        }
    }

    unsigned long start = millis();
    while ((millis() - start) < timeoutMs)
    {
        int16_t sqLoop = modem.getSignalQuality();
        Serial.print("Signal quality: ");
        Serial.println(sqLoop);

        if (modem.isNetworkConnected())
        {
            Serial.println("Network registered.");
            return true;
        }

        if (modem.waitForNetwork(15000))
        {
            Serial.println("Network registered.");
            return true;
        }

        Serial.println("Network not ready, retrying...");
    }

    Serial.println("Network registration timed out.");
    return false;
}

// ⏱ Wait for GPS fix
bool waitForGPS(int timeout_sec = 60)
{
    Serial.println("Waiting for GPS fix...");
    unsigned long start = millis();
    while ((millis() - start) < (timeout_sec * 1000))
    {
        if (modem.getGPS(&lat, &lon))
        {
            Serial.print("GPS Fix acquired after ");
            Serial.print((millis() - start) / 1000);
            Serial.println(" seconds");
            fetch = true;
            return true;
        }

        if ((millis() - start) % 5000 < 100)
        {
            Serial.print("Waiting for GPS fix... ");
            Serial.print((millis() - start) / 1000);
            Serial.println(" seconds elapsed");
        }
        delay(100);
    }
    Serial.println("GPS fix timeout!");
    return false;
}

//  Send SMS with raw AT commands (bypass TinyGSM)
bool sendSMS_Manual(const char *number, const char *message)
{
    Serial.print("Manual send to: ");
    Serial.println(number);
    
    // Set text mode
    modem.sendAT("+CMGF=1");
    if (modem.waitResponse(5000) != 1) {
        Serial.println("ERROR: Text mode failed");
        return false;
    }
    
    // Build AT+CMGS command
    char cmd[50];
    snprintf(cmd, sizeof(cmd), "+CMGS=\"%s\"", number);
    
    Serial.print("Sending: ");
    Serial.println(cmd);
    
    modem.sendAT(cmd);
    
    // Wait for '>' prompt
    if (modem.waitResponse(5000, ">") != 1) {
        Serial.println("ERROR: No '>' prompt");
        modem.stream.write((char)0x1B); // Send ESC to cancel
        modem.waitResponse();
        return false;
    }
    
    Serial.println("Got '>' prompt, sending message...");
    
    // Send message text
    modem.stream.print(message);
    modem.stream.write((char)0x1A); // CTRL+Z to send
    
    // Wait for +CMGS response (message ID)
    String response = "";
    unsigned long start = millis();
    bool gotCMGS = false;
    
    while (millis() - start < 60000) {  // 60s timeout
        if (modem.stream.available()) {
            char c = modem.stream.read();
            Serial.write(c);
            response += c;
            
            if (response.indexOf("+CMGS:") >= 0) {
                gotCMGS = true;
            }
            
            if (response.indexOf("OK") >= 0 && gotCMGS) {
                Serial.println("\n✓ SMS SENT!");
                return true;
            }
            
            if (response.indexOf("ERROR") >= 0) {
                Serial.println("\n✗ SMS ERROR");
                return false;
            }
        }
        delay(10);
    }
    
    Serial.println("\n✗ SMS TIMEOUT");
    return false;
}

//  Send SMS to all recipients
void sendsms(const char *message)
{
    Serial.println("\n=== SMS SEND ATTEMPT ===");
    
    // Check network again
    if (!modem.isNetworkConnected()) {
        Serial.println("NOT REGISTERED! Waiting for network...");
        if (!ensureNetworkConnected()) {
            Serial.println("FAILED: Cannot register to network");
            return;
        }
    }
    
    // Get signal quality
    int16_t sq = modem.getSignalQuality();
    Serial.print("Signal: ");
    Serial.println(sq);
    
    // Check SMSC before sending
    Serial.print("\nSMSC check: ");
    modem.sendAT("+CSCA?");
    modem.waitResponse(2000);
    Serial.println();
    
    // Try manual AT command SMS (more reliable)
    for (int i = 0; i < 2; i++)
    {
        Serial.print("\n========== Recipient ");
        Serial.print(i+1);
        Serial.print(": ");
        Serial.print(recipients[i]);
        Serial.println(" ==========");
        
        bool sent = sendSMS_Manual(recipients[i], message);
        
        if (sent) {
            Serial.println("✓✓✓ SMS delivered to recipient!");
        } else {
            Serial.println("✗✗✗ SMS FAILED - Possible reasons:");
            Serial.println("  1. Telcel SIM may not have international SMS enabled");
            Serial.println("  2. Texting US numbers from Mexico requires international plan");
            Serial.println("  3. SMSC may be incorrect for your region");
            Serial.println("  >> Test: Put SIM in phone and try texting these US numbers");
        }
        
        delay(3000);
    }
    
    Serial.println("\n=== SMS Complete ===");
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n=== SIM7600G-H Initialization ===");

    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
    delay(5000);  // Give SIM7600G time to wake up

    // Power sequence for SIM7600G
    pinMode(MODEM_PWRKEY, OUTPUT);
    digitalWrite(MODEM_PWRKEY, HIGH);
    delay(100);
    digitalWrite(MODEM_PWRKEY, LOW);
    delay(3000);
    digitalWrite(MODEM_PWRKEY, HIGH);

    pinMode(MODEM_FLIGHT, OUTPUT);
    digitalWrite(MODEM_FLIGHT, LOW); // Enable RF

    Serial.println("Waiting for modem responsiveness...");
    int attempts = 0;
    while (!modem.testAT() && attempts < 20) {
        Serial.print(".");
        delay(500);
        attempts++;
    }
    Serial.println();

    if (attempts >= 20) {
        Serial.println("ERROR: Modem not responding!");
        while(1) delay(1000);
    }

    Serial.println(" Modem responding");

    if (!modem.restart()) {
        Serial.println(" Modem restart may have failed");
    }
    
    delay(3000);

    Serial.println("Setting network mode to LTE only...");
    modem.setNetworkMode(38); // LTE only (better for Telcel)
    delay(1000);

    modem.sendAT("+CFUN=1");
    modem.waitResponse(1000);

    printAt("SIM status: ", "+CPIN?");
    printAt("Operator: ", "+COPS?");
    printAt("Reg (LTE): ", "+CEREG?");
    printAt("Signal: ", "+CSQ");
    
    // Configure Telcel APN
    Serial.println("\nConfiguring Telcel APN...");
    modem.sendAT("+CGDCONT=1,\"IP\",\"internet.itelcel.com\"");
    modem.waitResponse(1000);
    delay(500);
    
    // SMS Configuration - CRITICAL
    Serial.println("\n=== SMS Configuration ===");
    
    // Enable text mode
    modem.sendAT("+CMGF=1");
    modem.waitResponse(1000);
    Serial.println(" Text mode enabled");
    delay(500);
    
    // Enable error reporting
    modem.sendAT("+CMEE=2");
    modem.waitResponse(1000);
    delay(500);
    
    // Try to set charset
    modem.sendAT("+CSCS=\"GSM\"");
    modem.waitResponse(1000);
    delay(500);

    // SMS parameters (validity, DCS)
    modem.sendAT("+CSMP=17,167,0,0");
    modem.waitResponse(1000);
    delay(500);

    // New message indications
    modem.sendAT("+CNMI=2,1,0,0,0");
    modem.waitResponse(1000);
    delay(500);
    
    // Check and set SMSC for Telcel
    Serial.print("Current SMSC: ");
    modem.sendAT("+CSCA?");
    int resp = modem.waitResponse(3000);
    Serial.print("(Response code: ");
    Serial.print(resp);
    Serial.println(")");
    delay(1000);

    Serial.println("Setting SMSC to Telcel default...");
    modem.sendAT("+CSCA=\"+52733000000\"");
    modem.waitResponse(2000);
    delay(500);

    Serial.print("Verify SMSC: ");
    modem.sendAT("+CSCA?");
    modem.waitResponse(3000);
    delay(500);
    
    // Try PDU mode SMS (sometimes more reliable)
    Serial.println("\nTrying PDU mode setting...");
    modem.sendAT("+CMGF=0");
    modem.waitResponse(1000);
    delay(500);
    // Then back to text
    modem.sendAT("+CMGF=1");
    modem.waitResponse(1000);
    
    Serial.println("\nWaiting for network registration...");

    if (!ensureNetworkConnected())
    {
        Serial.println(" WARNING: Not registered! SMS will fail.");
    } else {
        Serial.println(" Network registered - SMS should work now");
    }
    
    modem.enableGPS();
    modem.sendAT("+CGPS=1,1");
    delay(1000);
    
    Serial.println("\n=== Setup Complete - Ready to Send SMS ===");
}


void loop()
{
    if (digitalRead(BUTTON_PIN) == LOW)
    {
        Serial.println("Button Pressed! Sending alert...");

        // 🔊 Buzz to alert
        digitalWrite(BUZZER_PIN, HIGH);
        delay(1000);
        digitalWrite(BUZZER_PIN, LOW);

        char message[160];

        if (waitForGPS(60))
        {
            Serial.print("Location: ");
            Serial.print(lat, 6);
            Serial.print(", ");
            Serial.println(lon, 6);

            snprintf(message, sizeof(message),
                     "MOM, I need help its Maleny! This is my location: https://www.google.com/maps?q=%f,%f",
                     lat, lon);
        }
        else
        {
            strcpy(message, "MOM, I need help! Maleny. Unable to get GPS location.");
        }

        if (!ensureNetworkConnected())
        {
            Serial.println("Cannot send SMS: modem not registered.");
            return;
        }

        sendsms(message);
        delay(5000);
    }
}
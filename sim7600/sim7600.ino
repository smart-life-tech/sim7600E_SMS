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

// Ensure the modem is actually registered before sending SMS
bool ensureNetworkConnected(uint32_t timeoutMs = 60000)
{
    Serial.println("Ensuring network registration...");
    unsigned long start = millis();
    while ((millis() - start) < timeoutMs)
    {
        int16_t sq = modem.getSignalQuality();
        Serial.print("Signal quality: ");
        Serial.println(sq);

        if (modem.isNetworkConnected())
        {
            Serial.println("Network registered.");
            return true;
        }

        // waitForNetwork blocks up to the timeout we give it
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
    
    // Try raw AT+CMGS command (more direct control)
    for (int i = 0; i < 2; i++)
    {
        Serial.print("\n[Recipient ");
        Serial.print(i+1);
        Serial.print("] ");
        Serial.println(recipients[i]);
        
        bool sent = false;
        
        for (int attempt = 1; attempt <= 3 && !sent; attempt++) {
            Serial.print("  Attempt ");
            Serial.print(attempt);
            Serial.print("/3: ");
            
            // Use TinyGSM built-in function
            sent = modem.sendSMS(recipients[i], message);
            
            if (sent) {
                Serial.println(" SUCCESS");
            } else {
                Serial.println("✗ FAILED");
                
                // Try to get error code
                Serial.print("    Getting error: ");
                modem.sendAT("+CMES?");
                int errResp = modem.waitResponse(2000);
                Serial.println(errResp);
                
                // Also check if still registered
                Serial.print("    Network status: ");
                if (modem.isNetworkConnected()) {
                    Serial.println("Registered");
                } else {
                    Serial.println("NOT REGISTERED!");
                }
                
                if (attempt < 3) {
                    Serial.println("    Waiting 8s...");
                    delay(8000);
                }
            }
        }
        
        if (!sent) {
            Serial.println("  >> Try using phone to verify SIM and SMS service");
        }
        
        delay(2000);
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

    modem.setNetworkMode(2); // Auto-select (allows fallback for SMS)
    
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
    
    // Check SMSC - don't set yet
    Serial.print("Current SMSC: ");
    modem.sendAT("+CSCA?");
    int resp = modem.waitResponse(3000);
    Serial.print("(Response code: ");
    Serial.print(resp);
    Serial.println(")");
    delay(1000);
    
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
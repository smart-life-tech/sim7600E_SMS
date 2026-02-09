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
    for (int i = 0; i < 2; i++)
    {
        Serial.print("Sending SMS to ");
        Serial.println(recipients[i]);
        
        // Check SMS service center
        Serial.print("Checking SMSC: ");
        modem.sendAT("+CSCA?");
        modem.waitResponse(2000);
        
        // Try sending with retry
        bool res = false;
        for (int attempt = 1; attempt <= 3 && !res; attempt++) {
            Serial.print("Attempt ");
            Serial.print(attempt);
            Serial.print("/3...");
            
            res = modem.sendSMS(recipients[i], message);
            
            if (res)
            {
                Serial.println(" ✓ SMS sent successfully!");
            }
            else
            {
                Serial.println(" ✗ SMS failed.");
                if (attempt < 3) {
                    Serial.println("Checking error...");
                    modem.sendAT("+CMEE=2");  // Enable verbose errors
                    modem.waitResponse(1000);
                    delay(3000);
                }
            }
        }
        delay(3000);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(10);

    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
    delay(3000);

    pinMode(MODEM_PWRKEY, OUTPUT);
    digitalWrite(MODEM_PWRKEY, HIGH);
    delay(300);
    digitalWrite(MODEM_PWRKEY, LOW);

    pinMode(MODEM_FLIGHT, OUTPUT);
    digitalWrite(MODEM_FLIGHT, HIGH);

    Serial.println("Starting modem...");
    delay(3000);
    while (!modem.testAT())
    {
        delay(10);
    }

    Serial.println("Modem initialized.");
    if (!modem.restart())
    {
        Serial.println("Modem restart failed.");
    }

    modem.setNetworkMode(2); // Auto-select (allows fallback for SMS)
    
    // Configure Telcel APN
    Serial.println("Configuring Telcel APN...");
    modem.sendAT("+CGDCONT=1,\"IP\",\"internet.itelcel.com\"");
    if (modem.waitResponse() == 1) {
        Serial.println("APN configured");
    }
    delay(1000);

    if (!ensureNetworkConnected())
    {
        Serial.println("Warning: modem not registered; SMS will fail until registration succeeds.");
    }
    
    // Enable verbose errors
    modem.sendAT("+CMEE=2");
    modem.waitResponse(1000);
    
    // SMS Configuration
    Serial.println("Configuring SMS...");
    modem.sendAT("+CMGF=1"); // Text mode
    if (modem.waitResponse() == 1) {
        Serial.println("SMS text mode enabled");
    }
    
    // Check/Set SMSC (Telcel Mexico)
    Serial.print("Current SMSC: ");
    modem.sendAT("+CSCA?");
    modem.waitResponse(2000);
    
    // If SMSC is empty, set Telcel default
    modem.sendAT("+CSCA=\"+52555000000\"");
    if (modem.waitResponse() == 1) {
        Serial.println("SMSC set to Telcel default");
    }
    
    modem.enableGPS();
    modem.sendAT("+CGPS=1,1");
    delay(1000);
    
    Serial.println("=== Setup Complete ===");
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
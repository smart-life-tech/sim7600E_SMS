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
    "+19565292282"};

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

        bool res = modem.sendSMS(recipients[i], message);
        
        if (res)
        {
            Serial.println("SMS sent successfully!");
        }
        else
        {
            Serial.println("SMS failed to send.");
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

    modem.setNetworkMode(4); // LTE only
    modem.enableGPS();
    modem.sendAT("+CGPS=1,1");
    delay(1000);

    modem.sendAT("+CMGF=1"); // Text mode
    Serial.println("SMS mode enabled");
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
                     "MOM, I need help! This is my location: https://www.google.com/maps?q=%f,%f",
                     lat, lon);
        }
        else
        {
            strcpy(message, "MOM, I need help! Maleny. Unable to get GPS location.");
        }

        sendsms(message);
        delay(5000);
    }
}

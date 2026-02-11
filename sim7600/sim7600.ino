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
    "+19563227945" // Fixed: removed extra 1 at beginning
};

void flushModem()
{
    while (modemSerial.available())
    {
        modemSerial.read();
    }
}

String sendATCommand(const char *cmd, uint32_t timeoutMs = 2000)
{
    flushModem();
    modemSerial.print(cmd);
    modemSerial.print("\r");

    unsigned long start = millis();
    String resp = "";
    while (millis() - start < timeoutMs)
    {
        while (modemSerial.available())
        {
            char c = modemSerial.read();
            resp += c;
            Serial.write(c);
        }
        delay(2);
    }
    return resp;
}

int parseCSQ(const String &resp)
{
    int idx = resp.indexOf("+CSQ:");
    if (idx < 0)
    {
        return 99;
    }
    int colon = resp.indexOf(':', idx);
    int comma = resp.indexOf(',', colon);
    if (colon < 0 || comma < 0)
    {
        return 99;
    }
    String val = resp.substring(colon + 1, comma);
    val.trim();
    return val.toInt();
}

bool isRegistered(const String &resp)
{
    return (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
}

// Ensure the modem is actually registered before sending SMS
bool ensureNetworkConnected(uint32_t timeoutMs = 60000)
{
    Serial.println("Ensuring network registration (AT only)...");
    sendATCommand("AT");
    sendATCommand("AT+CPIN?");
    sendATCommand("AT+CFUN?");
    sendATCommand("AT+COPS?");
    sendATCommand("AT+CNMP?");

    String csqResp = sendATCommand("AT+CSQ");
    int sq = parseCSQ(csqResp);
    Serial.print("Signal quality: ");
    Serial.println(sq);

    if (sq == 99 || sq == 0)
    {
        Serial.println("No signal. Forcing RF on and trying network modes...");
        sendATCommand("AT+CFUN=1");

        Serial.println("Trying LTE only (CNMP=38)...");
        sendATCommand("AT+CNMP=38");
        delay(3000);
        sq = parseCSQ(sendATCommand("AT+CSQ"));
        Serial.print("Signal quality: ");
        Serial.println(sq);

        if (sq == 99 || sq == 0)
        {
            Serial.println("Trying Auto (CNMP=2)...");
            sendATCommand("AT+CNMP=2");
            delay(3000);
            sq = parseCSQ(sendATCommand("AT+CSQ"));
            Serial.print("Signal quality: ");
            Serial.println(sq);
        }

        if (sq == 99 || sq == 0)
        {
            Serial.println("Trying GSM only (CNMP=13)...");
            sendATCommand("AT+CNMP=13");
            delay(3000);
            sq = parseCSQ(sendATCommand("AT+CSQ"));
            Serial.print("Signal quality: ");
            Serial.println(sq);
        }
    }

    sendATCommand("AT+COPS=0");

    unsigned long start = millis();
    while ((millis() - start) < timeoutMs)
    {
        String cereg = sendATCommand("AT+CEREG?");
        if (isRegistered(cereg))
        {
            Serial.println("Network registered (LTE).");
            return true;
        }

        String creg = sendATCommand("AT+CREG?");
        if (isRegistered(creg))
        {
            Serial.println("Network registered (GSM).");
            return true;
        }

        String cgreg = sendATCommand("AT+CGREG?");
        if (isRegistered(cgreg))
        {
            Serial.println("Network registered (GPRS).");
            return true;
        }

        Serial.println("Network not ready, retrying...");
        delay(3000);
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
        String resp = sendATCommand("AT+CGPSINFO", 2000);
        int idx = resp.indexOf("+CGPSINFO:");
        if (idx >= 0)
        {
            String data = resp.substring(idx + 10);
            int endLine = data.indexOf('\n');
            if (endLine >= 0)
            {
                data = data.substring(0, endLine);
            }
            data.trim();

            // Parse lat, N/S, lon, E/W
            String field[4];
            int f = 0;
            String current = "";
            for (unsigned int i = 0; i < data.length(); i++)
            {
                char c = data[i];
                if (c == ',')
                {
                    if (f < 4)
                    {
                        field[f++] = current;
                    }
                    current = "";
                }
                else
                {
                    current += c;
                }
            }
            if (f < 4)
            {
                field[f++] = current;
            }

            if (field[0].length() > 0 && field[2].length() > 0)
            {
                float latVal = field[0].toFloat();
                float lonVal = field[2].toFloat();

                int latDeg = (int)(latVal / 100);
                float latMin = latVal - (latDeg * 100);
                float latDec = latDeg + (latMin / 60.0f);

                int lonDeg = (int)(lonVal / 100);
                float lonMin = lonVal - (lonDeg * 100);
                float lonDec = lonDeg + (lonMin / 60.0f);

                if (field[1] == "S")
                {
                    latDec = -latDec;
                }
                if (field[3] == "W")
                {
                    lonDec = -lonDec;
                }

                lat = latDec;
                lon = lonDec;

                Serial.print("GPS Fix acquired after ");
                Serial.print((millis() - start) / 1000);
                Serial.println(" seconds");
                fetch = true;
                return true;
            }
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
    String resp = sendATCommand("AT+CMGF=1", 5000);
    if (resp.indexOf("OK") < 0)
    {
        Serial.println("ERROR: Text mode failed");
        return false;
    }

    sendATCommand("AT+CSCS=\"GSM\"");
    sendATCommand("AT+CSCA?");

    // Build AT+CMGS command
    char cmd[50];
    snprintf(cmd, sizeof(cmd), "+CMGS=\"%s\"", number);

    Serial.print("Sending: ");
    Serial.println(cmd);

    flushModem();
    modemSerial.print(cmd);
    modemSerial.print("\r");

    // Wait for '>' prompt
    unsigned long start = millis();
    bool gotPrompt = false;
    while (millis() - start < 5000)
    {
        if (modemSerial.available())
        {
            char c = modemSerial.read();
            Serial.write(c);
            if (c == '>')
            {
                gotPrompt = true;
                break;
            }
        }
        delay(10);
    }

    if (!gotPrompt)
    {
        Serial.println("ERROR: No '>' prompt");
        modemSerial.write((char)0x1B); // Send ESC to cancel
        sendATCommand("AT", 1000);
        return false;
    }

    Serial.println("\nGot '>' prompt, sending message...");

    // Send message text
    modemSerial.print(message);
    modemSerial.write((char)0x1A); // CTRL+Z to send

    // Wait for +CMGS response (message ID)
    String response = "";
    start = millis();
    bool gotCMGS = false;

    while (millis() - start < 60000)
    { // 60s timeout
        if (modemSerial.available())
        {
            char c = modemSerial.read();
            Serial.write(c);
            response += c;

            if (response.indexOf("+CMGS:") >= 0)
            {
                gotCMGS = true;
            }

            if (response.indexOf("OK") >= 0 && gotCMGS)
            {
                Serial.println("\nSMS SENT!");
                return true;
            }

            if (response.indexOf("ERROR") >= 0)
            {
                Serial.println("\nSMS ERROR");
                return false;
            }
        }
        delay(10);
    }

    Serial.println("\nSMS TIMEOUT");
    return false;
}

//  Send SMS to all recipients
void sendsms(const char *message)
{
    Serial.println("\n=== SMS SEND ATTEMPT ===");

    // Check network again
    if (!ensureNetworkConnected())
    {
        Serial.println("FAILED: Cannot register to network");
        return;
    }

    String csq = sendATCommand("AT+CSQ");
    int sq = parseCSQ(csq);
    Serial.print("Signal: ");
    Serial.println(sq);

    Serial.print("\nSMSC check: ");
    sendATCommand("AT+CSCA?");
    Serial.println();

    // Try manual AT command SMS (more reliable)
    for (int i = 0; i < 2; i++)
    {
        Serial.print("\n========== Recipient ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(recipients[i]);
        Serial.println(" ==========");

        bool sent = sendSMS_Manual(recipients[i], message);

        if (sent)
        {
            Serial.println(" SMS delivered to recipient!");
        }
        else
        {
            Serial.println(" SMS FAILED - Possible reasons:");
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
    delay(5000); // Give SIM7600G time to wake up

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
    while (attempts < 20)
    {
        String resp = sendATCommand("AT", 1000);
        if (resp.indexOf("OK") >= 0)
        {
            break;
        }
        Serial.print(".");
        delay(500);
        attempts++;
    }
    Serial.println();

    if (attempts >= 20)
    {
        Serial.println("ERROR: Modem not responding!");
        while (1)
            delay(1000);
    }

    Serial.println("Modem responding");

    Serial.println("Setting network mode to LTE only...");
    sendATCommand("AT+CNMP=38");
    delay(1000);

    sendATCommand("AT+CFUN=1");

    sendATCommand("AT+CPIN?");
    sendATCommand("AT+COPS?");
    sendATCommand("AT+CEREG?");
    sendATCommand("AT+CSQ");

    // Configure Telcel APN
    Serial.println("\nConfiguring Telcel APN...");
    sendATCommand("AT+CGDCONT=1,\"IP\",\"internet.itelcel.com\"");
    delay(500);

    // SMS Configuration - CRITICAL
    Serial.println("\n=== SMS Configuration ===");

    // Enable text mode
    sendATCommand("AT+CMGF=1");
    Serial.println(" Text mode enabled");
    delay(500);

    // Enable error reporting
    sendATCommand("AT+CMEE=2");
    delay(500);

    // Try to set charset
    sendATCommand("AT+CSCS=\"GSM\"");
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
    String resp = sendATCommand("AT+CSCA?", 3000);
    Serial.print("(Response length: ");
    Serial.print(resp.length());
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
    sendATCommand("AT+CMGF=0");
    delay(500);
    // Then back to text
    sendATCommand("AT+CMGF=1");

    Serial.println("\nWaiting for network registration...");

    if (!ensureNetworkConnected())
    {
        Serial.println(" WARNING: Not registered! SMS will fail.");
    }
    else
    {
        Serial.println(" Network registered - SMS should work now");
    }

    sendATCommand("AT+CGPS=1,1");
    delay(1000);

    Serial.println("\n=== Setup Complete - Ready to Send SMS ===");
}

void loop()
{
    if (digitalRead(BUTTON_PIN) == LOW)
    {
        Serial.println("v1.0 Button Pressed! Sending alert...");

        // Buzz to alert
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
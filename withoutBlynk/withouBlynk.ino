#define SerialMon Serial
#define SerialAT Serial1

#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb

#include <TinyGsmClient.h>
#include <DHT.h>

TinyGsm modem(SerialAT);

float latitude, longitude;
float sethum, settemp, t, h;
String contact1 = "1234567890"; // Update with your phone number
String contact2 = "";           // Optionally, add a second contact

#define red 12   // hum
#define blue 15  // button1 / motion
#define green 13 // temp

#define vibrationPin 22 // motion
#define button1 21      // sw

#define DHT11_PIN 0 // DHT sensor pin
#define DHTTYPE DHT11
DHT dht(DHT11_PIN, DHTTYPE);

volatile bool vibeflag = false;
volatile int vibeValue = 0;
volatile bool buttflag = false;
volatile int buttValue = 0;

bool alert1 = false;
bool alert2 = false; // humid  ,temp
bool alert3 = false;
bool alert4 = false; // motion ,button

unsigned long lastAlertTime = 0;
unsigned long delayBetweenAlerts = 0;
unsigned long currentTime = 0;
bool callFlag = true;

void setup()
{
    Serial.begin(115200);
    delay(10);

    pinMode(vibrationPin, INPUT_PULLUP);
    pinMode(button1, INPUT_PULLUP);

    pinMode(red, OUTPUT);
    digitalWrite(red, LOW);
    pinMode(blue, OUTPUT);
    digitalWrite(blue, HIGH);
    pinMode(green, OUTPUT);
    digitalWrite(green, HIGH);

    dht.begin();

    attachInterrupt(digitalPinToInterrupt(vibrationPin), vibrationChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(button1), button1Change, CHANGE);
    delay(10);

    SerialAT.begin(9600); // Start SIM800L communication
    delay(100);

    // Initialize modem
    startModem();

    // Test SMS sending
    sendSMS("System Initialized", contact1);
}

void loop()
{
    currentTime = millis();
    checkVibeButt();
    SendDhtData();
    delay(2000); // Add a delay to reduce CPU usage
}

void checkVibeButt()
{
    if (alert1 || alert2 || alert3 || alert4)
    {
        String message = "";
        if (alert1 || alert2)
        {
            message += "T=" + String(t) + "; H=" + String(h) + " \nTs=" + String(settemp) + " Hs=" + String(sethum) + " ";
        }
        if (alert3)
        {
            message += "Motion Detected! ";
        }
        if (alert4)
        {
            message += "Switch Pressed! ";
        }

        if (currentTime - lastAlertTime >= delayBetweenAlerts)
        {
            callFlag = true;
        }

        if (callFlag == true)
        {
            callFlag = false;
            lastAlertTime = currentTime;
            sendSMS(message, contact1);
        }
    }

    if (!alert1 && !alert2 && !alert3 && !alert4)
    {
        callFlag = true;
    }

    if (vibeflag)
    {
        if (vibeValue)
        {
            Serial.println("Motion detected!");
            digitalWrite(blue, LOW);
            if (alert3)
            {
                alert3 = true;
            }
        }
        vibeflag = false;
    }

    if (buttflag)
    {
        if (buttValue)
        {
            Serial.println("Button pressed!");
            digitalWrite(blue, LOW);
            if (alert4)
            {
                alert4 = true;
            }
        }
        buttflag = false;
    }
}

void vibrationChange()
{
    vibeValue = !digitalRead(vibrationPin);
    vibeflag = true;
}

void button1Change()
{
    buttValue = !digitalRead(button1);
    buttflag = true;
}

void SendDhtData()
{
    h = dht.readHumidity();
    t = dht.readTemperature(true);

    if (isnan(h) || isnan(t))
    {
        Serial.println(F("Failed to read from DHT sensor!"));
        return;
    }

    if (h > sethum && t > settemp)
    {
        digitalWrite(green, LOW);
        digitalWrite(red, LOW);
        alert1 = true;
        alert2 = true;
    }
    else if (h > sethum)
    {
        digitalWrite(green, HIGH);
        digitalWrite(red, LOW);
        alert1 = true;
        alert2 = false;
    }
    else if (t > settemp)
    {
        digitalWrite(green, LOW);
        digitalWrite(red, HIGH);
        alert1 = false;
        alert2 = true;
    }
    else
    {
        digitalWrite(green, HIGH);
        digitalWrite(red, HIGH);
        alert1 = false;
        alert2 = false;
    }
}

void startModem()
{
    SerialMon.println("Initializing modem...");
    if (!modem.restart())
    {
        Serial.println("Failed to restart modem, attempting to continue without restarting");
    }
}

void sendSMS(String message, String recipient)
{
    SerialMon.print("Sending SMS to ");
    SerialMon.println(recipient);
    modem.sendSMS(recipient.c_str(), message.c_str());
    SerialMon.println("SMS Sent!");
}

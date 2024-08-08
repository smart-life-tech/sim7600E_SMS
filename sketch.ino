/*
RA35Originalllllllllllllllllllllll
  23/02/2023

  gps fix,only when online will do call, restart added, added notification , 2 number sms and call, use init(),

  v17 - contact1
  v18 - contact2
  v7  - set temp     ,v12 - temp alert   ,v3 - cur temp
  v8  - set humid    ,v13 - hum  alert   ,v4 - cur humi
  v14 - motion alert ,mot but - v20
  v15 - sw   alert   ,sw  but - v6
  v0  - GPS
  v1  - alertdel

  // Hardware Connection:
  red 12, blue 15, green 13, vibration 22, button 21
*/

#define SerialMon Serial
#define SerialAT Serial1

#define BLYNK_TEMPLATE_ID "TMPLgHiM3u9V"
#define BLYNK_TEMPLATE_NAME "Arduino GSM"
#define BLYNK_AUTH_TOKEN "BOoctVPf2GN-Cxi9A23IIg5Yr0i1YoJL"
#define BLYNK_HEARTBEAT 60 // Default heartbeat interval for GSM is 60
#define BLYNK_PRINT Serial
char auth[] = BLYNK_AUTH_TOKEN;

// GSM
char apn[] = "";
char user[] = "";
char pass[] = "";
#define GSM_PIN ""
#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb

#include <TinyGsmClient.h>
#include <BlynkSimpleTinyGSM.h> //https://github.com/blynkkk/blynk-library
#include "utilities.h"

// See all AT commands, if wanted
// #define DUMP_AT_COMMANDS
#define TINY_GSM_DEBUG Serial
#define TINY_GSM_YIELD() \
  {                      \
    delay(2);            \
  }

#ifdef DUMP_AT_COMMANDS // if enabled it requires the streamDebugger lib
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

#include <TinyGPS++.h>
TinyGPSPlus gps;
float latitude, longitude;

BlynkTimer timer;

bool obtainLoc = false;
#define red 12   // hum
#define blue 15  // button1 / motion
#define green 13 // temp

#define vibrationPin 22 // motion
#define button1 21      // sw

#include <DHT.h>
#define DHT11_PIN 0 //
#define DHTTYPE DHT11
DHT dht(DHT11_PIN, DHTTYPE);

WidgetLED vibeled(V14);
WidgetLED buttled(V15);
WidgetLED templed(V12);
WidgetLED humled(V13);

int alertDel = 0;
float sethum, settemp, t, h;
String contact1 = "";
String contact2 = "";

volatile bool vibeflag = false;
volatile int vibeValue = 0;
volatile bool buttflag = false;
volatile int buttValue = 0;

int motbut, swbut;

unsigned long lastAlertTime = 0;
unsigned long delayBetweenAlerts = 0;
unsigned long currentTime = 0;
bool alert1 = false;
bool alert2 = false; // humid  ,temp
bool alert3 = false;
bool alert4 = false; // motion ,button

bool callFlag = true;

bool reply = false;

bool containsOnlyNumbers(String str)
{
  for (size_t i = 0; i < str.length(); i++)
  {
    if (!isDigit(str[i]))
    {
      return false;
    }
  }
  return true;
}

// sync new values
BLYNK_WRITE(V17)
{
  contact1 = param.asStr();
  if (containsOnlyNumbers(contact1))
  {
    Serial.println("Contact1 contains only numbers.");
  }
  else
  {
    contact1 = "";
    Blynk.virtualWrite(V17, "");
  }
}
BLYNK_WRITE(V18)
{
  contact2 = param.asStr();
  if (containsOnlyNumbers(contact2))
  {
    Serial.println("Contact2 contains only numbers.");
  }
  else
  {
    contact2 = "";
    Blynk.virtualWrite(V18, "");
  }
}
BLYNK_WRITE(V7)
{
  settemp = param.asInt();
}
BLYNK_WRITE(V8)
{
  sethum = param.asInt();
}
BLYNK_WRITE(V1)
{
  alertDel = param.asInt();
  // Calculate the delay in milliseconds based on the alertDel value in minutes
  delayBetweenAlerts = alertDel * 60 * 1000;
}
BLYNK_WRITE(V20)
{
  motbut = param.asInt();
  if (motbut == 0)
  {
    alert3 = false;
  }
}
BLYNK_WRITE(V6)
{
  swbut = param.asInt();
  if (swbut == 0)
  {
    alert4 = false;
  }
}

BLYNK_CONNECTED()
{
  digitalWrite(LED_PIN, HIGH);

  // sync all data at start
  // Blynk.syncAll();
  Blynk.syncVirtual(V17);
  Blynk.syncVirtual(V18);
  Blynk.syncVirtual(V7);
  Blynk.syncVirtual(V8);
  Blynk.syncVirtual(V1);
  Blynk.syncVirtual(V20);
  Blynk.syncVirtual(V6);
  // make all alert off
  Blynk.virtualWrite(V14, 0);
  Blynk.virtualWrite(V15, 0);
  Blynk.virtualWrite(V13, 0);
  Blynk.virtualWrite(V12, 0);
}

BLYNK_DISCONNECTED()
{
  digitalWrite(LED_PIN, LOW);

  modem.init();
  // modem.waitResponse(30000);
}

void setup()
{
  // put your setup code here, to run once:
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

  // SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX); // Start AT Mode
  delay(100);

  startModem();

  Blynk.begin(auth, modem, apn, user, pass); // Initialize Blynk

  blinkLED(red);
  blinkLED(red);
  blinkLED(red);

  modem.enableGPS();
  // Download Report GPS NMEA-0183 sentence , NMEA TO AT PORT
  modem.sendAT("+CGPSINFOCFG=1,31");
  modem.waitResponse(30000);

  timer.setInterval(10000L, SendDhtData);
  timer.setInterval(2000L, checkVibeButt);
}

void loop()
{
  // put your main code here, to run repeatedly:
  Blynk.run();
  timer.run();
  sendGPS();
}

void checkVibeButt()
{
  currentTime = millis();
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
    if (obtainLoc)
    {
      message += "https://www.google.com/maps?q=" + String(latitude) + "," + String(longitude);
    }
    else
    {
      message += "GPS Location Not Found Yet!";
    }

    // Check if enough time has elapsed since the last alert
    if (currentTime - lastAlertTime >= delayBetweenAlerts)
    {
      callFlag = true;
    }
    if (callFlag == true)
    {
      if (Blynk.connected())
      {
        callFlag = false;
        lastAlertTime = currentTime; // Update the last alert time to the current time
      }
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
      vibeled.on();
      Blynk.syncVirtual(V20);
      if (motbut == 1)
      {
        alert3 = true;
      }
    }
    else
    {
      vibeled.off();
      digitalWrite(blue, HIGH);
    }
    vibeflag = false;
  }

  if (buttflag)
  {
    if (buttValue)
    {
      Serial.println("swpress detected!");
      digitalWrite(blue, LOW);
      buttled.on();
      Blynk.syncVirtual(V6);
      if (swbut == 1)
      {
        alert4 = true;
      }
    }
    else
    {
      buttled.off();
      digitalWrite(blue, HIGH);
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
  t = dht.readTemperature(true);                 // Read temperature as Celsius (the default)
  float hic = dht.computeHeatIndex(t, h, false); // Compute heat index in Celsius (isFahreheit = false)
  //-----------------------------------------------------------------------
  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t))
  {
    Serial.println(F("Failed to read from DHT sensor!"));
    Blynk.virtualWrite(V3, 0);
    Blynk.virtualWrite(V4, 0);
    return;
  }
  // Serial.print("Temperature: ");  Serial.print(t);
  // Serial.print("   Humidity: ");  Serial.print(h);
  // Serial.print("   heatIndex: "); Serial.println(hic);
  // Serial.print("Set temp");       Serial.print(settemp);
  // Serial.print("   Set hum: ");   Serial.println(sethum);
  // Serial.println("------------------------------------------");

  if (h > sethum && t > settemp)
  {
    humled.on();
    templed.on();
    digitalWrite(green, LOW);
    digitalWrite(red, LOW);
    alert1 = true;
    alert2 = true;
  }
  else if (h > sethum)
  {
    humled.on();
    templed.off();
    digitalWrite(green, HIGH);
    digitalWrite(red, LOW);
    alert1 = true;
    alert2 = false;
  }
  else if (t > settemp)
  {
    humled.off();
    templed.on();
    digitalWrite(green, LOW);
    digitalWrite(red, HIGH);
    alert1 = false;
    alert2 = true;
  }
  else
  {
    humled.off();
    templed.off();
    digitalWrite(green, HIGH);
    digitalWrite(red, HIGH);
    alert1 = false;
    alert2 = false;
  }
  Blynk.virtualWrite(V3, t);
  Blynk.virtualWrite(V4, h);
}

void sendGPS()
{
  while (SerialAT.available() > 0)
  {
    gps.encode(SerialAT.read());
    if (gps.location.isValid())
    {
      latitude = (gps.location.lat());
      longitude = (gps.location.lng());
      // Serial.print(F("Latitude:"));     Serial.print(latitude, 6);      Serial.print(F("   Longitude:"));  Serial.println(longitude, 6);
      Blynk.virtualWrite(V0, gps.location.lng(), gps.location.lat());
      obtainLoc = true;
    }
  }
}

void startModem()
{
  /*
    MODEM_PWRKEY IO:4 The power-on signal of the modulator must be given to it,
    otherwise the modulator will not reply when the command is sent
  */
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(300); // Need delay
  digitalWrite(MODEM_PWRKEY, LOW);
  /*
    MODEM_FLIGHT IO:25 Modulator flight mode control,
    need to enable modulator, this pin must be set to high
  */
  pinMode(MODEM_FLIGHT, OUTPUT);
  digitalWrite(MODEM_FLIGHT, HIGH);
  // To skip it, call init() instead of restart()
  SerialMon.println("Initializing modem...");
  if (!modem.init())
  {
    Serial.println("Failed to restart modem, attempting to continue without restarting");
  }
}

void blinkLED(int pin)
{
  digitalWrite(pin, LOW);
  delay(1500);
  digitalWrite(pin, HIGH);
  delay(1500);
}
#include <Arduino.h>
#include <ESP8266WiFi.h>

#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "RTClib.h"
#include <ErriezLKM1638Board.h>

#include "ConfigManager.h"

//GMT
const auto DEFAULT_GMT_OFFSET = 28800; //GMT +8 in seconds
const auto GMT_OFFSET_LENGTH = 6;
int32_t gmtOffset = DEFAULT_GMT_OFFSET;

//Config manager
ConfigManager configManager;

//Wifi
WiFiManager wifiManager;

// id/name, placeholder/prompt, default, length
WiFiManagerParameter paramterGMTOffset("gmtOffset", "GMT Offset (seconds)", String(gmtOffset).c_str(), GMT_OFFSET_LENGTH);

//NTP client
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP, gmtOffset);

//RTC
RTC_DS3231 rtc;
#define RTC_VCC_PIN     D3
#define RTC_GND_PIN     D4

//LKM1638
#define TM1638_CLK_PIN   D5
#define TM1638_DIO_PIN   D6
#define TM1638_STB0_PIN  D7
#define TM1638_BRIGHTNESS_MIN 0
#define TM1638_BRIGHTNESS_MAX 7

// Create LKM1638 board
static constexpr auto DOTS_ON = 0b1001000;
static uint8_t DOTS_OFF = 0;
LKM1638Board lkm1638(TM1638_CLK_PIN, TM1638_DIO_PIN, TM1638_STB0_PIN);
LedColor colors[NUM_COLOR_LEDS];
uint8_t currentLED = 0;
uint8_t brightness = 4;

//LDR
#define LDR_PIN A0
uint16_t ldrMin = 0;
uint16_t ldrMax = 0;

//Success booleans
bool wiFiSuccess = false;
bool ntpSuccess = false;
bool rtcSuccess = false;

void displayTime(const DateTime& now)
{
    lkm1638.setPrintPos(6);
    lkm1638.print(now.hour(), DEC, 2, 2);
    lkm1638.setPrintPos(3);
    lkm1638.print(now.minute(), DEC, 2, 2);
    lkm1638.setPrintPos(0);
    lkm1638.print(now.second(), DEC, 2, 2);

    //Dots blinking effect
    lkm1638.setDots(now.second() & 1 ? DOTS_ON : DOTS_OFF);

    //Flip current LED color
    colors[currentLED] = colors[currentLED] == LedRed ? LedGreen : LedRed;
    lkm1638.setColorLED(currentLED, colors[currentLED]);

    //Increment current LED counter for waterfall effect
    currentLED++;
    currentLED %= NUM_COLOR_LEDS;
}

void displayTimeSerial(const DateTime& now)
{
    static const char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

    Serial.printf("%02d", now.day());
    Serial.print('/');
    Serial.printf("%02d", now.month());
    Serial.print('/');
    Serial.printf("%02d", now.year());

    Serial.print(" (");
    Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
    Serial.print(") ");

    Serial.printf("%02d", now.hour());
    Serial.print(':');
    Serial.printf("%02d", now.minute());
    Serial.print(':');
    Serial.printf("%02d", now.second());
    Serial.println();
}

void setupDisplay()
{
    //Initialize LKM1638Board
    lkm1638.begin();
    lkm1638.clear();

    lkm1638.setBrightness(brightness);
    for (size_t i = 0; i < NUM_COLOR_LEDS; i++)
    {
        colors[i] = LedRed;
        lkm1638.setColorLED(i, colors[i]);
    }
}

void setupArduinoOTA()
{
    ArduinoOTA.onStart([]()
    {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
        }
        else
        { // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Serial.println("[ArduinoOTA] Start updating " + type);
    });

    ArduinoOTA.onEnd([]()
    {
        Serial.println("[ArduinoOTA] End");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        Serial.printf("[ArduinoOTA] Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error)
    {
        Serial.printf("[ArduinoOTA] Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
        {
            Serial.println("[ArduinoOTA] Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            Serial.println("[ArduinoOTA] Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            Serial.println("[ArduinoOTA] Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            Serial.println("[ArduinoOTA] Receive Failed");
        }
        else if (error == OTA_END_ERROR)
        {
            Serial.println("[ArduinoOTA] End Failed");
        }
    });

    ArduinoOTA.begin();
}

void updateNTPtoRTC()
{
    ntpClient.update();
    rtc.adjust(DateTime(ntpClient.getEpochTime()));
    Serial.println("[RTC] Time update from NTP successful");
}

void wifiManagerSaveParamsCallback()
{
    //Set global value
    gmtOffset = String(paramterGMTOffset.getValue()).toInt();
    configManager.setValue("gmtOffset", gmtOffset);

    //Update ntpClient and RTC
    ntpClient.setTimeOffset(gmtOffset);
    updateNTPtoRTC();
}

bool setupWiFiManager()
{
    WiFi.mode(WIFI_STA);

    //wifiManager.resetSettings();

    //WiFiManager settings
    wifiManager.setParamsPage(true);
    wifiManager.setConfigPortalBlocking(false);

    //Get GMT offset from config
    gmtOffset = configManager.getValue("gmtOffset", DEFAULT_GMT_OFFSET);

    //Set to parameter
    paramterGMTOffset.setValue(String(gmtOffset).c_str(), GMT_OFFSET_LENGTH);
    wifiManager.setSaveParamsCallback(wifiManagerSaveParamsCallback);

    //Params
    wifiManager.addParameter(&paramterGMTOffset);

    //Connect
    return wifiManager.autoConnect();
}

bool setupNTPClient()
{
    Serial.println("[NTP] Connecting...");

    //Setup ntp client
    ntpClient.setTimeOffset(gmtOffset);
    ntpClient.begin();
    bool success = ntpClient.update();

    if (success)
    {
        Serial.println("[NTP] Connection successful");
        Serial.print("[NTP] Time: ");
        Serial.println(ntpClient.getFormattedTime());
    }
    else
    {
        Serial.println("[NTP] Connection unsuccessful");
    }

    return success;
}

bool setupRTC()
{
    if (rtc.begin())
    {
        Serial.println("[RTC] Connection successful");

        if (rtc.lostPower())
            Serial.println("[RTC] Lost power");

        return true;
    }
    else
    {
        Serial.println("[RTC] Connection unsuccessful");
        return false;
    }
}

void setupLDR()
{
    ldrMin = ldrMax = analogRead(LDR_PIN);
}

void setup()
{
    Serial.begin(115200);
    delay(500); //Let serial monitor connect

    setupDisplay();

    wiFiSuccess = setupWiFiManager();
    ntpSuccess = false;

    if (wiFiSuccess)
        ntpSuccess = setupNTPClient();

    rtcSuccess = setupRTC();

    if (wiFiSuccess && ntpSuccess && rtcSuccess)
        updateNTPtoRTC();

    //RTC based clock
    if (rtcSuccess)
    {
        if (ntpSuccess)
        {
            ntpClient.end(); //Cleanup time client
            ntpUDP.stopAll();
        }

        // if (wiFiSuccess)
        //     WiFi.disconnect();
    }
    //else implies NTP clock

    setupLDR();

    //Start web portal
    wifiManager.startWebPortal();

    //Start OTA
    setupArduinoOTA();
}

void setBrightness()
{
    const auto currentLDR = analogRead(LDR_PIN);

    if (currentLDR < ldrMin)
        ldrMin = currentLDR;

    if (currentLDR > ldrMax)
        ldrMax = currentLDR;

    //https://makeabilitylab.github.io/physcomp/advancedio/smoothing-input.html
    static const float EWMA_ALPHA = 0.1; // the EWMA alpha value (α)
    static double ewmaLDR = currentLDR; // the EWMA result (Si), initialized to first brightness value

    ewmaLDR = (EWMA_ALPHA * currentLDR) + (1 - EWMA_ALPHA) * ewmaLDR; // Apply the EWMA formula

    //Fallback for LDR not attached
    if (currentLDR == 0 && ldrMin == 0 && ldrMax == 0)
        brightness = 4;
    else
        brightness = map(ewmaLDR, ldrMin, ldrMax, TM1638_BRIGHTNESS_MIN, TM1638_BRIGHTNESS_MAX);

    lkm1638.setBrightness(brightness);
}

void runEverySecond()
{
    static int8_t currentRunSecond = -1;

    const auto currentSecond = ntpClient.getSeconds();

    if (currentSecond == currentRunSecond)
        return;

    //Code to run every second
    DateTime now;

    if (rtcSuccess)
        now = rtc.now();
    else
        now = ntpClient.getEpochTime();

    setBrightness();

    displayTime(now);
    //displayTimeSerial(now);

    currentRunSecond = currentSecond;
}

void loop()
{
    wifiManager.process();
    ArduinoOTA.handle();

    runEverySecond();
}
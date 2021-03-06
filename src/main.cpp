#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <Wire.h>

#include <ArduinoJson.h>
#include <BH1750.h>
#include <ESPAsyncWebServer.h>

const int RELAY_PIN = D0;
const int JSON_CAPACITY = 64;
const unsigned long MEASURE_INTERVAL = 10000;
const char SETTINGS_PATH[] = "/settings.json";

unsigned long lastMeasurement = 0;
float threshold = 50, hysteresis = 10;

float lux = 0;

AsyncWebServer server(80);
AsyncEventSource events("/events");
BH1750 lightMeter;

void printSuccess(bool success)
{
    Serial.println(success ? F("✔️") : F("✖️"));
}

void setup()
{
    bool success;

    pinMode(RELAY_PIN, OUTPUT);

    Serial.println(F("Setting up Serial"));
    Serial.begin(74880);

    Serial.println(F("Setting up I²C"));
    Wire.begin();

    Serial.print(F("Setting up LittleFS..."));
    success = LittleFS.begin();
    printSuccess(success);

    if (LittleFS.exists(SETTINGS_PATH)) {
        Serial.print(F("Getting initial values of threshold and hysteresis..."));
        File f = LittleFS.open(SETTINGS_PATH, "r");
        if (!f) {
            printSuccess(false);
            Serial.println(F("failed to open settings file"));
        } else {
            StaticJsonDocument<JSON_CAPACITY> settings;
            DeserializationError err = deserializeJson(settings, f);
            if (err) {
                printSuccess(false);
                Serial.print(F("deserializeJSON() failed with code "));
                Serial.println(err.f_str());
            } else {
                threshold = settings[F("threshold")];
                hysteresis = settings[F("hysteresis")];
                printSuccess(true);
            }
        }
    }

    Serial.print(F("Setting up light sensor..."));
    success = lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE);
    printSuccess(success);

    Serial.print(F("Setting up WiFi soft-AP..."));
    success = WiFi.softAP("pg72_backlight", NULL, 1, true);
    printSuccess(success);

    Serial.println(F("Setting up web server"));
    DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), F("*"));
    events.onConnect(
        [](AsyncEventSourceClient* client) { client->send(String(lux, 0).c_str()); });
    server.addHandler(&events);
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
        AsyncResponseStream* resp = req->beginResponseStream(F("application/json"));
        StaticJsonDocument<JSON_CAPACITY> settings;
        settings[F("threshold")] = threshold;
        settings[F("hysteresis")] = hysteresis;
        serializeJson(settings, *resp);
        req->send(resp);
    });
    server.on("/settings", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* resp = req->beginResponse(200);
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
        resp->addHeader("Access-Control-Allow-Methods", "PUT");
        req->send(resp);
    });
    server.onRequestBody([](AsyncWebServerRequest* req,
                            uint8_t* data,
                            size_t len,
                            size_t index,
                            size_t total) {
        if (req->url() == "/settings" && req->method() == HTTP_PUT) {
            StaticJsonDocument<JSON_CAPACITY> settings;
            DeserializationError err = deserializeJson(settings, data);
            if (err) {
                AsyncResponseStream* resp = req->beginResponseStream("text/plain");
                resp->setCode(400);
                resp->print(F("deserializeJSON() failed with code "));
                resp->println(err.f_str());
                req->send(resp);
                return;
            } else {
                threshold = settings[F("threshold")];
                hysteresis = settings[F("hysteresis")];
                File f = LittleFS.open(SETTINGS_PATH, "w");
                if (!f) {
                    AsyncResponseStream* resp = req->beginResponseStream("text/plain");
                    resp->setCode(500);
                    resp->println("failed to open settings file");
                    req->send(resp);
                    return;
                } else {
                    serializeJson(settings, f);
                    req->send(204);
                }
            }
        }
    });
    server.onNotFound([](AsyncWebServerRequest* req) { req->send(404); });
    server.begin();
}

void loop()
{
    unsigned long currentMillis = millis();

    if ((currentMillis - lastMeasurement >= MEASURE_INTERVAL)
        && lightMeter.measurementReady(true)) {
        lux = lightMeter.readLightLevel();
        events.send(String(lux, 0).c_str());
        if (lux < threshold) {
            digitalWrite(RELAY_PIN, HIGH);
        } else if (lux > (threshold + hysteresis)) {
            digitalWrite(RELAY_PIN, LOW);
        }
        lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE);
        lastMeasurement = currentMillis;
    }
}

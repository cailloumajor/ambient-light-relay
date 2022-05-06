#include <Arduino.h>
#include <Wire.h>

#include <BH1750.h>

const int relayPin = D0;

BH1750 lightMeter;

void setup()
{
    pinMode(relayPin, OUTPUT);

    Serial.begin(9600);

    Wire.begin(D2, D1);

    lightMeter.begin();

    Serial.println(F("BH1750 test begin"));
}

void loop()
{
    float lux = lightMeter.readLightLevel();
    Serial.print(F("Light: "));
    Serial.print(lux);
    Serial.println(F(" lx"));
    digitalWrite(relayPin, lux < 20 ? HIGH : LOW);
    delay(1000);
}

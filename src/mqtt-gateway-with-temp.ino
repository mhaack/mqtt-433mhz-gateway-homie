#include <Adafruit_BMP085_U.h>
#include <Adafruit_Sensor.h>
#include <Homie.h>
#include <RCSwitch.h>
#include <Wire.h>

// temp sensor
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);
unsigned long lastTemperatureSent = 0;
const int DEFAULT_TEMPERATURE_INTERVAL = 10;
const double DEFAULT_TEMPERATURE_OFFSET = 0;

// homie nodes & settings
HomieNode temperatureNode("temperature", "temperature");
HomieNode rfSwitchNode("MQTTto433", "switch");
HomieNode rfReceiverNode("433toMQTT", "switch");
HomieSetting<long> temperatureIntervalSetting("temperatureInterval", "The temperature interval in seconds");
HomieSetting<double> temperatureOffsetSetting("temperatureOffset", "The temperature offset in degrees");
HomieSetting<const char *> channelMappingSetting("channels", "Mapping of 433MHz signals to mqtt channel.");

// RF Switch
RCSwitch mySwitch = RCSwitch();

void setupHandler() { temperatureNode.setProperty("unit").send("c"); }

void loopHandler() {
    if (millis() - lastTemperatureSent >= temperatureIntervalSetting.get() * 1000UL || lastTemperatureSent == 0) {
        float temperature = 0;
        sensors_event_t event;
        bmp.getEvent(&event);
        if (event.pressure) {
            Serial << "Pressure: " << event.pressure << " hPa" << endl;
            bmp.getTemperature(&temperature);
            Serial << "Temperature: " << temperature << " °C" << endl;
            temperature += temperatureOffsetSetting.get();
            Serial << "Temperature (after offset): " << temperature << " °C" << endl;
        } else {
            Serial << "Sensor error" << endl;
        }
        temperatureNode.setProperty("degrees").send(String(temperature));
        lastTemperatureSent = millis();
    }

    if (mySwitch.available()) {
        long data = mySwitch.getReceivedValue();
        mySwitch.resetAvailable();
        Serial << "Receiving 433Mhz > MQTT signal: " << data << endl;

        String currentCode = String(data);
        String channelId = getChannelByCode(currentCode);
        Serial << "Code: " << currentCode << " matched to channel " << channelId << endl;
        rfReceiverNode.setProperty("channel-" + channelId).send(currentCode);
    }
}

String getChannelByCode(const String &currentCode) {
    String mappingConfig = channelMappingSetting.get();
    String mapping = "";
    String codes = "";
    int lastIndex = 0;
    int lastCodeIndex = 0;

    for (int i = 0; i < mappingConfig.length(); i++) {
        if (mappingConfig.substring(i, i + 1) == ";") {
            mapping = mappingConfig.substring(lastIndex, i);
            // Serial << "mapping: " << mapping << endl;

            codes = mapping.substring(mapping.indexOf(':') + 2, mapping.length() - 1);
            for (int j = 0; j < codes.length(); j++) {
                if (codes.substring(j, j + 1) == ",") {
                    if (currentCode.indexOf(codes.substring(lastCodeIndex, j)) > -1) {
                        return mapping.substring(0, mapping.indexOf(':'));
                        ;
                    }
                    codes = codes.substring(j + 1, codes.length());
                }
            }
            if (currentCode.indexOf(codes) > -1) {
                return mapping.substring(0, mapping.indexOf(':'));
                ;
            }
            lastIndex = i + 1;
        }
    }
    return "0";
}

bool rfSwitchOnHandler(const HomieRange &range, const String &value) {
    long int data = 0;
    int pulseLength = 350;
    if (value.indexOf(',') > 0) {
        pulseLength = atoi(value.substring(0, value.indexOf(',')).c_str());
        data = atoi(value.substring(value.indexOf(',') + 1).c_str());
    } else {
        data = atoi(value.c_str());
    }
    Serial << "Receiving MQTT > 433Mhz signal: " << pulseLength << ":" << data << endl;

    mySwitch.setPulseLength(pulseLength);
    mySwitch.send(data, 24);
    rfSwitchNode.setProperty("on").send(String(data));
    return true;
}

void setup() {
    Serial.begin(115200);
    Serial << endl << endl;

    // init BMP sensor
    if (!bmp.begin()) {
        Serial << "Ooops, no BMP085 detected ... Check your wiring or I2C ADDR!" << endl;
        while (1)
            ;
    }

    // init RF library
    mySwitch.enableTransmit(D0);    // RF Transmitter is connected to Pin D2
    mySwitch.setRepeatTransmit(20); // increase transmit repeat to avoid lost of rf sendings
    mySwitch.enableReceive(D5);     // Receiver on pin D3

    // init Homie
    Homie_setFirmware("mqtt-gateway-livingroom", "1.0.0");
    Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);
    Homie.disableResetTrigger();

    rfSwitchNode.advertise("on").settable(rfSwitchOnHandler);
    rfReceiverNode.advertise("channel-0");

    temperatureNode.advertise("unit");
    temperatureNode.advertise("degrees");
    temperatureIntervalSetting.setDefaultValue(DEFAULT_TEMPERATURE_INTERVAL);
    temperatureOffsetSetting.setDefaultValue(DEFAULT_TEMPERATURE_OFFSET);

    Homie.setup();
}

void loop() { Homie.loop(); }
#include <Arduino.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "BTSPPServer.h"
#include <ConfigItem.h>
#include <EEPROMConfig.h>

#define SPP_SERVER_TAG "SPP_SERVER"

const char *manifest[]{
    // Firmware name
    "Bluetooth SPP Server",
    // Firmware version
    "0.1.0",
    // Hardware chip/variant
    "esp32-pico-devkitm-2",
    // Device name
    "Bluetooth SPP Server"
};

struct LongConfigItem : public ConfigItem<uint64_t> {
	LongConfigItem(const char *name, const uint64_t value)
	: ConfigItem(name, sizeof(uint64_t), value)
	{}

	virtual void fromString(const String &s) { value = strtoull(s.c_str(), nullptr, 16); }
	virtual String toJSON(bool bare = false, const char **excludes = 0) const { return String(value, 16); }
	virtual String toString(const char **excludes = 0) const { return String(value, 16); }
	LongConfigItem& operator=(const uint64_t val) { value = val; return *this; }
};
template <class T>
void ConfigItem<T>::debug(Print *debugPrint) const {
	if (debugPrint != 0) {
		debugPrint->print(name);
		debugPrint->print(":");
		debugPrint->print(value);
		debugPrint->print(" (");
		debugPrint->print(maxSize);
		debugPrint->println(")");
	}
}
template void ConfigItem<uint64_t>::debug(Print *debugPrint) const;

#define RXD 7
#define TXD 8
#define COMMAND_PIN 15
#define CONNECTED_PIN 13

#define MAX_NAME_LEN 63

StringConfigItem serverName("server_name", MAX_NAME_LEN, "timefliesbridge");
StringConfigItem clientName("client_name", MAX_NAME_LEN, "Time Flies");
LongConfigItem clientAddress("address", 0);

BTSPPServer btSPPServer(serverName.toString().c_str(), Serial1);

TaskHandle_t commitEEPROMTask;
TaskHandle_t sppTask;

QueueHandle_t sppQueue;

// Global configuration
BaseConfigItem* configSetGlobal[] = {
	&serverName,
	&clientName,
    &clientAddress,
	0
};

CompositeConfigItem globalConfig("global", 0, configSetGlobal);

BaseConfigItem* rootConfigSet[] = {
    &globalConfig,
    0
};

CompositeConfigItem rootConfig("root", 0, rootConfigSet);

EEPROMConfig config(rootConfig);

void sppTaskFn(void *pArg) {
	while(true) {
		btSPPServer.loop();
	}
}

void initFromEEPROM() {
//	config.setDebugPrint(debugPrint);
	config.init();
//	rootConfig.debug(debugPrint);
	ESP_LOGD(SPP_SERVER_TAG, "serverName: %s", serverName.value.c_str());
	rootConfig.get();	// Read all of the config values from EEPROM
	ESP_LOGD(SPP_SERVER_TAG, "serverName: %s", serverName.value.c_str());
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);

	EEPROM.begin(2048);
	initFromEEPROM();

	btSPPServer.setClientAddressCallback([](long address) { clientAddress = address; clientAddress.put(); config.commit(); });
	btSPPServer.setServerNameCallback([](const char *name) { serverName = name; serverName.put(); config.commit(); });
	btSPPServer.setClientNameCallback([](const char *name) { clientName = name; clientName.put(); config.commit(); });
	
	btSPPServer.setClientAddress(clientAddress);
	btSPPServer.setServerName(serverName.value.c_str());
	btSPPServer.setClientName(clientName.value.c_str());

	btSPPServer.setCommandPin(COMMAND_PIN);
	btSPPServer.setConnectedPin(CONNECTED_PIN);
	
	btSPPServer.start(38400, SERIAL_8N1, RXD, TXD);

    xTaskCreatePinnedToCore(
        sppTaskFn,   /* Function to implement the task */
        "BT SPP task", /* Name of the task */
        8192,                 /* Stack size in words */
        NULL,                 /* Task input parameter */
        tskIDLE_PRIORITY + 1,     /* More than background tasks */
        &sppTask,    /* Task handle. */
        xPortGetCoreID());

    ESP_LOGD(SPP_SERVER_TAG, "setup() running on core %d", xPortGetCoreID());

	vTaskDelete(NULL);
}

void loop() {
}

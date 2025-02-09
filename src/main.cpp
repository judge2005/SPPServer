#include <Arduino.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "BTSPP.h"
#include "BTGAP.h"
#include "CommandHandler.h"
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

typedef enum {
	NOT_INITIALIZED = 0,
    NOT_CONNECTED,
    SEARCHING,
    CONNECTING,
    CONNECTED,
	DISCONNECTING
} SPPConnectionState;

std::unordered_map<SPPConnectionState, std::string> state2string = {
	{NOT_INITIALIZED, "NOT_INITIALIZED"},
    {NOT_CONNECTED, "NOT_CONNECTED"},
	{SEARCHING, "SEARCHING"},
	{CONNECTED, "CONNECTED"},
	{SEARCHING, "SEARCHING"},
	{DISCONNECTING, "DISCONNECTING"}
};

#define RXD 7
#define TXD 8
#define COMMAND_PIN 15
#define CONNECTED_PIN 13

#define MAX_NAME_LEN 63

StringConfigItem serverName("server_name", MAX_NAME_LEN, "timefliesbridge");
StringConfigItem clientName("client_name", MAX_NAME_LEN, "Time Flies");
LongConfigItem clientAddress("address", 0);

CommandHandler commandHandler(Serial1);

BTSPP btSPP(serverName.toString().c_str());
BTGAP btGAP;
bool canConnect = false;

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

SPPConnectionState connectionStatus = NOT_INITIALIZED;

void initSPP() {
	if (!btSPP.inited()) {
		if (btSPP.init()) {
			if (btGAP.init()) {
				connectionStatus = NOT_CONNECTED;
			} else {
				ESP_LOGE(SPP_SERVER_TAG, "GAP initialization failed: %s", btGAP.getErrMessage().c_str());
			}
		} else {
			ESP_LOGE(SPP_SERVER_TAG, "BT initialization failed: %s", btSPP.getErrMessage().c_str());
		}
	}
}

void initiateConnection() {
	if (clientAddress != 0ULL) {
		ESP_LOGI(SPP_SERVER_TAG, "Connecting to clock");
		connectionStatus = CONNECTING;
		uint64_t uAddress = clientAddress;
		esp_bd_addr_t address = {0};
		address[0] = uAddress & 0xff;
		address[1] = (uAddress >> 8) & 0xff;
		address[2] = (uAddress >> 16) & 0xff;
		address[3] = (uAddress >> 24) & 0xff;
		address[4] = (uAddress >> 32) & 0xff;
		address[5] = (uAddress >> 40) & 0xff;
		
		btSPP.startConnection(address);
	} else {
		connectionStatus = SEARCHING;
		ESP_LOGI(SPP_SERVER_TAG, "Searching for clock");
		if (!btGAP.startInquiry()) {
			ESP_LOGE(SPP_SERVER_TAG, "Error starting inqury: %s", btGAP.getErrMessage());
			connectionStatus = NOT_CONNECTED;
		}
	}
}

void sppTaskFn(void *pArg) {
    static char msg[200];

	uint32_t delayNextMsg = 1;
	uint32_t maxWait = 500;
	delay(10000);
	uint32_t lastConnectedTime = millis();
	while(true) {
		if (digitalRead(COMMAND_PIN) == HIGH) {
			commandHandler.setMode(CommandHandler::COMMAND);
		} else {
			commandHandler.setMode(CommandHandler::PASSTHROUGH);
		}

		commandHandler.loop();

		if (connectionStatus == CONNECTED) {
			// Toggle pin. Sending a message is a bad idea because of asynchronicity
			digitalWrite(CONNECTED_PIN, HIGH);
			canConnect = false;

#ifdef DISCONNECT_BT_ON_IDLE
			if (millis() - lastConnectedTime > 30000) {
				ESP_LOGI(SPP_SERVER_TAG, "Disconnecting");
				btSPP.endConnection();
				connectionStatus = DISCONNECTING;
			}
#endif
		} else {
			digitalWrite(CONNECTED_PIN, LOW);
		}

		if (connectionStatus == NOT_INITIALIZED) {
			initSPP();	// Will move to NOT_CONNECTED if it succeeds
		}

		if ((connectionStatus == NOT_CONNECTED) && canConnect) {
			initiateConnection();	// Will move to CONNECTING or SEARCHING
		}

		if (connectionStatus == SEARCHING) {
			if (btGAP.inquiryDone()) {
                uint8_t *address = btGAP.getAddress(clientName.value.c_str());
                if (address) {
                    clientAddress =  ((uint64_t)address[0]) |
                                ((uint64_t)address[1]) << 8 |
                                ((uint64_t)address[2]) << 16 |
                                ((uint64_t)address[3]) << 24 |
                                ((uint64_t)address[4]) << 32 |
                                ((uint64_t)address[5]) << 40
                                ;
                    clientAddress.put();
                    config.commit();
				}
                connectionStatus = NOT_CONNECTED;	// Try connecting or searching again
            }
        }

		if (connectionStatus == CONNECTING) {
			// Connecting might fail - re-initiate the connection attempt
			if (btSPP.isError()) {
				ESP_LOGI(SPP_SERVER_TAG, "%s", btSPP.getErrMessage().c_str());
				connectionStatus == NOT_CONNECTED;
			} else if (btSPP.connectionDone()) {
				ESP_LOGI(SPP_SERVER_TAG, "Connected");
				connectionStatus = CONNECTED;
			}
		}

		if (connectionStatus == DISCONNECTING) {
			if (!btSPP.connectionDone()) {
				ESP_LOGI(SPP_SERVER_TAG, "Disconnected");
				connectionStatus = NOT_CONNECTED;
			}
		}
	}
}

bool sendData(uint8_t *pData, int len) {
	if (connectionStatus == CONNECTED) {
		bool ret = btSPP.write(pData, len);
		if (btSPP.isError()) {
			ESP_LOGI(SPP_SERVER_TAG, "Error writing message: %s", btSPP.getErrMessage().c_str());
		}

		return ret;
	}

	return true;	// Can't send but let handler clear data as we aren't connected
}

bool setRname(std::string cmd, std::string name) {
	if (name.size() <= MAX_NAME_LEN) {
		if (strcmp(clientName.value.c_str(), name.c_str()) != 0) {
			clientName = name.c_str();
			clientName.put();

			clientAddress = 0;
			clientAddress.put();

			config.commit();
			clientName.notify();
		}

		return true;
	}

	return false;
}

bool connect(std::string cmd, std::string name) {
	if (name.size() > 0) {
		setRname(cmd, name);
	}

	canConnect = true;

	return true;
}

bool disconnect(std::string cmd, std::string name) {
	ESP_LOGI(SPP_SERVER_TAG, "Disconnecting");
	btSPP.endConnection();
	if (btSPP.isError()) {
		return false;
	}

	connectionStatus = DISCONNECTING;

	return true;
}

bool setName(std::string cmd, std::string name) {
	if (name.size() <= MAX_NAME_LEN) {
		serverName = name.c_str();
		serverName.put();
		config.commit();
		return btGAP.setName(name.c_str());
	}

	return false;
}

bool reportState(std::string cmd, std::string name) {
	Serial1.println(state2string[connectionStatus].c_str());

	return true;
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

  	Serial1.begin(38400, SERIAL_8N1, RXD, TXD);

    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

	EEPROM.begin(2048);
	initFromEEPROM();

	commandHandler.setCommandCallback("RNAME", setRname);
	commandHandler.setCommandCallback("NAME", setName);
	commandHandler.setCommandCallback("CONNECT", connect);
	commandHandler.setCommandCallback("DISCONNECT", disconnect);
	commandHandler.setCommandCallback("STATE", reportState);
	commandHandler.setSendCallback(sendData);
	
	pinMode(COMMAND_PIN, INPUT_PULLUP);
	pinMode(CONNECTED_PIN, OUTPUT);
  	digitalWrite(CONNECTED_PIN, LOW);

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

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
#include <BTSPPServer.h>

#define SPP_SERVER_TAG "SPP_SERVER"

std::unordered_map<BTSPPServer::State, std::string> BTSPPServer::state2string = {
	{NOT_INITIALIZED, "NOT_INITIALIZED"},
    {NOT_CONNECTED, "NOT_CONNECTED"},
	{SEARCHING, "SEARCHING"},
	{CONNECTED, "CONNECTED"},
	{SEARCHING, "SEARCHING"},
	{DISCONNECTING, "DISCONNECTING"}
};

#define MAX_NAME_LEN 63
#define RECV_BUF_SIZE 50

BTSPPServer::BTSPPServer(const std::string& name, HardwareSerial &_serial) :
    serial(_serial),
    commandHandler(_serial),
    btSPP(name, RECV_BUF_SIZE),
    serverName(name),
    clientName("A client"),
    clientAddressCallback([](long address) { ESP_LOGI(SPP_SERVER_TAG, "client address=0x%6.6x", address); }),
    serverNameCallback([](const char *name) { ESP_LOGI(SPP_SERVER_TAG, "server name=%s", name); }),
    clientNameCallback([](const char *name) { ESP_LOGI(SPP_SERVER_TAG, "client name=%s", name); })
{
}

void BTSPPServer::initSPP() {
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

void BTSPPServer::initiateConnection() {
	if (clientAddress != 0ULL) {
		ESP_LOGI(SPP_SERVER_TAG, "Connecting to client");
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
		ESP_LOGI(SPP_SERVER_TAG, "Searching for client");
		if (!btGAP.startInquiry()) {
			ESP_LOGE(SPP_SERVER_TAG, "Error starting inqury: %s", btGAP.getErrMessage());
			connectionStatus = NOT_CONNECTED;
		}
	}
}

void BTSPPServer::setClientAddressCallback(std::function<void(unsigned long address)> callback) {
    clientAddressCallback = callback;
}

void BTSPPServer::setServerNameCallback(std::function<void(const char* name)> callback) {
    serverNameCallback = callback;
}

void BTSPPServer::setClientNameCallback(std::function<void(const char* name)> callback) {
    clientNameCallback = callback;
}

void BTSPPServer::setCommandPin(uint8_t pin) {
    commandPin = pin;
}

void BTSPPServer::setConnectedPin(uint8_t pin) {
    connectedPin = pin;
}

void BTSPPServer::setClientAddress(unsigned long address) {
    clientAddress = address;
}

void BTSPPServer::setServerName(const char *name) {
    serverName = name;
}

void BTSPPServer::setClientName(const char *name) {
    clientName = name;
}

void BTSPPServer::loop() {
    static unsigned long lastReadMs = 0;
    static uint8_t recvBuf[RECV_BUF_SIZE];

    unsigned long now = millis();

    if (digitalRead(commandPin) == HIGH) {
        commandHandler.setMode(CommandHandler::COMMAND);
    } else {
        commandHandler.setMode(CommandHandler::PASSTHROUGH);
    }

    commandHandler.loop();

    if (millis() - lastReadMs > 1000) {
        lastReadMs = now;
        int len = 0;
        if ((len = btSPP.read(recvBuf, sizeof(recvBuf))) > 0) {
            serial.printf("%.*s\r\n", len, recvBuf);
            serial.flush();
            ESP_LOGI(SPP_SERVER_TAG, "Received message: %.*s", len, recvBuf);
        }
    }

    if (connectionStatus == CONNECTED) {
        // Toggle pin. Sending a message is a bad idea because of asynchronicity
        digitalWrite(connectedPin, HIGH);
        canConnect = false;
    } else {
        digitalWrite(connectedPin, LOW);
    }

    if (connectionStatus == NOT_INITIALIZED) {
        initSPP();	// Will move to NOT_CONNECTED if it succeeds
    }

    if ((connectionStatus == NOT_CONNECTED) && canConnect) {
        initiateConnection();	// Will move to CONNECTING or SEARCHING
    }

    if (connectionStatus == SEARCHING) {
        if (btGAP.inquiryDone()) {
            uint8_t *address = btGAP.getAddress(clientName.c_str());
            if (address) {
                clientAddress =  ((uint64_t)address[0]) |
                            ((uint64_t)address[1]) << 8 |
                            ((uint64_t)address[2]) << 16 |
                            ((uint64_t)address[3]) << 24 |
                            ((uint64_t)address[4]) << 32 |
                            ((uint64_t)address[5]) << 40
                            ;
                clientAddressCallback(clientAddress);
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

bool BTSPPServer::sendData(uint8_t *pData, int len) {
	if (connectionStatus == CONNECTED) {
		bool ret = btSPP.write(pData, len);
		if (btSPP.isError()) {
			ESP_LOGI(SPP_SERVER_TAG, "Error writing message: %s", btSPP.getErrMessage().c_str());
		}

		return ret;
	}

	return true;	// Can't send but let handler clear data as we aren't connected
}

bool BTSPPServer::setRname(std::string cmd, std::string name) {
	ESP_LOGI(SPP_SERVER_TAG, "Setting client name to %s", name.c_str());

	if (name.size() <= MAX_NAME_LEN) {
		if (strcmp(clientName.c_str(), name.c_str()) != 0) {
			clientName = name;
			clientNameCallback(clientName.c_str());

			clientAddress = 0;
            clientAddressCallback(clientAddress);
		}

		return true;
	}

	return false;
}

bool BTSPPServer::connect(std::string cmd, std::string name) {
	if (name.size() > 0) {
		setRname(cmd, name);
	}

	ESP_LOGI(SPP_SERVER_TAG, "Connecting to %s", clientName.c_str());

	canConnect = true;

	return true;
}

bool BTSPPServer::disconnect(std::string cmd, std::string name) {
	ESP_LOGI(SPP_SERVER_TAG, "Disconnecting");
	btSPP.endConnection();
	if (btSPP.isError()) {
		return false;
	}

	connectionStatus = DISCONNECTING;

	return true;
}

bool BTSPPServer::setName(std::string cmd, std::string name) {
	if (name.size() <= MAX_NAME_LEN) {
		serverName = name;
        serverNameCallback(serverName.c_str());
		return btGAP.setName(name.c_str());
	}

	return false;
}

bool BTSPPServer::reportState(std::string cmd, std::string name) {
    // ESP_LOGI(SPP_SERVER_TAG, "Sending state %d", connectionStatus);

	serial.println(connectionStatus);
    serial.flush();

	return true;
}

void BTSPPServer::start(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin) {
  	serial.begin(baud, config, rxPin, txPin);

    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

	commandHandler.setCommandCallback("RNAME", [this](std::string cmd, std::string name) { return setRname(cmd, name);});
	commandHandler.setCommandCallback("NAME", [this](std::string cmd, std::string name) { return setName(cmd, name);});
	commandHandler.setCommandCallback("CONNECT", [this](std::string cmd, std::string name) { return connect(cmd, name);});
	commandHandler.setCommandCallback("DISCONNECT", [this](std::string cmd, std::string args) { return disconnect(cmd, args);});
	commandHandler.setCommandCallback("STATE", [this](std::string cmd, std::string name) { return reportState(cmd, name);});
	commandHandler.setSendCallback([this](uint8_t *pData, int len) { return sendData(pData, len);});
	
	pinMode(commandPin, INPUT_PULLUP);
	pinMode(connectedPin, OUTPUT);
  	digitalWrite(connectedPin, LOW);
}

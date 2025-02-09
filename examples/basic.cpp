#include <Arduino.h>
#include <esp_log.h>

#define EXAMPLE_TAG "EXAMPLE"

#define RXD 16
#define TXD 17

int connectionStatus = 0;

String OK_RESPONSE("OK\r");

bool verifySPPCommand(String command) {
	Serial1.println(command);
	String response = Serial1.readStringUntil('\n');
	bool ret = response.equals(OK_RESPONSE);
	if (!ret) {
		ESP_LOGE(EXAMPLE_TAG, "Unexpected response: %s", response.c_str());
	}

	return ret;
}

void getSPPState() {
	Serial1.println("AT+STATE");
	String result = Serial1.readStringUntil('\n');
	int status = result.charAt(0) - '0';
	if (status >= 0 && status <= 9 && connectionStatus != status) {
		connectionStatus = status;
		ESP_LOGI(EXAMPLE_TAG, "SPP Connection status=%d", connectionStatus);
	}
	// Read OK\r\n
	result = Serial1.readStringUntil('\n');
}

void setRname() {
	verifySPPCommand("AT+RNAME=Some Client");
}

void initiateConnection() {
	verifySPPCommand("AT+CONNECT");
}

void closeConnection() {
	verifySPPCommand("AT+DISCONNECT");
}

void setup() {
  	Serial1.begin(38400, SERIAL_8N1, RXD, TXD);
}

void loop() {
    delay(500);

	getSPPState();

    if (connectionStatus == 1) {
        // Server is initialized, try to connect to a named client
        initiateConnection();
    }

    if (connectionStatus == 4) {
        // Server is connect to client, send it some data
        Serial1.println("this will be sent to the SPP client");
    }
}
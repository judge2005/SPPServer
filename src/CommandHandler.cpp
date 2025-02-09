#include <CommandHandler.h>
#include "esp_log.h"

#define COMMAND_TAG "COMMAND_HANDLER"
CommandHandler::CommandHandler(HardwareSerial& _serial) :
    serial(_serial),
    infoCallback([](const char *msg) { ESP_LOGI(COMMAND_TAG, "%s", msg); }),
    debugCallback([](const char *msg) { ESP_LOGD(COMMAND_TAG, "%s", msg); }),
    sendCallback([](uint8_t *data, int len) { ESP_LOGI(COMMAND_TAG, "Should send: %.*s", len, data); return true; }),
    errorCallback([this](Error error) { serial.printf("FAILED(%d)\r\n", error);})
{
}

void CommandHandler::setMode(Mode mode) {
    this->mode = mode;
}

void CommandHandler::setInfoCallback(std::function<void(const char*)> callback) {
  infoCallback = callback;
}

void CommandHandler::setDebugCallback(std::function<void(const char*)> callback){
  debugCallback = callback;
}

void CommandHandler::setSendCallback(std::function<bool(uint8_t*, int)> callback){
  sendCallback = callback;
}

void CommandHandler::setCommandCallback(std::string command, std::function<bool(std::string command, std::string arguments)> commandCallback)
{
    commandCallbacks.insert_or_assign(command, commandCallback);
}

void CommandHandler::setErrorCallback(std::function<void(Error)> callback) {
    errorCallback = callback;
}

void CommandHandler::loop() {
    while (serial.available() > 0) {
        int c = serial.read();
        if (c > 0) {
            delay(1);
            if (x_position < sizeof(x_buffer)) {
                if (c == '\n') {
                    if (x_position > 1 && x_buffer[x_position-1] == '\r') {
                        x_buffer[x_position-1] = 0;
                    } else {
                        x_buffer[x_position] = 0;
                    }
                    if (strncmp((const char*)x_buffer, "AT+", 3) == 0) {
                        parseCommand();
                    } else {
                        sendCallback(x_buffer, x_position-1);
                    }
                    x_position = 0;
                } else {
                    x_buffer[x_position++] = c;
                }
            } else {
                if (c == '\n') {
                    errorCallback(Error::ERROR_BUFFER_OVERFLOW);
                    x_position = 0;
                }
            }
        }
    }
}

void CommandHandler::parseCommand()
{
    if (strncmp((const char*)x_buffer, "AT+", 3) != 0) {
        errorCallback(Error::ERROR_INVALID_COMMAND);
        return;
    }

    const char *cmdPtr = (const char*)(&x_buffer[3]);
    char *eqPtr = strchr(cmdPtr, '=');
    char *argPtr = NULL;

    if (eqPtr != NULL) {
        *eqPtr = 0;
        argPtr = eqPtr + 1;
    }

    cmd = cmdPtr;

    if (commandCallbacks.count(cmd) == 0) {
        errorCallback(Error::ERROR_UNKNOWN_COMMAND);
    }

    std::string args = "";
    if (argPtr != NULL) {
        args = argPtr;
    }

    if (!commandCallbacks[cmd](cmd, args)) {
        errorCallback(Error::ERROR_COMMAND_FAILED);
    } else {
        serial.println("OK");
    }

    return;
}


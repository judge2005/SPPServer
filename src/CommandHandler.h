#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include <functional>
#include <string>
#include <map>

class CommandHandler {
public:
    enum Error : uint8_t {
        ERROR_NONE = 0x00,
        ERROR_INVALID_COMMAND = 0x01,
        ERROR_INVALID_ARGUMENT = 0x02,
        ERROR_COMMAND_FAILED = 0x03,
        ERROR_BUFFER_OVERFLOW = 0x04,
        ERROR_UNKNOWN_COMMAND = 0x05,
        ERROR_UNKNOWN = 0xFF,
    };

    enum Mode : uint8_t {
        COMMAND = 0x0,
        PASSTHROUGH = 0x1,
    };

    CommandHandler(HardwareSerial& serial);

    void loop();
    void setMode(Mode mode);
    void setInfoCallback(std::function<void(const char*)> callback);
    void setDebugCallback(std::function<void(const char*)> callback);
    void setSendCallback(std::function<bool(uint8_t*, int)> callback);
    void setCommandCallback(std::string command, std::function<bool(std::string command, std::string arguments)> commandCallback);
    void setErrorCallback(std::function<void(Error)> errorCallback);
    
private:
    char msgBuf[16];
    uint8_t x_buffer[50];
    uint8_t x_position = 0;
    uint8_t end_position = 0;
    bool foundEquals = false;
    std::string cmd;
    Mode mode = COMMAND;

    HardwareSerial& serial;

    std::function<void(const char*)> infoCallback;
    std::function<void(const char*)> debugCallback;
    std::function<void(Error)> errorCallback;

    std::unordered_map<std::string, std::function<bool(std::string command, std::string arguments)>> commandCallbacks;
    std::function<bool(uint8_t*, int)> sendCallback;

    void parseCommand();
};
#endif
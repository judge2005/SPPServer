#ifndef _BT_SPP_SERVER_H
#define _BT_SPP_SERVER_H

#include <unordered_map>
#include <string>

#include <CommandHandler.h>
#include <BTSPP.h>
#include <BTGAP.h>

class BTSPPServer {
public:
    BTSPPServer(const std::string& name, HardwareSerial &serial);

    typedef enum {
        NOT_INITIALIZED = 0,
        NOT_CONNECTED,
        SEARCHING,
        CONNECTING,
        CONNECTED,
        DISCONNECTING
    } State;

    void setClientAddressCallback(std::function<void(unsigned long address)> callback);
    void setServerNameCallback(std::function<void(const char *name)> callback);
    void setClientNameCallback(std::function<void(const char *name)> callback);
    
    void setClientAddress(unsigned long address);
    void setServerName(const char* name);
    void setClientName(const char * name);

    void setCommandPin(uint8_t pin);
    void setConnectedPin(uint8_t pin);

    void start(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin);
    void loop();

private:
    BTSPP btSPP;
    BTGAP btGAP;
    CommandHandler commandHandler;
    State connectionStatus = NOT_INITIALIZED;
    HardwareSerial &serial;
    bool canConnect = false;
    bool sendRx = false;
    unsigned long clientAddress = 0;
    std::string serverName;
    std::string clientName;
    uint8_t commandPin = 15;
    uint8_t connectedPin = 13;
    
    void initSPP();
    void initiateConnection();

    bool setSendRx(std::string cmd, std::string name);
    bool setRname(std::string cmd, std::string name);
    bool setName(std::string cmd, std::string name);
    bool connect(std::string cmd, std::string name);
    bool disconnect(std::string cmd, std::string args);
    bool reportState(std::string cmd, std::string unused);
    bool sendData(uint8_t *pData, int len);

    std::function<void(unsigned long address)> clientAddressCallback;
    std::function<void(const char *name)> serverNameCallback;
    std::function<void(const char *name)> clientNameCallback;

    static std::unordered_map<State, std::string> state2string;
};
#endif

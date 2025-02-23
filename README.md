This is an implementation of a Bluetooth SPP server - kind of like an HC-05 in server mode, but the HC-05 never really worked for me because:
* You never know what version of the firmware you're going to get with an HC-05. The commands may or may not match any online documentation.
* HC-05 doesn't seem to be designed for embedded systems where the controlling processor is in charge - saving state, telling the server when to connect and disconnect, what to connect to etc. To send commands to it you have to put in to command mode using physical buttons!
* The inquiry mode, where it searches for devices to connect to, doesn't find the devices I want to connect to.
So I wrote this. It is almost general purpose, though not quite. Obviously you need an ESP32 module that implements class bluetooth.

I implemented several AT commands. Note that these commands have to be sent terminated with \r\n (carriage return, newline):

| Command | Parameter |Response|Example|
| -------- | ------- | ------- | ------- |
| AT+NAME= | The name of the server, defaults to "timefliesbridge" |OK|AT+NAME=My Server|
| AT+RNAME= | The name of the client to connect to, defaults to "Time Flies" |OK|AT+RNAME=My Client|
| AT+CONNECT | Try to connect to the client. With no parameter it will used the one set with AT+RNAME, otherwise it will use the name provided as an argument. If the server doesn't have an address stored for this client name, it will search for it until it finds it. |OK|AT+CONNECT=Some Other Client|
| AT+DISCONNECT | Disconnect from whatever client it might be connected to |OK|AT+DISCONNECT|
| AT+STATE | Return the current state |\<state\>\r\nOK|AT+STATE|
| AT+SENDRX= | If argument == 1, send anything received from the SPP client back to our client. If argument == 0, just discard anything received from the SPP client |OK|AT+SENDRX=0|

The state can be any of the following:
|Value|Meaning|Explanation|
|-|-|-|
|0|	NOT_INITIALIZED|Bluetooth stack hasn't been initialized yet - it is initialized at power on|
|1| NOT_CONNECTED|Bluetooth stack is initialized, but the server isn't connected to a client|
|2| SEARCHING | The server is searching for a client RNAME, or the name given in the CONNECT command|
|3| CONNECTING | The server has found the client and is trying to connect to it|
|4| CONNECTED | The server is connected to the client |
|5|	DISCONNECTING | The server is in the process of disconnecting from the client|

When the server is connected to the client, any strings sent to it that dont start with _AT+_ will be sent on to the client.

It should be easy to add more AT commands - they are just impemented as callbacks in the _CommandHandler_ class.

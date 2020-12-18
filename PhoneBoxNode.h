/*
 * PhoneBoxNode.h
 *
 *  Created on: 17 May 2020
 *      Author: bongani
 */

#ifndef PHONEBOXNODE_H_
#define PHONEBOXNODE_H_

#include "spiffs.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <Update.h>
//#include <BLEDevice.h>
//#include <BLEUtils.h>
//#include <BLEServer.h>

/* You only need to format SPIFFS the first time you run a
 test or else use the SPIFFS plugin to create a partition
 https://github.com/me-no-dev/arduino-esp32fs-plugin */
#define FORMAT_SPIFFS_IF_FAILED false
#define OPEN_TAG                0xFA
#define CLOSE_TAG               0x7E
#define HEADER_PACKET_BYTES     4
#define PAYLOAD_PACKET_BYTES    8
#define TRAILER_PACKET_BYTES    4
#define HEADER_PACKET           0xAACAEAFA
#define UPDATE_HEADER_PACKET    0xBBCAEAFA
#define TRAILER_PACKET          0x7EEACAAA
#define BUFFER_SIZE				1024
#define STATUS_LED_PIN			2
#define MAX_IO_MODULES 			10
#define MAX_RETRY_COUNT			3

// Node config
// IO PINS
#define CLOCK_DELAY    		1   // Data shift clock timing delay(1us)
#define CHARGE_DET_DEL 		700 // Data shift clock timing delay(900us)
#define RS485_DIR      		5   // Direction
#define LED_STATUS     		23  // Status LED
#define MSR_DATA_OUT   		18  // Master serial data output pin
#define MSR_SHIFT_CLK  		19  // Serial clock shift pin
#define MSR_LATCH_CLK  		21  // Latch clock pin
#define MSR_DATA_IN    		39  // Slave serial data input pin
#define MSR_LOAD       		25  // Slave data output load
#define MSR_RESET      		26  // Register reset
#define BUZZER		   		22  // Buzzer
#define LIGHT				27  // Alarm light
#define UNUSED_NODE_PIN  	2   // UNUSED...FYI This will disable serial boot if connected
#define NODE_ADDR_BIT0  	4   // Node Address Bit 0
#define NODE_ADDR_BIT1  	32  // Node Address Bit 1
#define NODE_ADDR_BIT2  	33  // Node Address Bit 2
#define MODULE_COUNT_BIT0  	36  // Module Count bit 0
#define MODULE_COUNT_BIT1  	35  // Module Count bit 1
#define MODULE_COUNT_BIT2  	34  // Module Count bit 2

// Modbus parameters
#define RS485_BOUD     		115200  // Modbus rtu boud rate(bps)
#define MAX_REG_CNT    		10	// Amount of holding registers
#define MAX_REG_SIZE   		16	// Maixmum holding register size.

//
#define MAX_MODULE_CNT 		3 // Maximum modules per blue pill
#define IO_COUNT	   		40 // IO structure size
#define INPUT_COUNT    		16 // Number of inputs per io card
#define OUTPUT_COUNT   		24 // Number of outputs per IO card
#define MAX_MODULE_CNT 		10 // Total number of modules connected
#define CHARGE_DETECT  		4  // Number of times the charge pin has to change to be considered charging

#define TOTAL_INPUT_COUNT INPUT_COUNT * MAX_MODULE_CNT // Total number of all avilable inputs

#define pfState_chars 0
#define pfState_firstFmtChar 1
#define pfState_otherFmtChar 2
#define NUMBERBYTES 4
#define BUFFER_ALLOC_SIZE 128


#pragma pack(push, 1) // Disable padding
union IOStructure {
	uint8_t buffer[PAYLOAD_PACKET_BYTES];
	struct {
		uint32_t inputs;
		uint32_t outputs;
	} payload;
};

union Timestamp {
	uint8_t buffer[4];
	struct {
		uint8_t hour;
		uint8_t day;
		uint8_t month;
		uint8_t year;
	} timestamp;
};

union Version {
	uint8_t buffer[13];
	struct {
		uint8_t available;
		uint8_t version[4];
		uint32_t size;
		Timestamp timestamp;
	};
};

union Frame {
	struct {
		uint32_t header;
		uint16_t size;
		uint8_t alarm;
		uint8_t buzzer;
		IOStructure payload[MAX_IO_MODULES];
	} frame;
	uint8_t buffer[sizeof(frame)];
};

typedef enum {
	IDLE, // Device on idle, not data transmission currently in progress
	HEADER, // A header packet is busy being read
	UPDATE_INIT, // Initiate software update process
	UPDATE_START, // Start software update process
	UPDATE_BUSY, // Software update process currently in progress
	UPDATE_DONE, // A software update has just finished
	PAYLOAD, // Payload data is busy being read
	TRAILER, // Trailer data is busy being read
	READY // Data is ready to be sent
} Status;

#pragma pack(pop) // Return to previous mode

TaskHandle_t CMDThread;
TaskHandle_t TCPThread;

String wifi_ssid = "Hosp3s";
String wifi_password = "B3MYGU3$T123";
String firmware_file = "/firmware";
String version_file = "/version";
String server_address = "10.0.0.12";
String server_port = "4444";
String serial_number;

WiFiClient	g_client;
Frame 		g_frame;
//IOStructure g_structure[MAX_IO_MODULES];
Status 		g_status = IDLE;
uint8_t 	g_err_packet[4] = { 0xFA, 0x4E, 0x4F, 0x7E };
uint8_t 	g_ack_packet[4] = { 0xFA, 0x59, 0x45, 0x7E };
uint32_t 	g_received_file_bytes = 0;
File 		g_bin_file;
Version 	g_version;

short moduleCount;
short g_retry_count = 0;
//hw_timer_t *g_timer;

uint8_t g_nodeAddress = 0;
byte 	inputStructure[TOTAL_INPUT_COUNT]; // Stores all the inputs available and passes them to the holding register when needed.
byte 	stateChange[TOTAL_INPUT_COUNT]; // The number of times an input state changed
int 	timeLapsed[TOTAL_INPUT_COUNT]; // Time elapsed since last input state changes
bool 	serialDebug = true;                          // Serial debug flag


void configThread(void *params);

void fileHelp();
void file(int arg_cnt, char **args);

void configHelp();
void loadConfg();
void config(int arg_cnt, char **args);

void deviceHelp();
void scanNetworks();
void status();
void device(int arg_cnt, char **args);

void hw(int arg_cnt, char **args);

void help(int arg_cnt, char **args);

//PluginServer
void BLEInit();
void TCPInit();
void idle();
bool TCPRun();
void installSofwareUpdate();
bool initSoftwareUpdate();
bool handleSoftwareUpdate();
bool handlePayloadPacket();
bool handleHeaderPacket();
bool processPayload();
void setVersion();
void getVersion();
void printVersion();
bool connectServer();
bool connectWifi();
void sendNodeAddress();

//IO
void IOInit();
int  getNodeAddress();
int  getModuleCount();
void loadStructure(IOStructure *structure);
void loadInputs();
void packIOStructure();
void unpackIOStructure();
void printHoldingRegister();
void latchClock();
void shiftClock();
void shiftLoad();

//Threads
void tcpThread(void *params);

#endif /* PHONEBOXNODE_H_ */

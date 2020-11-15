#include "PhoneBoxNode.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

void setup() {
	// Setup serial
	Serial.begin(115200);
	Serial.println("\r\n*******Init*******\r\n");

	// Set LED PIN Mode
	pinMode(STATUS_LED_PIN, OUTPUT);

	// Turn the status LED offg
	digitalWrite(STATUS_LED_PIN, 0);

	//BLEInit();

	TCPInit();
	IOInit();
}

void loop() {
	TCPRun();
}

void tcpThread(void *params) {
	for (;;) {
		TCPRun();
	}
}

void updateRetry() {
	if (++g_retry_count > MAX_RETRY_COUNT) {
		ESP.restart();
	} else {
		//timerWrite(g_timer, 0); //feed watchdog
	}
}

void TCPInit() {
	// Mount the file system
	if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
		Serial.println("Failed to mount SPIFFS");
		return;
	}

	Serial.println("SPIFFS Mounted successfully");

	listDir(SPIFFS, "/", 0);

	// Reset structs
	memset(&g_frame, NULL, sizeof(g_frame));
	memset(&g_version, NULL, sizeof(g_version));
	g_status = IDLE;

	getVersion();

	if (g_version.available) {
		// Check for any available firmware file
		g_bin_file = SPIFFS.open(firmware_file);
		if (g_bin_file.available()) {
			// Firmware file present, Install new software
			installSofwareUpdate();

			// Clear the availability flag
			g_version.available = 0;

			// Restart the device
			Serial.println("Device Reset in 3 Seconds..");
			delay(3000);
			ESP.restart();
		}
	}

	Serial.println("No updates to install");

	//g_timer = timer;

	// Connect WIFI
	WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

	// Check if WIFI is connected
	while (WiFi.status() != WL_CONNECTED) {
		updateRetry();
		delay(5000);
		Serial.println("Connecting to WiFi..");
	}

	Serial.print("Connected to the WiFi network\r\nIP : ");
	Serial.println(WiFi.localIP());

	connectServer();
}

void IOInit() {
// Setup hardware config pins
	pinMode(NODE_ADDR_BIT0, INPUT);
	pinMode(NODE_ADDR_BIT1, INPUT);
	pinMode(NODE_ADDR_BIT2, INPUT);

// Get node address from hardware config pins
	getNodeAddress();

// Setup IO pins
	pinMode(LED_STATUS, OUTPUT);
	pinMode(MSR_DATA_OUT, OUTPUT);
	pinMode(MSR_SHIFT_CLK, OUTPUT);
	pinMode(MSR_LATCH_CLK, OUTPUT);
	pinMode(MSR_RESET, OUTPUT);
	pinMode(MSR_LOAD, OUTPUT);
	pinMode(MSR_DATA_IN, INPUT);

	digitalWrite(MSR_RESET, LOW);

// Get total number of modules connected.
	moduleCount = getModuleCount();

}

bool connectServer() {
	while (!g_client.connect(server_address.c_str(), server_port.toInt())) {
		updateRetry();
		Serial.print("Connecting to server [");
		Serial.print(server_address);
		Serial.print(":");
		Serial.print(server_port);
		Serial.println("]");
		delay(5000);
	}

	sendNodeAddress();
	return true;
}

void sendNodeAddress() {
	g_client.write(64); // Node address
}

bool TCPRun() {
	g_frame.frame.header = 0;
	bool error_flag = false;

	if (!g_client.connected()) {
		connectServer();
		g_status = IDLE;
	}

	// Check if there's available data from server
	while (g_client.available() > 0) {
		error_flag = true;

		// Check the previously set g_status of the device
		switch (g_status) {
		case IDLE:
			idle();
			break;
		case HEADER:
			error_flag = handleHeaderPacket();
			break;
		case UPDATE_INIT:
			error_flag = initSoftwareUpdate();
			break;
		case UPDATE_BUSY:
			error_flag = handleSoftwareUpdate();
			break;
		case PAYLOAD:
			error_flag = handlePayloadPacket();
			break;
		}

		if (!error_flag) {
			g_client.write(g_err_packet, 4);
			Serial.println("Error occurred");
			g_status = IDLE;
		} else if (g_status == READY) {
			g_client.write((byte*) g_frame.buffer, sizeof(g_frame));
			g_status = IDLE;
		} else if (g_status == UPDATE_DONE) {
			setVersion();
			g_client.write(g_ack_packet, 4);
			Serial.println("Download complete");
			Serial.println("Resetting in 5 seconds");
			delay(5000);
			ESP.restart();
		} else if (g_status == UPDATE_START) {
			g_client.write(g_ack_packet, 4);
			Serial.println("Downloading latest version");
			g_status = UPDATE_BUSY;
		}
	}

	return true;

	//timerWrite(g_timer, 0);//feed watchdog
}

void installSofwareUpdate() {
	Serial.println("Installing software update..");
	bool error = false;
	digitalWrite(STATUS_LED_PIN, HIGH);

	size_t fileSize = g_bin_file.size();

	if (!Update.begin(fileSize)) {
		Serial.println("Failed to update firmware");
		return;
	};

	Update.writeStream(g_bin_file);
	error = Update.end();

	if (error) {
		Serial.println("Update installed successfully");
	} else {
		Serial.println("Error Occurred: " + String(Update.getError()));
	}

	g_bin_file.close();
	deleteFile(SPIFFS, firmware_file.c_str());
}

void idle() {
	uint8_t g_byte = g_client.read();

// Check if it's a valid open tag, else data is invalid
	if (g_byte == OPEN_TAG) {
		// Append the the header packet
		g_frame.frame.header |= g_byte;

		// Update device g_status
		g_status = HEADER;
	}
}

bool initSoftwareUpdate() {
	Serial.println("Software update");
	uint8_t g_byte;
	uint8_t x;

// Open binary file for writing
	g_bin_file = SPIFFS.open(firmware_file, "w");
	if (!g_bin_file) {
		Serial.println("Error opening file for writing.");
		return false;
	}

// Reset the version struct
	memset(&g_version, NULL, sizeof(g_version));

// Set the available flag to trigger install on boot.
	g_version.available = 1;

// Get the file size
	for (x = 0; x < 4; x++) {
		g_byte = g_client.read();
		g_version.size |= (g_byte << (8 * x));
	}

// Get the version number
	for (x = 0; x < 4; x++) {
		g_byte = g_client.read();
		g_version.version[(3 - x)] = g_byte;
	}

// Get the version timestamp
	for (x = 0; x < 4; x++) {
		g_byte = g_client.read();
		g_version.timestamp.buffer[(3 - x)] = g_byte;
	}

	Serial.println("New software update available");

	g_received_file_bytes = 0;
	g_status = UPDATE_START;
	return true;
}

bool handleSoftwareUpdate() {
	uint8_t buff[BUFFER_SIZE];
	char tbuff[BUFFER_SIZE];
	int rx_len = 0;

	memset(buff, NULL, sizeof(buff));
	rx_len = g_client.read(buff, BUFFER_SIZE);
	if (rx_len <= 0) {
		Serial.println("Failed to read data from socket.");
		return false;
	}

	if (g_bin_file.write(buff, rx_len) <= 0) {
		Serial.println("File write error.");
		return false;
	}

	g_received_file_bytes += rx_len;

	Serial.print("Downloading software update ");
	Serial.print(g_received_file_bytes, DEC);
	Serial.print(" / ");
	Serial.println(g_version.size, DEC);

	if (g_version.size <= g_received_file_bytes) {
		g_status = UPDATE_DONE;
		g_received_file_bytes = 0;
		g_bin_file.close();
	}

// Toggle LED
	if (digitalRead(STATUS_LED_PIN) == 1)
		digitalWrite(STATUS_LED_PIN, 0);
	else
		digitalWrite(STATUS_LED_PIN, 1);

	return true;
}

bool handleHeaderPacket() {
// Read header packet data
	for (short x = 1; x < HEADER_PACKET_BYTES; x++) {
		uint8_t g_byte = g_client.read();
		g_frame.frame.header |= (g_byte << (8 * x));
	}

	switch (g_frame.frame.header) {
	case HEADER_PACKET:
		// Header packet checks out, now we read the payload
		g_status = PAYLOAD;
		break;
	case UPDATE_HEADER_PACKET:
		// Update packet checks out, now we read the receive the file
		g_status = UPDATE_INIT;
		break;
	default:
		g_status = IDLE;
		return false;
	}

	return true;
}

bool handlePayloadPacket() {
	uint8_t g_byte;
	uint32_t size;
	short x, y;

	// Get payload size
	for (x = 0; x < 2; x++) {
		g_byte = g_client.read();
		g_frame.frame.size |= (g_byte << (8 * x));
	}

	// Get buzzer value
	if (g_client.read() == 0) {
		digitalWrite(BUZZER, LOW);
	} else {
		digitalWrite(BUZZER, HIGH);
	}

	// Get light value
	if (g_client.read() == 0) {
		digitalWrite(LIGHT, LOW);
	} else {
		digitalWrite(LIGHT, HIGH);
	}

	memset(g_frame.frame.payload, NULL, MAX_IO_MODULES);
	// Loop through payload
	for (y = 0; y < g_frame.frame.size; y++) {
		for (x = 0; x < PAYLOAD_PACKET_BYTES; x++) {
			g_byte = g_client.read();
			g_frame.frame.payload[y].buffer[x] = g_byte;
		}
	}

	loadStructure(g_frame.frame.payload);

	g_frame.frame.size = moduleCount;
	g_status = READY;

	return true;
}

/* Loads the structure to the the IO module's shift registers
 * if setup mode is true, the data is not lached to the outputs
 *
 * The structure is made up of 24 outputs and 24 inputs
 */
void loadStructure(IOStructure *structure) {
	byte cursor = 0;       // This marks the index of each structure
	byte structIndex = 0;  // Indexes the number of structures available
	byte inputIndex = 0;
	byte inputCount = 0;
	byte ioVal = 0;
	short bit = 0;

//	// Resset structure
	//memset(&structure, NULL, sizeof(structure));

	// Load all input data from storage registers.
	shiftLoad();
	Serial.print("Outputs : ");
	Serial.println(structure[0].payload.outputs, BIN);

	// Loop through the connected modules
	for (byte x = 0; x < moduleCount; x++) {
		structure[x].payload.inputs = 0;
		// loop through the IOs
		for (byte y = 0; y < OUTPUT_COUNT; y++) {
			bit = (OUTPUT_COUNT * x) + y;

			// Set output value
			if (structure[x].payload.outputs & (1 << bit)) {
				digitalWrite(MSR_DATA_OUT, HIGH);
			} else {
				digitalWrite(MSR_DATA_OUT, LOW);
			}

			if (y < INPUT_COUNT) {
				// Read input
				if (digitalRead(MSR_DATA_IN)) {
					bitSet(structure[x].payload.inputs, bit);
				}
			}

			// Shift the register
			shiftClock();
		}
	}

	Serial.print("Inputs : ");
	Serial.println(structure[0].payload.inputs, BIN);
	Serial.println("\n\n");

	// Latch all shiter data to and from storage registers
	latchClock();
}

bool processPayload() {
	g_frame.frame.size = moduleCount;
	g_status = READY;
	return true;
}

void setVersion() {
	File ver_file = SPIFFS.open(version_file, "w");

	if (!ver_file) {
		Serial.println("Error opening version file for writing.");
		return;
	}

	ver_file.write(g_version.buffer, sizeof(g_version.buffer));
	ver_file.close();
	printVersion();
}

void getVersion() {
	File ver_file = SPIFFS.open(version_file, "r");

	if (!ver_file) {
		Serial.println("Error opening version file for reading.");
		return;
	}

	if (ver_file.read(g_version.buffer, sizeof(g_version.buffer)) < 1) {
		Serial.println("Failed to read version file.");
		return;
	}
	printVersion();
}

void printVersion() {
	char temp_buff[512];
	sprintf(temp_buff,
			"\r\nVersion  : %d.%d.%d\r\nReleased : 20%d-%02d-%d %dH00 [%d bytes]\r\n",
			g_version.version[1], g_version.version[2], g_version.version[3],
			g_version.timestamp.timestamp.year,
			g_version.timestamp.timestamp.month,
			g_version.timestamp.timestamp.day,
			g_version.timestamp.timestamp.hour, g_version.size);
	Serial.println(temp_buff);
}

/* Gets node address from hardware pin configuration
 */
int getNodeAddress() {
// Read the input pins
	return 64;
}

/* Determines the number of IO modules connected
 * by shifting zeros to the shift register and a
 * single SET bit(1) while counting the number of
 * clock cycles it takes for the 1 to be read back.
 */
int getModuleCount() {
	return 1;
}

// Reads the inputs from the IO module's shift register
void loadInputs() {
	byte in;
	byte chargeIndex = 0;
	int stateMachine = millis();
	int timeDiff = 0;

// Load all input data from storage registers.
	shiftLoad();

// Get all the inputs
	for (byte x = 0; x < TOTAL_INPUT_COUNT; x++) {
// Shift the clock
		shiftClock();

// Read the input value
		in = digitalRead(MSR_DATA_IN);

		if (in != inputStructure[x]) {
			timeDiff = stateMachine - timeLapsed[x];

			// If the last change was more than half a minute ago
			if ((timeDiff >= 500) && (stateChange[x] == CHARGE_DETECT)) {

				Serial.println("Still charging");

				inputStructure[chargeIndex + (24 * moduleCount)] = 0; // Set the input to not fully charged

				if (chargeIndex == 7) {
					chargeIndex = 0;
				} else {
					chargeIndex++;
				}

				stateChange[x] = 0; // Reset the state change count to start counting again.

			} else if ((timeDiff == stateMachine)
					|| (stateChange[x] != CHARGE_DETECT)) {

				// The input just changed and we record the time
				timeLapsed[x] = stateMachine; // Set the current state machine
				stateChange[x] += 1; // Capture the number of times the state changed

			}
		}

		// Update the input structure
		inputStructure[x] = in;
	}
}

void latchClock() {
	delay(CLOCK_DELAY);
	digitalWrite(MSR_LATCH_CLK, HIGH);
	digitalWrite(MSR_LATCH_CLK, LOW);
	delay(CLOCK_DELAY);
}

void shiftClock() {
	delay(CLOCK_DELAY);
	digitalWrite(MSR_SHIFT_CLK, HIGH);
	digitalWrite(MSR_SHIFT_CLK, LOW);
	delay(CLOCK_DELAY);
}

void shiftLoad() {
	digitalWrite(MSR_LOAD, LOW);
	delay(CLOCK_DELAY);
	latchClock();
	delay(CLOCK_DELAY);
	digitalWrite(MSR_LOAD, HIGH);
}

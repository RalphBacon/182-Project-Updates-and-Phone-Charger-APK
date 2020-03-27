/*
 * Based on original sketch:
 * Bluetooth HC-06/05 (SLAVE) control from your Android phone RSB May 2016
 */
#include "Arduino.h"
#include <SoftwareSerial.h>
#include <stdlib.h> // atoi

// If you are using an HC06 set the following line to false
#define USING_HC05 true

// Instantiate our BT object. First value is RX pin, second value TX pin
// NOTE: do NOT connect the RX directly to the Arduino unless you are using a
// 3.3v board. In all other cases connect pin 4 to a 1K2 / 2K2 resistor divider
/*
 * ---- pin 6-----> |----1K2----| to HC06 RX |----2K2----| -----> GND
 *
 * See my video #36 & #37 at www.youtube.com/RalphBacon for more details.
 */
// Communicate with the BT device on software controlled serial pins
SoftwareSerial BTserial(4, 6); // RX , TX

// Our BT Serial Buffer where BT sends its text data to the Arduino
char buffer[18] = { 0 };

// Get the flag to show whether the phone thinks it is plugged in
bool isPluggedIn = false;

// Conversion buffer for the individual 3-digit integer values
char btValue[4] = { 0 };

// Heartbeat buffer (string) will contain "HEARTBEAT"
char heartBeat[10] = { '\0' };
unsigned long lastHeartBeat = 0;

// Connected? Only works on HC-05
#define connectedState 8
bool prevStateDisconnected = true;

// Power on/off LED
#define pwrControlLED 9

// Connected LED
#define connectedLED 10

// Heartbeat LED (must be PWM if you want it to fade in/out)
#define heartbeatLED 11

// Are we charging to the MAX ppint or discharging to MIN level?
// Note this depends on whehter the output is HIGH or LOW in Setup()
bool chargingUp = true;

// Forward declarations
void printDateTimeStamp(char buffer[15]);
void displayBTbuffer();
bool extractHeartBeatFromBTdata(int startIdx, int endIdx);
int extractDataFromBTdata(int startIdx, int endIdx);
void printRawData();
void pluggedInStatus();

// -----------------------------------------------------------------------------------
// SET UP   SET UP   SET UP   SET UP   SET UP   SET UP   SET UP   SET UP   SET UP
// -----------------------------------------------------------------------------------
void setup() {

	// Power charging pin LED
	pinMode(pwrControlLED, OUTPUT);
	digitalWrite(pwrControlLED, HIGH);

	// Connected LED
	pinMode(connectedLED, OUTPUT);
	digitalWrite(connectedLED, LOW);

	// Heartbeat LED (fades in/out on PMW)
	pinMode(heartbeatLED, OUTPUT);
	digitalWrite(heartbeatLED, LOW);

	// State pin (is Bluetooth device connected according to HC05?)
	pinMode(connectedState, INPUT);

	// Serial Windows stuff
	Serial.begin(9600);

	// Set baud rate of HC-06 that you set up using the FTDI USB-to-Serial module
	BTserial.begin(9600);

	// Ensure we don't wait forever for a BT character
	BTserial.setTimeout(750); //mS

	// Setup done
	Serial.println("Set up complete");
}

// Main process to inspect the BT data from phone
void processBTdata()
{
	// Get the chars. This blocks until the number specified
	// has been read or it times out
	int byteCount = BTserial.readBytes(buffer, 18);

	// Useful to see the raw BT buffer in debugging
	printRawData();

	// If we timed out we won't have the full X bytes
	if (byteCount == 18)
	{
		// first 8 chars are time format "hh:MM:ss"
		printDateTimeStamp(buffer);

		// Flag for whether phone is currently plugged in
		isPluggedIn = buffer[17] == '1';

		// Is this the heartbeat?
		if (extractHeartBeatFromBTdata(8, 16))
		{
			Serial.println("Heartbeat received.");
			pluggedInStatus();

			// Reset the heartbeat clock so we don't get a warning
			lastHeartBeat = millis();
		} else
		{
			/* This next section extracts the integer values from
			 * the text data, and displays lots of debugging info
			 * so we can track what is going on whilst developing
			 * this program!
			 */
			int batLevel = extractDataFromBTdata(8, 10);
			Serial.print("Battery Level:");
			Serial.print(batLevel);
			displayBTbuffer();

			printDateTimeStamp(buffer);

			int maxCharge = extractDataFromBTdata(11, 13);
			Serial.print("Max Charge Level:");
			Serial.print(maxCharge);
			displayBTbuffer();

			printDateTimeStamp(buffer);

			int minCharge = extractDataFromBTdata(14, 16);
			Serial.print("Min charge Level:");
			Serial.print(minCharge);
			displayBTbuffer();

			printDateTimeStamp(buffer);

			Serial.print("Phone plugged in:");
			if (isPluggedIn)
			{
				Serial.println("Yes");
			} else
			{
				Serial.println("No");
			}

			// If the battery is now >= max wanted, switch off
			if (batLevel >= maxCharge && chargingUp)
			{
				digitalWrite(pwrControlLED, LOW);
				chargingUp = false;
			}

			// If the battery is now <= min wanted, switch on
			if (batLevel <= minCharge && !chargingUp)
			{
				digitalWrite(pwrControlLED, HIGH);
				chargingUp = true;
			}

			printDateTimeStamp(buffer);
			if (chargingUp)
			{
				Serial.println("Charging.");
			} else
			{
				Serial.println("Charging paused.");
			}

			// Is phone plugged in when it should be?
			pluggedInStatus();

			// Data in lieu of heartbeat still counts as heartbeat
			lastHeartBeat = millis();
		}
	} else
	{
		Serial.print("Only received ");
		Serial.print(byteCount);
		Serial.println(" characters - ignored.");
	}
	// Discard partial data in serial buffer
	while (BTserial.available())
	{
		BTserial.read();
		Serial.println("Discarded a serial character.");
	}
	// clear BT buffer
	memset(buffer, 0, sizeof(buffer));
}

// -----------------------------------------------------------------------------------
// MAIN LOOP     MAIN LOOP     MAIN LOOP     MAIN LOOP     MAIN LOOP     MAIN LOOP
// -----------------------------------------------------------------------------------
void loop() {
	/* If the HC-06/05 has some data for us, get it.
	 *
	 * First 8 bytes time in text format hh:MM:ss
	 *
	 * Then follows three variables of 3 bytes each in text format
	 * that contain the current battery level, the level at which the
	 * user wants to start charging and the level the user wants to
	 * stop charging
	 *
	 * The final byte is whether the phone is currently plugged in.
	 *
	 * If there has been no change to the battery level then a
	 * heartbeat is sent just so that this sketch knows the phone
	 * is still sending data down the line
	 */

#if USING_HC05
	// Only if connected do any of this (assume HC06 always connected)
	if (digitalRead(connectedState))
	#else
	// When using HC06 cannot detected whether connected
	if(true)
#endif
	{
		// Confirm connected state if previously disconnected
		if (prevStateDisconnected)
		{
			Serial.println("CONNECTED.");
			prevStateDisconnected = false;

			// TODO Turn on the (blue) connected LED here
		}

		// If we have serial data to process
		if (BTserial.available())
		{
			// Get the chars. This blocks until the number specified
			// has been read or it times out
			processBTdata();
		} else
		{

			// We are connected but no data at this time.

			// Check last heartbeat
			// See http://www.gammon.com.au/millis on why we do it this way
			if (millis () - lastHeartBeat >= 300000UL)
			{
				Serial.println("Connected, but no heartbeat for 5 minutes");
				lastHeartBeat = millis();

				// TODO Do something with the heartbeat LED here
			}
		}
	} else
	{
		// If we were previously connected but now are not
		if (!prevStateDisconnected)
		{
			Serial.println("NOT CONNECTED.");
			prevStateDisconnected = true;

			// TODO Do something with the connected LED here
		}
	}

	// Give the data a chance to arrive
	delay(100);
}

// Print the first 8 characters - always the timestamp
void printDateTimeStamp(char buffer[15]) {

	for (auto cnt = 0; cnt < 8; cnt++)
	{
		Serial.print((char) buffer[cnt]);
	}
	Serial.print(" ");
}

// Display the extracted 3-character value buffer
void displayBTbuffer() {
	Serial.print(" (");
	for (auto cnt = 0; cnt < 3; cnt++)
	{
		Serial.print(btValue[cnt]);
	}
	Serial.println(")");
}

// Extract the data element to check for heartbeat string
bool extractHeartBeatFromBTdata(int startIdx, int endIdx) {

	int hbIdx = 0;
	memset(heartBeat, '\0', sizeof(heartBeat));
	for (auto cnt = startIdx; cnt <= endIdx; cnt++)
	{
		heartBeat[hbIdx++] = buffer[cnt];
	}

//	Serial.print("Heartbeat buffer:");
//	for (auto cnt = 0; cnt < 9; cnt++)
//	{
//		Serial.print(heartBeat[cnt]);
//	}

	char key[] = "HEARTBEAT";
	if (strcmp(heartBeat, key) == 0)
	{
		return true;
	} else
	{
		return false;
	}
}

// Extract the 3-digit value from the string BT data
int extractDataFromBTdata(int startIdx, int endIdx) {

	// Clear the target buffer and ensure last (4th) char
	// is a null terminator 0
	memset(btValue, 0, sizeof(btValue));

	int btValueIdx = 0;
	for (auto cnt = startIdx; cnt <= endIdx; cnt++)
	{
		btValue[btValueIdx++] = buffer[cnt];
	}

	// Convert the 'string' to an integer
	int returnValue = atoi(btValue);
	return returnValue;
}

// What's coming in the BT serial buffer?
void printRawData()
{
	Serial.print("Raw buffer: ");
	for (auto cnt = 0; cnt < 18; cnt++)
	{
		Serial.print(buffer[cnt]);
	}
	Serial.println(" ");
}

// Is phone connected to USB power source?
void pluggedInStatus() {
	// Phone not plugged in?
	if (!isPluggedIn && chargingUp)
	{
		printDateTimeStamp(buffer);
		Serial.println("Plug phone in to Charge.");
	}
}

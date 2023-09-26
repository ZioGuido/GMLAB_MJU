//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GMLAB MJU - Midi Jack USB
// Basic firmware by Guido Scognamiglio
// Visit: www.gmlab.it - www.GenuineSoundware.com
// Last update: Sep 2023
//

// HOW TO USE:
// Press the upper button to select page (Jack Type, Midi Type, Midi Channel, Midi Number, Range Min, Range Max, Monitor)
// Press the lower button to select value
// Current status is automatically remembered


// Uncomment this line to compile with USB MIDI
#define USBMIDI 1

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PIN DEFINITIONS (DO NOT CHANGE!)
#define PIN_SEGMENT_A 21
#define PIN_SEGMENT_B 20
#define PIN_SEGMENT_C 19
#define PIN_SEGMENT_D 18
#define PIN_SEGMENT_E 15
#define PIN_SEGMENT_F 14
#define PIN_SEGMENT_G 16
#define PIN_SEGMENT_P 10
#define PIN_DIGIT0    5
#define PIN_DIGIT1    7
#define PIN_DIGIT2    9
#define PIN_JACK_T    4 // Analog input A6
#define PIN_JACK_R    6 // Analog input A7
#define PIN_JACK_S    8 // Analog input A8
#define PIN_BTN_UP    2
#define PIN_BTN_DN    3


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Include libraries
#include <EEPROM.h>
#include <MIDI.h>
MIDI_CREATE_DEFAULT_INSTANCE();

// Remember that if this project is compiled with USB MIDI, the Arduino IDE won't be able 
// to "see" the board during the upload of the sketch until the RESET button is pressed.
#ifdef USBMIDI
#include <MIDIUSB.h>
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This library creates a timer with millisecond precision
#include "MillisTimer.h"
MillisTimer DisplayTimer; // This is needed for the display
MillisTimer ButtonTimer;  // This is used for debouncing the buttons
MillisTimer PageTimer;    // This is used for changing display pages

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Variables for the Buttons
int button_pin = -1;
int DebounceTimer, AutorepeatTimer;
#define DEBOUNCE_TIME 100
#define AUTOREPEAT_MAX_TIME  500
#define AUTOREPEAT_MIN_TIME  100
enum Buttons { BTN_UP, BTN_DN };


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global variables
char DisplayText[3]; // Holds the text (or number) in the 3 LED digits
int dgt = 0; // Used to cycle through the 3 digits
int MenuPage = -1; // Will be incremented the first time the button is pressed, so will start from page 0
int JackType = 0;
int MidiType = 0;
int MidiChan = 0;
int MidiWhat = 0;
int MidiMin = 0;
int MidiMax = 127;
int PrevMidiValue = -1;
#define MidiType_MAX 2

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// These are used to stabilize the ADC reading
#define DEADBAND    8
int prev_value = DEADBAND;

enum EEPROM_MemoryLocations
{
	EE_LOC_Skip_0 = 0,
	EE_LOC_JackType,
	EE_LOC_MidiType,
	EE_LOC_MidiChan,
	EE_LOC_MidiWhat,
	EE_LOC_MidiMin,
	EE_LOC_MidiMax,
};

// Accessories that can be connected to the TRS input
enum JackTypesEnum
{
	e_Jack_ExpPed_WiperOnTip = 0,
	e_Jack_ExpPed_WiperOnRing,
	e_Jack_HalfMoon_Crumar,
	e_Jack_HalfMoon_Hammond,
	e_Jack_Sustain_Positive,
	e_Jack_Sustain_Negative,

	e_JackType_MAX // used to wrap selection counter
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Display strings
const char MenuPages[][4] = { "TRS", "TYP", "CHN", "NUM", "MIN", "MAX", "MON" };
const char JackTypes[][4] = { "EP1", "EP2", "HMC", "HMH", "SPP", "SPN" };
const char MidiTypes[][4] = { " CC", " PC", "NOT" }; // Control Change, Program Change, Note Event


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 7 segment LED display characters
const unsigned char alphabet[] =
{  // bit order: 0 A B C D E F G P
  0b011111100,  // 0  // ASCII CHAR 48
  0b001100000,  // 1
  0b011011010,  // 2
  0b011110010,  // 3
  0b001100110,  // 4
  0b010110110,  // 5
  0b010111110,  // 6
  0b011100000,  // 7
  0b011111110,  // 8
  0b011110110,  // 9

  0b000000010,  // Minus sign -
  0,            //    // UNUSED ASCII CHARS
  0,
  0,
  0,
  0,
  0,

  0b011101110,  // A  // ASCII CHAR 65
  0b000111110,  // B
  0b010011100,  // C
  0b001111010,  // D
  0b010011110,  // E
  0b010001110,  // F
  0b010111100,  // G
  0b000101110,  // H
  0b000100000,  // I
  0b001110000,  // J
  0b000000000,  // K (null)
  0b000011100,  // L
  0b011101100,  // M
  0b000101010,  // N
  0b000111010,  // O
  0b011001110,  // P
  0b011100110,  // Q
  0b000001010,  // R
  0b010110110,  // S
  0b000011110,  // T
  0b001111100,  // U
  0b000111000,  // V
  0b000000000,  // W (null)
  0b001101110,  // X (null)
  0b001110110,  // Y
  0b001001010,  // Z
};

// This function prints a single character on the LED display by turning the segments on or off 
void PrintChar(unsigned char c) // Pass ASCII code
{
	unsigned char chr;
	switch (c)
	{
	case '-': chr = alphabet[10]; break;  // Dash symbol
	case ' ': chr = alphabet[11]; break;  // Whitespace
	default:
		// Our array starts from 0, so shift the ASCII value by -48
		chr = alphabet[c - 48];
	}

	// This display uses common anode, so a segment must be LOW for On and HIGH for Off
	digitalWrite(PIN_SEGMENT_A, !(chr >> 7 & 1));
	digitalWrite(PIN_SEGMENT_B, !(chr >> 6 & 1));
	digitalWrite(PIN_SEGMENT_C, !(chr >> 5 & 1));
	digitalWrite(PIN_SEGMENT_D, !(chr >> 4 & 1));
	digitalWrite(PIN_SEGMENT_E, !(chr >> 3 & 1));
	digitalWrite(PIN_SEGMENT_F, !(chr >> 2 & 1));
	digitalWrite(PIN_SEGMENT_G, !(chr >> 1 & 1));
	digitalWrite(PIN_SEGMENT_P, !(chr & 1));
}

// Called to turn all segments off
void ClearChar()
{
	digitalWrite(PIN_SEGMENT_A, 1);
	digitalWrite(PIN_SEGMENT_B, 1);
	digitalWrite(PIN_SEGMENT_C, 1);
	digitalWrite(PIN_SEGMENT_D, 1);
	digitalWrite(PIN_SEGMENT_E, 1);
	digitalWrite(PIN_SEGMENT_F, 1);
	digitalWrite(PIN_SEGMENT_G, 1);
	digitalWrite(PIN_SEGMENT_P, 1);
}

// Called to set a 3-digit number to be displayed (0 to 999)
void DisplayNumber(unsigned char num)
{
	unsigned char h = num / 100;                // Hundreds (left digit)
	unsigned char t = (num - h * 100) / 10;     // Tens     (middle digit)
	unsigned char u = num - h * 100 - t * 10;   // Units    (right digit)

	// Add 48 to match ASCII value
	// These conditional statements replace the leading zeroes with whitespaces
	DisplayText[0] = h > 0 ? 48 + h : ' ';
	DisplayText[1] = t > 0 ? 48 + t : (h > 0 ? 48 + t : ' ');
	DisplayText[2] = 48 + u;
}

// This function is called by the timer to cycle through the display digits
void DisplayLoop()
{
	// 1. Call this at each interaction before turning the segments off in order to reduce the "leakage"
	ClearChar();

	// 2. Shift to next digit
	if (++dgt > 2) dgt = 0;

	// 3. Toggle digits (turn on the current digit and turn off the other two)
	digitalWrite(PIN_DIGIT0, dgt == 0);
	digitalWrite(PIN_DIGIT1, dgt == 1);
	digitalWrite(PIN_DIGIT2, dgt == 2);

	// 4. Print the requested character
	PrintChar(DisplayText[dgt]);
}

// Called to cycle through display pages
void BrowsePage()
{
	if (++MenuPage > 6) MenuPage = 0;
	memcpy(DisplayText, MenuPages[MenuPage], 3);

	// Start a timer of 1 second, the page value will be shown when this expires
	PageTimer.reset();
	PageTimer.start();
}

// Called to set a value to the current page
void SetPageValue()
{
	// Change value if the button has been pushed
	if (button_pin == BTN_DN)
	{
		switch (MenuPage)
		{
		case 0: // Jack Type
			if (++JackType >= e_JackType_MAX) JackType = 0;
			EEPROM.update(EE_LOC_JackType, JackType);
			ConfigureTRS();
			break;

		case 1: // Midi Type
			if (++MidiType > MidiType_MAX) MidiType = 0;
			EEPROM.update(EE_LOC_MidiType, MidiType);
			break;

		case 2: // Midi Channel
			if (++MidiChan > 15) MidiChan = 0;
			EEPROM.update(EE_LOC_MidiChan, MidiChan);
			break;

		case 3: // Midi What
			if (++MidiWhat > 127) MidiWhat = 0;
			EEPROM.update(EE_LOC_MidiWhat, MidiWhat);
			break;

		case 4: // Range Min
			if (++MidiMin > 127) MidiMin = 0;
			EEPROM.update(EE_LOC_MidiMin, MidiMin);
			break;

		case 5: // Range Max
			if (++MidiMax > 127) MidiMax = 0;
			EEPROM.update(EE_LOC_MidiMax, MidiMax);
			break;
		}
	}

	// Display current value
	switch (MenuPage)
	{
	case 0: // Jack Type
		memcpy(DisplayText, JackTypes[JackType], 3);
		break;

	case 1: // Midi Type
		memcpy(DisplayText, MidiTypes[MidiType], 3);
		break;

	case 2: // Midi Channel
		DisplayNumber(MidiChan + 1);
		break;

	case 3: // Midi What
		DisplayNumber(MidiWhat);
		break;

	case 4: // Midi Min
		DisplayNumber(MidiMin);
		break;

	case 5: // Midi Max
		DisplayNumber(MidiMax);
		break;
	}
}

// Called whenever the TRS Type is changed, sets the I/O ports for the desired type
void ConfigureTRS()
{
	switch (JackType)
	{
	case e_Jack_ExpPed_WiperOnTip:
		pinMode(PIN_JACK_T, INPUT);
		pinMode(PIN_JACK_R, OUTPUT);  digitalWrite(PIN_JACK_R, HIGH);   // Set Ring as VCC
		pinMode(PIN_JACK_S, OUTPUT);  digitalWrite(PIN_JACK_S, LOW);    // Set Sleeve as GND
		break;

	case e_Jack_ExpPed_WiperOnRing:
		pinMode(PIN_JACK_T, OUTPUT);  digitalWrite(PIN_JACK_T, HIGH);   // Set Tip as VCC
		pinMode(PIN_JACK_R, INPUT);
		pinMode(PIN_JACK_S, OUTPUT);  digitalWrite(PIN_JACK_S, LOW);    // Set Sleeve as GND
		break;

	case e_Jack_HalfMoon_Crumar:
		pinMode(PIN_JACK_T, INPUT_PULLUP);
		pinMode(PIN_JACK_R, OUTPUT);  digitalWrite(PIN_JACK_R, LOW);   // Set Ring as GND
		pinMode(PIN_JACK_S, INPUT_PULLUP);
		break;

	case e_Jack_HalfMoon_Hammond:
		pinMode(PIN_JACK_T, INPUT_PULLUP);
		pinMode(PIN_JACK_R, INPUT_PULLUP);
		pinMode(PIN_JACK_S, OUTPUT);  digitalWrite(PIN_JACK_S, LOW);    // Set Sleeve as GND
		break;

	case e_Jack_Sustain_Positive:
	case e_Jack_Sustain_Negative:
		pinMode(PIN_JACK_T, INPUT_PULLUP);
		pinMode(PIN_JACK_R, OUTPUT);  digitalWrite(PIN_JACK_R, LOW);    // Set Ring as GND
		pinMode(PIN_JACK_S, OUTPUT);  digitalWrite(PIN_JACK_S, LOW);    // Set Sleeve as GND
		break;
	}
}

// Reads an Analog Input and applies the "dead band" filter
int ReadADC(int ADC_PIN)
{
	// Get the 10-bit value from the ADC
	int value = analogRead(ADC_PIN);

	// Get difference between current and previous value
	int diff = abs(value - prev_value);

	// Exit this function if the new value is within the deadband
	if (diff <= DEADBAND) return;

	// Store new value
	prev_value = value;

	// Get the 7 bit Midi value by shifting the bits to the right by 3 positions
	return value >> 3;
}

int KeepInRange(int value)
{
	auto r1 = MidiMax - MidiMin;
	auto r2 = r1 * value;
	auto r3 = r2 / 127;
	return r3 + MidiMin;
}

// Called to read the TRS inputs and generate the MIDI event whenever the input has changed status
void ReadTRS()
{
	int MidiValue;

	switch (JackType)
	{
		///////////////////////////////////////////////////
	case e_Jack_ExpPed_WiperOnTip:
		MidiValue = KeepInRange(ReadADC(A6));
		break;

		///////////////////////////////////////////////////
	case e_Jack_ExpPed_WiperOnRing:
		MidiValue = KeepInRange(ReadADC(A7));
		break;

		///////////////////////////////////////////////////
	case e_Jack_HalfMoon_Crumar:
		if (digitalRead(PIN_JACK_T) == LOW)
			MidiValue = 127;  // Fast
		else
			if (digitalRead(PIN_JACK_S) == LOW)
				MidiValue = 0;    // Slow
			else
				MidiValue = 64;   // Stop
		break;

		///////////////////////////////////////////////////
	case e_Jack_HalfMoon_Hammond:
		if (digitalRead(PIN_JACK_T) == LOW)
			MidiValue = 0;    // Slow
		else
			if (digitalRead(PIN_JACK_R) == LOW)
				MidiValue = 127;  // Fast
			else
				MidiValue = 64;   // Stop
		break;

		///////////////////////////////////////////////////
	case e_Jack_Sustain_Positive:
		MidiValue = digitalRead(PIN_JACK_T) ? 127 : 0;
		break;

		///////////////////////////////////////////////////
	case e_Jack_Sustain_Negative:
		MidiValue = digitalRead(PIN_JACK_T) ? 0 : 127;
		break;
	}

	// Send only if the state has changed
	if (PrevMidiValue != MidiValue)
		SendMidiMessage(MidiValue);

	// Remember previous state
	PrevMidiValue = MidiValue;
}

// Called after a button has been debounced
void ButtonUpdate()
{
	switch (button_pin)
	{
	case BTN_UP:
		BrowsePage();
		break;

	case BTN_DN:
		SetPageValue();
		break;
	}

	// Accelerate auto-repeat
	if (AutorepeatTimer > AUTOREPEAT_MIN_TIME)
		AutorepeatTimer -= 100;
}

// Called every 1 millisecond to check for button states and apply the debounce timer
void CheckButtons()
{
	// Check if either upper or lower button has been depressed
	if (digitalRead(PIN_BTN_UP) == LOW)
	{
		button_pin = BTN_UP;
		++DebounceTimer;
	}
	else
		if (digitalRead(PIN_BTN_DN) == LOW)
		{
			button_pin = BTN_DN;
			++DebounceTimer;
		}
		else
		{
			// If no buttons are pushed, reset the debounce timer
			button_pin = -1;
			DebounceTimer = 0;
			AutorepeatTimer = AUTOREPEAT_MAX_TIME;
			return;
		}

	// Check debounce
	if (DebounceTimer == DEBOUNCE_TIME) ButtonUpdate();
	if (DebounceTimer >= AutorepeatTimer) DebounceTimer = 0; // Enable auto-repeat
}

// Called to generate and send the desired MIDI event
void SendMidiMessage(int MidiValue)
{
	switch (MidiType)
	{
	case 0: // Send Control Change
		MIDI.sendControlChange(MidiWhat, MidiValue, MidiChan + 1);
		break;

	case 1: // Send Program Change
		MIDI.sendProgramChange(MidiValue, MidiChan + 1);
		break;

	case 2: // Send Note On
		MIDI.sendNoteOn(MidiWhat, MidiValue, MidiChan + 1);
		break;
	}

#ifdef USBMIDI
	midiEventPacket_t Event;
	switch (MidiType)
	{
	case 0: // Send Control Change
		Event = { 0x0B, 0xB0 | MidiChan, MidiWhat, MidiValue };
		MidiUSB.sendMIDI(Event);
		MidiUSB.flush();
		break;

	case 1: // Send Program Change
		Event = { 0x0C, 0xC0 | MidiChan, MidiValue, 0 };
		MidiUSB.sendMIDI(Event);
		MidiUSB.flush();
		break;

	case 2: // Send Note On
		Event = { 0x09, 0x90 | MidiChan, MidiWhat, MidiValue };
		MidiUSB.sendMIDI(Event);
		MidiUSB.flush();
		break;
	}
#endif

	// Monitor MidiValue
	if (MenuPage == 6)
		DisplayNumber(MidiValue);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Called at startaup to setup pins and initialize variables and objects
void setup()
{
	// Setup output pins for the display
	pinMode(PIN_SEGMENT_A, OUTPUT);
	pinMode(PIN_SEGMENT_B, OUTPUT);
	pinMode(PIN_SEGMENT_C, OUTPUT);
	pinMode(PIN_SEGMENT_D, OUTPUT);
	pinMode(PIN_SEGMENT_E, OUTPUT);
	pinMode(PIN_SEGMENT_F, OUTPUT);
	pinMode(PIN_SEGMENT_G, OUTPUT);
	pinMode(PIN_SEGMENT_P, OUTPUT);
	pinMode(PIN_DIGIT0, OUTPUT);
	pinMode(PIN_DIGIT1, OUTPUT);
	pinMode(PIN_DIGIT2, OUTPUT);

	// Setup input pins for the buttons
	pinMode(PIN_BTN_UP, INPUT_PULLUP);
	pinMode(PIN_BTN_DN, INPUT_PULLUP);

	// Setup pins for the TRS Jack, their status can be changed later according to jack configuration
	pinMode(PIN_JACK_T, INPUT_PULLUP);
	pinMode(PIN_JACK_R, INPUT_PULLUP);
	pinMode(PIN_JACK_S, OUTPUT);
	digitalWrite(PIN_JACK_S, LOW); // Set Sleeve as GND

	// INTRO SCREEN
	memcpy(DisplayText, "MJU", 3);

	// Restore last settings from EEPROM
	JackType = EEPROM.read(EE_LOC_JackType);
	MidiType = EEPROM.read(EE_LOC_MidiType);
	MidiChan = EEPROM.read(EE_LOC_MidiChan);
	MidiWhat = EEPROM.read(EE_LOC_MidiWhat);
	MidiMin = EEPROM.read(EE_LOC_MidiMin);
	MidiMax = EEPROM.read(EE_LOC_MidiMax);
	ConfigureTRS();

	// These checks are actually needed in case the EEPROM is empty (all bytes 255)
	if (JackType >= e_JackType_MAX) JackType = 0;
	if (MidiType > MidiType_MAX) MidiType = MidiType_MAX;
	if (MidiChan > 15) MidiChan = 0;
	if (MidiWhat > 127) MidiWhat = 0;
	if (MidiMin > 127) MidiMin = 0;
	if (MidiMax > 127) MidiMax = 127;

	// Initialize the buttons
	button_pin = -1;
	DebounceTimer = 0;
	AutorepeatTimer = AUTOREPEAT_MAX_TIME;
	ButtonTimer.setInterval(1); // Check for button presses every millisecond
	ButtonTimer.expiredHandler(CheckButtons);
	ButtonTimer.start();

	// Setup timer for the display. 
	// With a faster scan (lower interval) the flicker can be reduced but the light leakage increases.
	// Five milliseconds per each digit, considering it's only 3, seems fair enough.
	DisplayTimer.setInterval(5);
	DisplayTimer.expiredHandler(DisplayLoop);
	DisplayTimer.start();

	// This timer is configured as a "one-shot" timer, and is used to switch between menu page name and page value.
	// Will be reset and restarted every time a page is changed.
	PageTimer.setInterval(1000);
	PageTimer.setRepeats(1); // Will stop automatically once expired
	PageTimer.expiredHandler(SetPageValue);

	// Setup MIDI
	MIDI.turnThruOn(); // The MIDI input isn't used in this project...
	MIDI.begin(MIDI_CHANNEL_OMNI);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The main running loop
void loop()
{
	MIDI.read();

	// Read TRS input
	ReadTRS();

	// Run Timers
	DisplayTimer.run();
	PageTimer.run();
	ButtonTimer.run();
}

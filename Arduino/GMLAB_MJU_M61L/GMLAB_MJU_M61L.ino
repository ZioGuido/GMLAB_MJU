//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GMLAB MJU - Mojo61L adapter
// This is a modified firmware for the MJU that allows it to transform the data coming 
// from the MOJO61L keyboard into standard MIDI note events
//
// Firmware by Guido Scognamiglio
// Visit: www.gmlab.it
// Last update: December 2022
//

// HOW TO USE:
// Press the upper button to select page (Jack Type, Midi Type, Midi Channel, Midi Number, Velocity, Transpose, Monitor)
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
char DisplayText[4]; // Holds the text (or number) in the 3 LED digits
int dgt = 0; // Used to cycle through the 3 digits
int MenuPage = -1; // Will be incremented the first time the button is pressed, so will start from page 0
int JackType = 0;
int MidiType = 0;
int MidiChan = 0;
int MidiWhat = 0;
int PrevMidiValue = -1;
#define MidiType_MAX 2

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// These are used to stabilize the ADC reading
#define DEADBAND    8
int prev_value = DEADBAND;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Variables and includes for the Mojo61L scanboard processing
#include "velocity_scale.h"
struct { unsigned char TxChan, NoteNum, TxStatus; } NoteMem[128]; // Structure for the note memory
enum eIntNoteStatus { kStatusNone, kStatusVelOFF, kStatusVelON, kStatusNoteOff };
int Contact2[128];
int MidiTxVelocity;
int Transpose;

enum EEPROM_MemoryLocations
{
	EE_LOC_Skip_0 = 0,
	EE_LOC_JackType,
	EE_LOC_MidiType,
	EE_LOC_MidiChan,
	EE_LOC_MidiWhat,

	EE_LOC_Velocity, // Added for the Mojo scanboard modified firmware
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
const char MenuPages[][4] = { "TRS", "TYP", "CHN", "NUM", "VEL", "TSP", "MON" };
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
  0b000000000,  // X (null)
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
	if (++MenuPage > 6) MenuPage = 0; // Added 2 more pages for the Mojo Scanboard modified firmware
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
			EEPROM.write(EE_LOC_JackType, JackType);
			ConfigureTRS();
			break;

		case 1: // Midi Type
			if (++MidiType > MidiType_MAX) MidiType = 0;
			EEPROM.write(EE_LOC_MidiType, MidiType);
			break;

		case 2: // Midi Channel
			if (++MidiChan > 15) MidiChan = 0;
			EEPROM.write(EE_LOC_MidiChan, MidiChan);
			break;

		case 3: // Midi What
			if (++MidiWhat > 127) MidiWhat = 0;
			EEPROM.write(EE_LOC_MidiWhat, MidiWhat);
			break;

		case 4: // Velocity on or off
			//MidiTxVelocity = !MidiTxVelocity;
			if (++MidiTxVelocity > 1) MidiTxVelocity = 0;
			EEPROM.write(EE_LOC_Velocity, MidiTxVelocity);
			break;

		case 5: // Transpose (not stored into EEPROM)
			if (++Transpose > 12) Transpose = -12;
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

	case 4: // Velocity
		memcpy(DisplayText, MidiTxVelocity > 0 ? " ON" : "OFF", 3);
		break;

	case 5: // Transpose
		// Ouch! This is though!... In order to display this correctly, we must show this as a text
		sprintf(DisplayText, "%s%2d", Transpose < 0 ? "-" : " ", abs(Transpose));
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

// Called to read the TRS inputs and generate the MIDI event whenever the input has changed status
void ReadTRS()
{
	int MidiValue;

	switch (JackType)
	{
		///////////////////////////////////////////////////
	case e_Jack_ExpPed_WiperOnTip:
		MidiValue = ReadADC(A6);
		break;

		///////////////////////////////////////////////////
	case e_Jack_ExpPed_WiperOnRing:
		MidiValue = ReadADC(A7);
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
	if (MenuPage == 6) // Was page 4 in the original firmware
		DisplayNumber(MidiValue);
}

// This function is called only by the scanboard function to send note on or off messages
void SendNoteMessage(int byte1, int note, int velocity)
{
	int status = byte1 & 0xF0;
	int chn = byte1 & 0x0F;
	switch (status)
	{
	case 0x90: // Note ON
		MIDI.sendNoteOn(note, velocity, chn + 1);
		break;

	case 0x80: // Note OFF
		MIDI.sendNoteOff(note, velocity, chn + 1);
		break;
	}

#ifdef USBMIDI
	midiEventPacket_t Event;
	switch (status)
	{
	case 0x90: // Note ON
		Event = { 0x09, byte1, note, velocity };
		MidiUSB.sendMIDI(Event);
		MidiUSB.flush();
		break;

	case 0x80: // Note OFF
		Event = { 0x08, byte1, note, velocity };
		MidiUSB.sendMIDI(Event);
		MidiUSB.flush();
		break;
	}
#endif
}

/*  Here is a little secret: the scan board of the Mojo (also used in all Mojo models and in the GSi DMC-122)
 *  generates plain MIDI data, using distinct messages for each of the two bubble contacts and for each event
 *  when a contact is closed or opened. Rather than generating note events, it uses the PolyAftertouch event (0xA0)
 *  which is a 3-byte event just like note events. The main CPU then translates these messages in note events by
 *  applying velocity or not, according to the instrument settings, and deciding whether the note on event must be
 *  sent at the first contact (for better organ response) or at the second contact (needed for calculating the
 *  velocity information). This function is an excerpt of what the Mojo's main CPU does with the data coming
 *  from the scan board.
 *  This function also applies a two velocity curves, one only for compensating the lever action of the black
 *  keys, which are shorter and have the finger apply the force closer to the fulcrum; the second curve is
 *  applied on top of all for a better response with the TP-8o keyboard.
 *  One further feature is the use of the "note memory", which stores each note's channel and number when the
 *  note-on event is generated, and uses them when the note-off must be generated, so to avoid orphan note-on events
 *  in case the channel or the transposition is changed after a note is played and before it's released.
*/
void ProcessMojoScanboard(int chan, int note, int velocity)
{
	int Status = kStatusNone;

	// * ******************************************************* * /
	// Messages coming from the SCAN MATRIX v2.4 (MojoScan3):
	// KEY DOWN (contact closed):
	//    First contact   = 0xA0 0xA1 with value 127;
	//    Second contact  = 0xA2 0xA3 with velocity;
	//
	// KEY UP (contact opened):
	//    Second contact  = 0xA4 0xA5 with value 0
	//    First contact   = 0xA6 0xA7 with value 0

	// * ******************************************************* * /
	// FIRST THING: calculate note-off velocity based on the time
	// interval between second and first contact being opened

	switch (chan)
	{
		// if second contact is opened
	case 4:
		Contact2[note] = millis();
		return;

		// if first contact is opened
	case 6:
		if (Contact2[note] > 0) // check that a value was stored into the array for this note
		{
			int time_diff = millis() - Contact2[note];  // calc the time difference
			if (time_diff > 127) time_diff = 127;       // trim to 127
			velocity = 127 - time_diff;                 // assign to velocity byte
			Contact2[note] = 0;                         // reset array
			velocity = VelocityScale[velocity];         // Apply velocity scaling
		}
		else velocity = 0;                          // use velocity = 0 if the second contact was never reached

		// Set Status
		Status = kStatusNoteOff;
		break;

		// First contact
	case 0:
		Status = kStatusVelOFF;
		break;

		// Second contact
	case 2:
		// Find black keys and apply velocity compensation
		for (int p = 0; p < 25; p++) { if (note == BlackKey_note_num[p]) { velocity = BlackKeyVelocity[velocity]; break; } }

		// Apply velocity scaling
		velocity = VelocityScale[velocity];

		// Set Status
		Status = kStatusVelON;
		break;
	}


	// Generate outgoing messages using note memory for NOTE ON and NOTE OFF
	switch (Status)
	{
	case kStatusVelOFF: // First contact, no velocity
		// Store note and channel
		NoteMem[note].TxChan = MidiChan;
		NoteMem[note].NoteNum = note + Transpose;

		// Send if velocity feature is OFF
		if (MidiTxVelocity == 0)
		{
			SendNoteMessage(0x90 | NoteMem[note].TxChan, NoteMem[note].NoteNum, velocity);
			NoteMem[note].TxStatus = 1;
		}
		break;

	case kStatusVelON: // Second contact, velocity
		// Send if velocity feature is ON
		if (MidiTxVelocity == 1)
		{
			SendNoteMessage(0x90 | NoteMem[note].TxChan, NoteMem[note].NoteNum, velocity);
			NoteMem[note].TxStatus = 1;
		}
		break;

	case kStatusNoteOff:
		// Send note off event using note memory
		if (NoteMem[note].TxStatus)
		{
			SendNoteMessage(0x80 | NoteMem[note].TxChan, NoteMem[note].NoteNum, velocity);
			NoteMem[note].TxStatus = 0;
		}
		break;
	}
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
	MidiTxVelocity = EEPROM.read(EE_LOC_Velocity);
	ConfigureTRS();

	// These checks are actually needed in case the EEPROM is empty
	if (JackType >= e_JackType_MAX) JackType = 0;
	if (MidiType > MidiType_MAX) MidiType = MidiType_MAX;
	if (MidiChan > 15) MidiChan = 0;
	if (MidiWhat > 127) MidiWhat = 0;
	if (MidiTxVelocity > 1) MidiTxVelocity = 1;

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
	MIDI.begin(MIDI_CHANNEL_OMNI);
	MIDI.turnThruOff();
	Transpose = 0; // Always start with Transpose = 0
	// Reset note memory and velocity-off array
	for (int n = 0; n < 128; ++n)
	{
		NoteMem[n].TxChan = 0;
		NoteMem[n].NoteNum = 127;
		NoteMem[n].TxStatus = 0;
		Contact2[n] = 0;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The main running loop
void loop()
{
	// Process incoming Midi messages
	if (MIDI.read())
	{
		// Process only PolyAftertouch messages (see comments above)
		if (MIDI.getType() == 0xA0)
			// NOTE to self: remember that this MIDI library uses actual channel numbers 1~16 !!!
			ProcessMojoScanboard(MIDI.getChannel() - 1, MIDI.getData1(), MIDI.getData2());
	}

	// Read TRS input
	ReadTRS();

	// Run Timers
	DisplayTimer.run();
	PageTimer.run();
	ButtonTimer.run();
}


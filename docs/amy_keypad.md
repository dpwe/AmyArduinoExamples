# A super-cheap polyphonic analog synth emulator
dan.ellis@gmail.com 2025-11-06

This project uses a low-cost microcontroller, some cheap push-buttons, and the AMY library, to build a fully-playable emulation of a classic 1980s analog synthesizer.  Add a couple of potentiometers and you can be a real knob-twister!   It’s bare-bones, but it can be a starting point for all kinds of related projects.

## Hardware

Here’s the breadboard layout and corresponding schematic:

![Breadboard](https://raw.githubusercontent.com/dpwe/AmyArduinoExamples/main/docs/pics/synth_keypad_bb.png)
![Schematic](https://raw.githubusercontent.com/dpwe/AmyArduinoExamples/main/docs/pics/synth_keypad_schem.png)


The Pico connects to the PCM510x DAC module with the standard four I2S wires (SCK isn’t needed for DAC output, but we typically use it to make it easy to add an ADC later).

The keypad is a 6 x 4 matrix (only the first 3 of the 4 columns are currently used; also, the schematic shows the matrix transposed, i.e. each left-to-right row is a “column” from the software’s point of view).  The piano key layout breaks each octave into two “columns” of 6 semitones each, so the three rows shown cover one-and-a-half octaves.  The software provides for a 4th “column” of control keys (e.g. for changing patch) but I haven’t connected them here.  The keypad is read (with the Arduino Keypad library) by successively pulling each “column” line low, and seeing which “row” inputs are pulled low.  Each “row” input has a pullup resistor so if no key is pressed, it reads high.  Each key button has an associated diode; this is to avoid the “n-key rollover” problem, where pressing several keys can lead to other “ghost” keys.

The wiring of the key buttons on the breadboard is a little odd - I shared the “column” input of some keys by putting them in the same set of connectors on the breadboard, with the result that I take the outputs from alternate sides.  As a result, the “row” connections don’t come out in strict order.

Here’s a photo of my build. It’s not exactly what’s in the schematic - I have an ADC and MIDI input connected. Also, the switches I used (which I don’t particularly recommend) had only 2 pins, but kept jumping out of the breadboard, so I ended up soldering onto a strip board. Consequently, when I realized the diodes were pointing the wrong way, it wasn’t attractive to try to fix it.

![Build](https://raw.githubusercontent.com/dpwe/AmyArduinoExamples/main/docs/pics/synth_build.png)

## Software

The full sketch is in https://github.com/dpwe/AmyArduinoExamples/blob/main/AMY_keypad/AMY_keypad.ino.  Let’s start at the bottom to see what the overall plan is:
```C
// ---------------------
// Main setup
// ---------------------


void setup() {
 led_setup();
 keypad_setup();
 amy_setup();
 knobs_setup();
}


void loop() {
 // Calculate waveform & pass to DAC
 amy_update();
 // Scan keypad
 keypad_update();
 // Check knobs
 knobs_update();
 // Maybe flash LED
 led_update();
}
```

I’m using a `_setup()/_update()` pattern here to try to make the code somewhat modular and easy to follow.  We have for facilities, `led` (flashing the on-board LED at 2 Hz, to show the code is running), `keypad` (to read the keypad matrix and send corresponding note on/off and control events to AMY), `amy` (configuring and running the AMY synthesis engine), and `knobs` (servicing potentiometer knobs connected to the ADC inputs of the Pico and sending resulting AMY parameter updates).
Now we can go back to the top of the file and look at each of these components.  First is the AMY code.  `amy_setup()` configures and starts the AMY engine.  We’re using synth 1, which by default is setup as a Juno emulator.  We also configure the I2S DAC output, and the serial MIDI input (although it isn’t connected in the circuit above, we could add a TTL-converted MIDI input on GPIO 5).  We then start up the AMY engine and make it ready to generate sound.  

```C
// ---------------------
// AMY synth
// ---------------------
#include <AMY-Arduino.h>


const int SYNTH = 1;  // Send commands to synth 1, the Juno


void amy_setup() {
 amy_config_t amy_config = amy_default_config();
 amy_config.features.startup_bleep = 1;
 // Install the default_synths on synths (MIDI chans) 1, 2, and 10 (this is the default).
 amy_config.features.default_synths = 1;
 // Pins for i2s board
 amy_config.i2s_mclk = 7;
 amy_config.i2s_bclk = 8;
 amy_config.i2s_lrc = 9;
 amy_config.i2s_dout = 10;
 amy_config.i2s_din = 11;
 // If you want MIDI over UART (5-pin or 3-pin serial MIDI)
 amy_config.midi = AMY_MIDI_IS_UART;
 // Pins for UART MIDI
 amy_config.midi_in = 5;
 amy_start(amy_config);
 amy_live_start();
}
```

The main `loop()` function includes the call to `amy_update()`, which is part of the core AMY API and doesn’t need any additional code here.

### Keypad  code

This is perhaps the most complicated part, just because it’s fiddly to deal with so many inputs, and because the crucial calls to `amy_add_event()` (which actually cause the reactions to key presses) are buried in the middle.  As we saw in the schematic, we handle up to 30 keys as a matrix of 5 “columns” each consisting of 6 “rows” - meaning we only have to use 11 GPIO pins (one for each row and column), not 30 as in a simpler approach where each key gets its own GPIO.  Fortunately, this kind of matrix keypad is common enough that there’s a well-established Arduino library, Keypad, which presents us with an easy-to-use interface and handles all the scanning etc.  So the keypad initialization code is all in the constructor for the library-provided type `Keypad kpd`, which takes the matrix geometry (`ROWS` and `COLS`), the GPIO pins they are attached to, and a keymap based on a 2D array of chars, defining the single char that will be returned by each key in the matrix.  These are arbitrary, but for simplicity we arrange our keyboard to have one letter per semitone.  We also use digits for the “control keys”, which we include in the same matrix.  We keep our `keypad_setup()` to preserve our `setup/update` framework, but it’s empty.

```C
// -----------------
// Keypad input
// -----------------
#include <Keypad.h>  // Install "Keypad by Mark Stanley, Alexander Brevig" from the library manager.


const byte ROWS = 6;
const byte COLS = 5;
// Keys have to be described with single chars, so we assign successive letters
// to successive semitones, starting with 'A' for the low C, and 'Y' is the C
// two octaves higer.  The first two columns constitute the low octave, etc. 
// The digits 1-5 are used for control keys.
// Note the transposition of the rows/columns. 
char keys[ROWS][COLS] = {
 {'A', 'G', 'M', 'S', '1'},
 {'B', 'H', 'N', 'T', '2'},
 {'C', 'I', 'O', 'U', '3'},
 {'D', 'J', 'P', 'V', '4'},
 {'E', 'K', 'Q', 'W', '5'},
 {'F', 'L', 'R', 'X', 'Y'},
};
byte rowPins[ROWS] = {17, 18, 19, 20, 21, 22}; //connect to the row pinouts of the kpd
byte colPins[COLS] = {12, 13, 14, 15, 16}; //connect to the column pinouts of the kpd
// Keypad sends low values to each column in sequence, and reads back which rows have
// been pulled low.  For n-key rollover, you need diodes in series with each key, and
// they should be oriented to allow flow from row to column.  Each row pin needs a pull-up,
// I had to add these externally (on RP2350) even though Keypad initializes as INPUT_PULLUP.
Keypad kpd = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

void keypad_setup() {
 // Already handled by the constructor.
}
```

We break out the handling of control buttons into a separate for clarity.  `handle_control_code()` takes an integer corresponding to the digit in the key map (i.e. 1 to 5 for the keymap above), and does some system state change.  Here, we have the first pair of buttons step through the Juno patch numbers (0 to 127), and the second pair of buttons transpose the effective range of the keypad up and down an octave.

```C
// MIDI code for the lowest note.
int lowest_note = 48;  // C3
// Current patch number
int current_patch = 0;

void handle_control_code(int control_code) {
 // Special case for "control" keys returned from matrix.
 amy_event e = amy_default_event();
 e.synth = SYNTH;
 if (control_code == 1) {
   // Previous patch.
   current_patch = (current_patch + 127) % 128;
   e.patch_number = current_patch;
   amy_add_event(&e);
 }
 if (control_code == 2) {
   // Next patch.
   current_patch = (current_patch + 1) % 128;
   e.patch_number = current_patch;
   amy_add_event(&e);
 }
 if (control_code == 3) {
   // Down an octave
   if ((lowest_note - 12) >= 0)
     lowest_note -= 12;
 }
 if (control_code == 4) {
   // Up an octave
   if ((lowest_note + 12) < 128)
     lowest_note += 12;
 }
 // ... other controls
}
```

Finally, `keypad_update()` calls `kpd.getKeys()`, which scans the keyboard and sets up the `kpd.key[]` array of structs describing the state of each active key.  We hand off keys in the digit range to `handle_control_code()`, and otherwise it’s a note key and we calculate the MIDI note number by seeing which letter it is, then adding the `lowest_note` maintained by the control code handler.  Then, we send note-on (`velocity = 1.0f`) when a key is pressed, and note-off (`velocity = 0`) when it is released:

```C
void keypad_update() {
 amy_event e = amy_default_event();
 e.synth = SYNTH;
 // getKeys() fills kpd.key[ ] array with up-to 10 active keys.
 // Returns true if there are ANY active keys, else there's nothing to do.
 if (!kpd.getKeys()) return;
 for (int i=0; i<LIST_MAX; i++) {  // Scan the whole key list.
   if (!kpd.key[i].stateChanged)  continue;  // Skip keys with no state change.
   if ((kpd.key[i].kchar & '0') == '0') {
     // It's a digit, so a control code instead of a note
     if (kpd.key[i].kstate == PRESSED) {
       handle_control_code(kpd.key[i].kchar - '0');
     }
     // (We ignore releases of control keys).
     continue;
   }
   // It's a note key.
   e.midi_note = kpd.key[i].kchar - 'A' + lowest_note;
   switch (kpd.key[i].kstate) {
     case PRESSED:
       e.velocity = 1.0f;
       amy_add_event(&e);
       break;
     case RELEASED:
       e.velocity = 0;
       amy_add_event(&e);
       break;
   }
 }
}
```

### Knob code
The other source of `amy_events` is the potentiometer knobs optionally connected to the RP2350 ADC inputs (GP26, 27, and 28). The sketch sets up the first to control the base filter frequency, and the second to change the filter resonance.  Essentially, we just read the ADC inputs every loop, scale them, and set the corresponding AMY parameter (for the Juno synth, the VCF is all configured on osc 0, so we just send the command to `synth = 1` and it gets applied to `osc = 0` by default).  However, rather than flooding AMY with repeated calls to set the same values, we only update the parameters if we detect a change in the ADC input value.  We apply a small minimum change threshold `knob_slack` to further avoid excessive updates from ADC input noise.

```C
// ---------------------
// Knobs input
// Potentiometers connected to RP2040 ADC in (GPIO 26, 27, 28)
// can be used to control parameters...
// ---------------------
void knobs_setup () {
 // nothing to do
}

const int num_knobs = 2;
int last_knob_val[num_knobs] = {0, 0};
int knob_pin[num_knobs] = {A0, A1};
const int knob_slack = 4;  // Ignore changes smaller than this.

void knobs_update () {
 amy_event e;
 for(int knob = 0; knob < num_knobs; ++knob) {
   int new_val = analogRead(knob_pin[knob]);
   if (abs(new_val - last_knob_val[knob]) > knob_slack) {
     last_knob_val[knob] = new_val;
     // Send a command.
     e = amy_default_event();
     e.synth = SYNTH;
     float knob_fval = (float)new_val / 4095.0f;
     if (knob == 0) {
       // Knob 0 adjusts filter freq
       e.filter_freq_coefs[COEF_CONST] = 50.f * exp2f(knob_fval * 8.0f);
     } else if (knob == 1) {
       // Knob 1 adjusts filter resonance
       e.resonance = 8.0f * knob_fval;
     }
     amy_add_event(&e);
   }
 }
}
```

### LED heartbeat

The LED code is straightforward:

```C
// ---------------------
// Flashing LED
// ---------------------
void led_setup() {
#ifdef LED_BUILTIN
 pinMode(LED_BUILTIN, OUTPUT);
 digitalWrite(LED_BUILTIN, 1);
#endif
}

static long last_millis = 0;
static const long millis_interval = 250;
static bool led_state = 0;

void led_update() {
 // Flash on-board LED every 250ms
 int now_millis = millis();
 if ((now_millis - last_millis) > millis_interval) {
   last_millis = now_millis;
   led_state = !led_state;
#ifdef LED_BUILTIN
   digitalWrite(LED_BUILTIN, led_state);  // turn the LED on (HIGH is the voltage level)
#endif
 }
}
```

And that’s it!  Full emulation of a polyphonic analog synth with a $5 MCU and a few pushbutton switches.

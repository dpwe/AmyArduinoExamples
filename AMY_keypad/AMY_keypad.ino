// AMY_keypad

// Live synth play from a matrix keypad.
// dan.ellis@gmail.com 2025-11-05

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
    digitalWrite(LED_BUILTIN, led_state);  // toggle the LED.
#endif
  }
}

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
  // LED hearbeat
  led_update();
}

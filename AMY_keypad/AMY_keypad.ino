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
  amy_config.midi_out = 4;
  amy_config.midi_in = 5;
  amy_start(amy_config);
  amy_live_start();
}

// -----------------
// Keypad input
// -----------------
#include <Keypad.h>  // Install "Keypad by Mark Stanley, Alexander Brevig" from the library manager.

const byte ROWS = 5;
const byte COLS = 6;
// Keys have to be described with single chars, so
// 'c' is low C, 'C' is low C#, 'j' is C one octave up, through 'p' is high B.
char keys[ROWS][COLS] = {
{'c', 'C', 'd', 'D', 'e', 'f'},  // First "row" of keypad is C1, C#1, D1, D#1, E1, F1
{'F', 'g', 'G', 'h', 'H', 'i'},  // Row 2 is F#1, G1, G#1, A1, A#1, B1
{'j', 'J', 'k', 'K', 'l', 'm'},  // Row 3 is C2, C#2, D2, D#2, E2, F2
{'M', 'n', 'N', 'o', 'O', 'p'},  // Row 4 is F#2, G2, G#2, A2, A#2, B2
// We could add more rows for more notes.
// We also add special keys for interface control:
{'1', '2', '3', '4', '5', '6'}
};
byte rowPins[ROWS] = {12, 13, 14, 15, 16}; //connect to the row pinouts of the kpd
byte colPins[COLS] = {17, 18, 19, 20, 21, 22}; //connect to the column pinouts of the kpd
// Keypad sends low values to each column in sequence, and reads back which rows have 
// been pulled low.  For n-key rollover, you need diodes in series with each key, and 
// they should be oriented to allow flow from row to column.  Each row pin needs a pull-up, 
// I had to add these externally (on RP2350) even though Keypad initializes as INPUT_PULLUP.
Keypad kpd = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

void keypad_setup() {
  // Already handled by constructor.
}

int kchar_to_midi(char kchar) {
  // Convert a single-char key indicator (from the keys[] table) to a midi note.
  char basenote = 'A';
  int is_sharp = 1;
  if (kchar >= 'a') {
    // White note
    basenote = 'a';
    is_sharp = 0;
  }
  int octave = (kchar - basenote) / 7;
  int degree = kchar - basenote - 7 * octave;
  // 2 semis per degree, *except* discount 1 for B>C, and another one for E>F.
  return 45 + 12 * octave + 2 * degree - (degree >= 2) - (degree >= 5) + is_sharp;
}

int current_patch = 0;

bool handle_control_key(char kchar) {
  // Special case for "control" keys returned from matrix.
  if (kchar < '1' || kchar > '6')  return false;  // false = not a UI control key, play it as a note.
  if (kchar == '1') {
    // Advance patch.
    current_patch = (current_patch + 1) % 128;
  }
  if (kchar == '2') {
    // Previous patch.
    current_patch = (current_patch + 127) % 128;
  }
  // ... other controls
  amy_event e = amy_default_event();
  e.synth = SYNTH;
  e.patch_number = current_patch;
  amy_add_event(&e);
  return true;  // We handled this key, don't try to make it a note event.
}

void keypad_update() {
  amy_event e = amy_default_event();
  e.synth = SYNTH;
  // getKeys() fills kpd.key[ ] array with up-to 10 active keys.
  // Returns true if there are ANY active keys.
  if (kpd.getKeys()) {
    for (int i=0; i<LIST_MAX; i++) {  // Scan the whole key list.
      if (kpd.key[i].stateChanged) {  // Only find keys that have changed state.
        if (!handle_control_key(kpd.key[i].kchar)) {
          e.midi_note = kchar_to_midi(kpd.key[i].kchar);
          switch (kpd.key[i].kstate) {  // Report active key state : IDLE, PRESSED, HOLD, or RELEASED
            case PRESSED:
              e.velocity = 1.0;
              amy_add_event(&e);
              break;
            case RELEASED:
              e.velocity = 0;
              amy_add_event(&e);
              break;
          }
        }
      }
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
    digitalWrite(LED_BUILTIN, led_state);  // turn the LED on (HIGH is the voltage level)
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
  // Maybe flash LED
  led_update();
}

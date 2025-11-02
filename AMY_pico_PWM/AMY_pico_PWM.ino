#include <AMY-Arduino.h>

// AMY_pico_pwm
//
// Runs AMY using arduino-pico's PWM audio output.
// This version responds to serial MIDI input.
// dpwe 2025-10-26

extern "C" {
  extern void on_pico_uart_rx(void);

  extern void example_sequencer_drums_synth(uint32_t start);
}

#include <PWMAudio.h>
PWMAudio pwm(0, true);  // PWM Stereo out on pins 0 and 1.

void setup() {
  amy_config_t amy_config = amy_default_config();
  amy_config.features.startup_bleep = 1;
  amy_config.features.default_synths = 1;

  amy_config.midi = AMY_MIDI_IS_UART;
  // Pins for UART MIDI
  amy_config.midi_in = 5;

  amy_start(amy_config);

  // Setup PWM
  pwm.setBuffers(4, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t) / sizeof(int32_t));
  pwm.begin(44100);

  // Set a drum loop going, as an example.
  example_sequencer_drums_synth(2000);
}

void loop() {
  on_pico_uart_rx();  // Handle MIDI input
  int16_t *block = amy_simple_fill_buffer();  // AMY calculates next block of audio
  // Write audio to PWM output
  size_t wrote = 0;
  do {
    wrote = pwm.write((const uint8_t *)block, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
  } while (wrote == 0);
}

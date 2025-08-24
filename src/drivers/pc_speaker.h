#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>

//PC Speaker ports
#define SPEAKER_PORT        0x61
#define PIT_CHANNEL_2       0x42
#define PIT_COMMAND         0x43

//PIT command for channel 2
#define PIT_SPEAKER_CMD     0xB6

//musical note frequencies 
#define NOTE_C4     262
#define NOTE_CS4    277
#define NOTE_D4     294
#define NOTE_DS4    311
#define NOTE_E4     330
#define NOTE_F4     349
#define NOTE_FS4    370
#define NOTE_G4     392
#define NOTE_GS4    415
#define NOTE_A4     440
#define NOTE_AS4    466
#define NOTE_B4     494
#define NOTE_C5     523
#define NOTE_CS5    554
#define NOTE_D5     587
#define NOTE_DS5    622
#define NOTE_E5     659
#define NOTE_F5     698
#define NOTE_FS5    740
#define NOTE_G5     784
#define NOTE_GS5    831
#define NOTE_A5     880
#define NOTE_AS5    932
#define NOTE_B5     988

#define FREQ_BEEP   1000
#define FREQ_ERROR  200
#define FREQ_SUCCESS 800

//function declarations
void speaker_init(void);
void speaker_play_freq(uint32_t frequency);
void speaker_stop(void);
void speaker_beep(uint32_t frequency, uint32_t duration_ms);
void speaker_play_note(uint32_t note, uint32_t duration_ms);

//macros
#define BEEP() speaker_beep(FREQ_BEEP, 100)
#define ERROR_SOUND() speaker_beep(FREQ_ERROR, 300)
#define SUCCESS_SOUND() speaker_beep(FREQ_SUCCESS, 150)

#endif

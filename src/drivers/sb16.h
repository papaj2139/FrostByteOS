#ifndef SB16_H
#define SB16_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


//register the SB16 device as /dev/sb16
int sb16_register_device(void);

//control/query API for procfs-based control
uint16_t sb16_get_rate(void);
int      sb16_set_rate(uint16_t rate);
void     sb16_speaker_on(void);
void     sb16_speaker_off(void);
int      sb16_is_speaker_on(void);
uint8_t  sb16_get_irq(void);
uint8_t  sb16_get_dma8(void);
int      sb16_set_volume(int vol01_100);
int      sb16_get_volume(void);
void     sb16_set_mute(int on);
int      sb16_get_mute(void);
void     sb16_pause(void);
void     sb16_resume(void);
void     sb16_stop(void);
int      sb16_is_playing(void);
int      sb16_is_paused(void);
uint32_t sb16_get_queued(void);
uint32_t sb16_get_underruns(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef WEB_PSX_MIXER_H
#define WEB_PSX_MIXER_H

/* Native audio backend for the web build. libspu/libsnd talk to this
 * instead of an emulated SPU register file: sample data uploaded by
 * SpuWrite is ADPCM-decoded to PCM once (cached per start address) and
 * 24 software voices mix those buffers with a closed-form ADSR, the
 * PsyQ reverb presets on a private delay buffer, and the XA/CD stream.
 *
 * Units deliberately match the PsyQ API values callers already hold:
 * volumes are signed 15-bit (0x3FFF ~ unity/2, bit15 = sweep mode),
 * pitch is 0x1000 = 44.1 kHz, sample addresses are byte offsets into
 * the 512 KiB sound-buffer address space handed out by SpuMalloc. */

#include <stdint.h>

#define MIX_VOICES   24
#define MIX_MEM_SIZE 0x80000

/* sample memory (the SpuMalloc address space) */
void mix_mem_write(uint32_t addr, const void *src, uint32_t len);
void mix_mem_read(uint32_t addr, void *dst, uint32_t len);
void mix_sample_prepare(uint32_t addr); /* decode+cache now */

/* voices */
void mix_voice_volume(int v, int16_t l, int16_t r);
void mix_voice_pitch(int v, uint16_t pitch);
void mix_voice_sample(int v, uint32_t addr);
void mix_voice_adsr(int v, uint16_t adsr1, uint16_t adsr2);
uint16_t mix_voice_get_adsr1(int v);
uint16_t mix_voice_get_adsr2(int v);
int16_t mix_voice_env(int v);
void mix_key_on(uint32_t mask);
void mix_key_off(uint32_t mask);
uint32_t mix_end_mask(void); /* voices that reached their end block */
void mix_noise_voices(uint32_t mask);
void mix_noise_clock(int clk);

/* master / CD / reverb */
void mix_master_volume(int16_t l, int16_t r);
int16_t mix_get_master_l(void);
int16_t mix_get_master_r(void);
void mix_cd_volume(int16_t l, int16_t r);
void mix_cd_input(const int16_t *stereo, int frames);
void mix_reverb_voices(uint32_t mask);
void mix_reverb_mode(int mode); /* SPU_REV_MODE_* preset */
void mix_reverb_depth(int16_t l, int16_t r);
void mix_reverb_enable(int on);
void mix_reverb_clear(void);

void mix_render(int16_t *out, int frames);
void mix_reset(void);

#endif

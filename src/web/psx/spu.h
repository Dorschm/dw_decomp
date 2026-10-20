#ifndef WEB_PSX_SPU_H
#define WEB_PSX_SPU_H

#include <stdint.h>

#define SPU_RAM_SIZE 0x80000

/* Register-file interface: addr is the raw 0x1F801C00-based offset. */
void spu_write_reg16(uint32_t addr, uint16_t val);
uint16_t spu_read_reg16(uint32_t addr);

/* Direct SPU-RAM data transfer (what SpuWrite/_spu_t mode does). */
void spu_dma_write(uint32_t spu_addr, const void *src, uint32_t len);
void spu_dma_read(uint32_t spu_addr, void *dst, uint32_t len);

extern uint8_t spu_ram[SPU_RAM_SIZE];

/* Render `frames` stereo frames at 44100 Hz into out[2*frames]. */
void spu_render(int16_t *out, int frames);

void spu_reset(void);

/* diagnostics for the harness */
extern unsigned long spu_kon_events;
extern unsigned long spu_samples_rendered;

#endif

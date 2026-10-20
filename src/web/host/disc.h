#ifndef WEB_DISC_H
#define WEB_DISC_H

#include <stdint.h>

/* Suspend (Asyncify) until a disc image is attached via Module.disc.
 * Returns 0 when Module.disc.readUser is available, -1 otherwise. */
int disc_wait_ready(void);

/* Copy `count` 2048-byte user-data blocks starting at sector `lba`
 * (MODE2 Form1 user offset 24, MODE1 offset 16) into dst.
 * Returns 0 on success, -1 on failure. */
int disc_read_user(uint32_t lba, uint32_t count, void *dst);

/* Copy `count` raw 2352-byte sectors starting at `lba` into dst.
 * Returns 0 on success, -1 on failure. */
int disc_read_raw(uint32_t lba, uint32_t count, void *dst);

#endif

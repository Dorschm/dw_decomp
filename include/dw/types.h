#ifndef DW_TYPES_H
#define DW_TYPES_H

#ifdef __MWERKS__

#ifndef NULL
#define NULL 0
#endif

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

typedef int intptr_t;
typedef unsigned int uintptr_t;

#else

#include <stddef.h>
#include <stdint.h>

#endif

#ifdef PLATFORM_WEB
/* Emulated 2 MiB PS1 main RAM (defined in src/web/psx/data_shim.c).
 * All hard-coded 0x80xxxxxx address literals resolve into it so that
 * the retail overlay buffers, scratch areas and ordering tables share
 * one address space exactly like on hardware. */
extern unsigned char PSX_RAM[0x200000];
#define PSX_PTR(addr) ((void *)(PSX_RAM + ((addr) & 0x1FFFFF)))
/* web debug trace to the browser console; compiles to nothing on PS1 */
extern void emscripten_console_logf(const char *fmt, ...);
#define WEB_TRACE(...) emscripten_console_logf(__VA_ARGS__)
#else
#define PSX_PTR(addr) ((void *)(addr))
#define WEB_TRACE(...) ((void)0)
#endif

#endif

#ifndef TK_ROMS_EMBEDDED_H
#define TK_ROMS_EMBEDDED_H
#include <stdint.h>
/* ROMs de 16 KB embutidas no executavel (ver roms/). */
extern const uint8_t rom_tk95[16384];      /* CRC32 17368e07 */
extern const uint8_t rom_tk90x[16384];     /* CRC32 3e785f6f */
extern const uint8_t rom_tk90x_v1[16384];  /* CRC32 d1b2f352 */
#endif

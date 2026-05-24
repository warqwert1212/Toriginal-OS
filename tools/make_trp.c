#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    const char *path = "iso/boot/test_real.trp";
    FILE *f = fopen(path, "wb");
    if (!f) return 1;

    /* Header: 'TRPK' */
    fwrite("TRPK", 1, 4, f);
    /* Entry offset: 8 (payload starts after header+4) */
    uint32_t entry = 8;
    fwrite(&entry, 4, 1, f);

    /* Payload: VM marker 'VM' */
    fwrite("VM", 1, 2, f);
    /* VM payload: PRINT_STR opcode 0x01 */
    uint8_t op = 0x01;
    fwrite(&op, 1, 1, f);
    /* length (u16 little endian) */
    const char *msg = "Hello from REAL TRP!";
    uint16_t len = (uint16_t)strlen(msg);
    fwrite(&len, 2, 1, f);
    fwrite(msg, 1, len, f);
    /* HALT opcode */
    uint8_t halt = 0xFF;
    fwrite(&halt, 1, 1, f);

    fclose(f);
    printf("Wrote %s\n", path);
    return 0;
}

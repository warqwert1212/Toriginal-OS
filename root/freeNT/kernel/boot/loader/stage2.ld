/* stage2.ld — links stage2.s + stage2_pm.s into one flat binary.
 *
 * Loaded at linear 0x9000 by stage1 (segment 0x0900, offset 0), but
 * every address in stage2.s's real-mode code is used as a segment
 * offset (DS=ES=SS=0x0900 throughout, see stage2.s's entry code) -
 * so this links at address 0, not 0x9000. gdt32_pointer explicitly
 * adds the 0x9000 linear base back in for the one place (GDTR) that
 * genuinely needs a linear address instead of an offset. */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_stage2_start)

SECTIONS
{
    . = 0x0000;
    .text : {
        *(.text)
    }
}

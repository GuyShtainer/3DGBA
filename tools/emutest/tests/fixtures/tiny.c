/* tiny.c — source of tiny.elf, the checked-in ELF fixture for test_elf_extract.py
 * (SPEC-harness H6.2: "a tiny checked-in ELF fixture (built once from a 5-line .c in the
 * slice)"). Built 2026-08-08 with devkitARM GCC 15.2.0:
 *   /opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc -nostdlib -nostartfiles \
 *     -Ttext=0x100000 -o tiny.elf tiny.c
 * Known contents: .text (code of _start), .rodata "RODATA-FIXTURE!", .data DE AD BE EF
 * 01 02 03 04, .bss 64 zero bytes (NOT file-backed -> elf_bytes() must return None). */
const char fix_ro[16] = "RODATA-FIXTURE!";
char fix_dw[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
char fix_zz[64];
int _start(void) { return fix_ro[0] + fix_dw[0] + fix_zz[0]; }

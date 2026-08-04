// gbatext.c — the Gen-3 charmap table (SPEC-avatar A4.2). Contract: gbatext.h.
#include "gbatext.h"

// ---- THE TABLE (A4.2.3: "the table IS the code") --------------------------------------------
// A static const char* const [256] of short UTF-8 strings, sized 256 EXACTLY so no index can
// escape it (the parameter is a uint8_t, so the bound is structural rather than checked) and so
// the two gendered signs and plain ASCII share one lookup with no runtime range arithmetic.
//
// PROVENANCE, per row, honestly:
//
//  * 0xBB..0xD4 'A'..'Z' and 0xD5..0xEE 'a'..'z' and 0xFF = terminator carry an IN-REPO WITNESS.
//    celiolink_payloads.h:98-100 / :130-133 ship Celio's canned demo identity as the name bytes
//    {0xC8, 0xDD, 0xE0, 0xE7, 0xFF}. Under this table:
//        0xC8 - 0xBB = 13 -> 'N'      0xDD - 0xD5 =  8 -> 'i'
//        0xE0 - 0xD5 = 11 -> 'l'      0xE7 - 0xD5 = 18 -> 's'      0xFF -> terminator
//    => "Nils", which IS Celio-Link's demo trainer name. One four-character round trip pins both
//    letter ranges and the terminator simultaneously, and test_presence TEST 34 asserts it.
//
//  * 0x00 space and 0xA1..0xAA '0'..'9' are corroborated in-repo a second way: presence.c's
//    presence_name_ascii (SPEC-data D6.3.1, shipped in slice M0 and host-tested by TEST 17/24)
//    was transcribed from pret's charmap.txt independently, and TEST 34 cross-checks the two
//    decoders over ALL 256 byte values. Two independent transcriptions agreeing is not proof, but
//    it is what is available and it is checked mechanically rather than by eye.
//
//  * *** VERIFY-ON-HW-PENDING: the punctuation block 0xAB..0xBA. *** It has NO in-repo witness.
//    It is transcribed from the decomp's published charmap.txt, which is NOT readable on this
//    machine (there is no pret checkout — /tmp/pret is absent as of 2026-08-04), so per the
//    standing house rule ("flag, don't silently ship a guess") it is marked here rather than
//    presented as verified. The blast radius is bounded BY CONSTRUCTION: an unmapped or wrong
//    entry becomes one cosmetic glyph inside a name, never an out-of-range read (the table is
//    256 wide) and never a buffer overrun (the writer is cap-driven). A player name is
//    overwhelmingly letters, which are the rows that DO carry the anchor above.
//    A7.2 item H11 settles it: open each game's own trainer card and compare, name for name.
//
// Everything not named below is "?" — see A4.2.2: a wrong entry costs a '?', and a '?' is
// cosmetic where a bad index would not be.
#define X6 "?", "?", "?", "?", "?", "?"
#define X8 X6, "?", "?"
#define X16 X8, X8
#define X32 X16, X16
#define X64 X32, X32

static const char* const GBATEXT[256] = {
	/* 0x00 */ " ",
	/* 0x01..0x0F */ X8, X6, "?",
	/* 0x10..0x4F */ X64,
	/* 0x50..0x8F */ X64,
	/* 0x90..0xA0 */ X16, "?",
	/* 0xA1..0xAA */ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	/* 0xAB..0xAE */ "!", "?", ".", "-",
	/* 0xAF..0xB3 */ "?", "?", "?", "?", "?",
	/* 0xB4 */ "'",
	/* 0xB5 */ "\xE2\x99\x82",   // U+2642 MALE SIGN   (UTF-8 E2 99 82)
	/* 0xB6 */ "\xE2\x99\x80",   // U+2640 FEMALE SIGN (UTF-8 E2 99 80)
	/* 0xB7 */ "?",
	/* 0xB8 */ ",",
	/* 0xB9 */ "?",
	/* 0xBA */ "/",
	/* 0xBB..0xD4 */ "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
	                 "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
	/* 0xD5..0xEE */ "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
	                 "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
	/* 0xEF..0xFE */ X16,
	/* 0xFF */ 0                 // terminator (pret encodes it as '$', charmap.txt:156)
};
#undef X64
#undef X32
#undef X16
#undef X8
#undef X6

const char* gbatext_glyph(uint8_t b) {
	return GBATEXT[b];           // b is a uint8_t: the index is in range by construction
}

int gbatext_decode(const uint8_t* src, int n, char* out, int cap) {
	if (!out || cap <= 0) return 0;
	out[0] = '\0';
	if (!src || n <= 0) return 0;

	int w = 0;                                   // bytes written so far (never includes the NUL)
	for (int i = 0; i < n; i++) {
		const char* g = GBATEXT[src[i]];
		if (!g) break;                           // 0xFF terminator: the name ends HERE, not at n
		// Length of this glyph, without <string.h>: every entry is 1..3 bytes.
		int gl = 0; while (g[gl]) gl++;
		// A glyph that does not fit WHOLE is dropped rather than truncated: half a UTF-8 sequence
		// is not text, and a caller that measures it would get a width for a byte the font cannot
		// draw. The NUL's byte is reserved before the fit test, so out is always terminated.
		if (w + gl + 1 > cap) break;
		for (int k = 0; k < gl; k++) out[w + k] = g[k];
		w += gl;
	}
	out[w] = '\0';
	return w;                                    // == strlen(out); see gbatext.h's deviation note
}

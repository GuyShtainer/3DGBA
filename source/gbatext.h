// gbatext.h — Gen-3 GBA character map -> UTF-8, the codebase's FIRST decoder.
// ============================================================================================
// Spec: docs/phase15-presence/SPEC-avatar.md A4.1 / A4.2 (slice M3). PURE C (CLAUDE.md rule #4):
// <stdint.h> only — no <3ds.h>, no citro*, no globals — so test/host/test_presence.c compiles it
// on the PC and the table is proved there, not on hardware.
//
// WHY A NEW MODULE AND NOT A LINE IN gamestate.c OR celiolink_payloads.h. A grep of source/ and
// test/ for "charmap|decode_name|to_ascii" finds the GBA character set PRODUCED and COPIED in
// three places — celiolink_payloads.h:35 (`name[8]`, "GBA-charset, 0xFF-terminated"),
// celiolink_payloads.h:98-102 (cl_make_trainer_card writes literal charmap bytes) and
// celiolink.c:734 — and DECODED in none. gamestate.c is game-RAM-shaped and links against
// gbacore; celiolink_payloads.h is the wire format. This is a pure text utility, so it lives on
// its own and the presence card, the nameplate and any future trainer-card viewer share ONE table.
//
// RELATIONSHIP TO presence_name_ascii (presence.c, shipped in slice M0, SPEC-data D6.3.1). That
// one is deliberately kept: it is the 9-byte, ASCII-only, allocation-free decoder the M1 HUD line
// needs, and its 8-char output bound is a property callers rely on. This one is the presentation
// decoder — UTF-8, so the two gendered glyphs survive, and cap-driven, so it can write into a card
// panel's buffer. They are cross-checked against each other for EVERY one of the 256 byte values
// by test_presence TEST 34, so the two tables cannot silently drift on the rows they share.
#pragma once
#include <stdint.h>

// Decode `n` GBA-charmap bytes into a NUL-terminated UTF-8 C string.
//
//   src   the raw bytes as they sit in game RAM / in a PeerPresence record (NOT pre-terminated:
//         0xFF ends the string, and a name that fills its field has no terminator at all).
//   n     how many bytes of `src` may be read. Nothing outside [src, src+n) is touched.
//   out   receives the UTF-8 text. ALWAYS NUL-terminated when cap >= 1.
//   cap   sizeof(out). A glyph that would not fit whole (its bytes plus the NUL) is dropped
//         rather than truncated mid-sequence — a half-written UTF-8 sequence is not text.
//
// Returns the number of BYTES written, excluding the NUL — i.e. exactly strlen(out).
//
//   *** DEVIATION, stated once: A4.1's prose says "the number of characters written". For a
//   multi-byte UTF-8 glyph "characters" is ambiguous (glyphs? bytes?), and every caller here does
//   pointer/length arithmetic, so the byte count — which is testable as strlen-equivalence — is
//   what is returned. TEST 34 asserts `gbatext_decode(...) == (int)strlen(out)` exhaustively. ***
//
// A4.1's sizing rule holds with room to spare: `out` needs >= 3*n + 1 bytes for any input (the
// widest entry in the table is 3 bytes). NEVER indexes outside the 256-entry table (it is indexed
// by a uint8_t, so it cannot) and NEVER writes past `cap`.
int gbatext_decode(const uint8_t* src, int n, char* out, int cap);

// The single-glyph lookup the table is built on, exposed so a caller can size a field and so the
// host suite can prove the table row by row. Returns a NUL-terminated UTF-8 string of 1..3 bytes
// for every input except the terminator, for which it returns NULL.
//   0xFF -> NULL (terminator)      anything unmapped -> "?"
const char* gbatext_glyph(uint8_t b);

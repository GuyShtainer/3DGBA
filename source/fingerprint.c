// fingerprint.c — D6 link-surface fingerprint: canonical bytes, two-lane FNV-1a, field diff,
// netlog formatter. Pure C (see fingerprint.h's header note); dual-compiled by
// test/test_celiolink.c (TEST 16 golden / TEST 17 mutation) and by test/host/
// test_netlink_reliability.c (netlink.c calls the formatter).
// Design source: docs/kb/external/gen1-link.md (Fingerprint/Handshake), gen1-parity.md §9;
// spec: docs/phase13-diagnostics/SPEC-suite-hardening.md §D6.
#include <string.h>
#include <stdio.h>    // snprintf only (libc, not a platform header — the pure-C rule is about
                      // libctru/citro/mGBA leakage; diag.c sets the same precedent)
#include "fingerprint.h"

// The struct layout IS the wire format — if this ever fires, the serialization changed and every
// golden + every peer's digest moved with it (see CL_PROTO_REV's bless discipline).
_Static_assert(sizeof(DgbaFprint) == DGBA_FPRINT_BYTES, "DgbaFprint must stay 8 packed bytes");

void dgba_fprint_fill(DgbaFprint* f, const char* gameCode4, uint8_t gameRev,
                      uint8_t clProtoRev, uint8_t netProto)
{
	if (!f) return;
	memset(f, 0, sizeof *f);
	if (gameCode4)
		for (int i = 0; i < 4 && gameCode4[i]; i++) f->gameCode[i] = (uint8_t)gameCode4[i];
	f->gameRev    = gameRev;
	f->clProtoRev = clProtoRev;
	f->netProto   = netProto;
	f->modeFlags  = (uint8_t)(DGBA_NET_EXP_DEFAULT & 0x0F);   // bit 7 reserved 0 (SPEC D6.1 field 5)
}

// FNV-1a, 32-bit: h = (h ^ byte) * 16777619, wrapping. Two lanes with DIFFERENT offset bases are
// folded into one u64 — gen1's exact construction (Fingerprint.lua:21-63). Lane B's basis is the
// documented FNV offset XOR 0xDEADBEEF (a second, written-down base; the point is only that the
// two lanes start in different states so the fold carries 64 bits of signal, not 32 duplicated).
#define FNV_PRIME    16777619u
#define FNV_BASIS_A  2166136261u                  // 0x811C9DC5
#define FNV_BASIS_B  (0x811C9DC5u ^ 0xDEADBEEFu)  // the FNV basis XOR a second written-down base

uint32_t dgba_fprint_lane(const DgbaFprint* f, int lane)
{
	if (!f) return 0;
	const uint8_t* b = (const uint8_t*)f;         // the packed struct bytes, in declared order
	uint32_t h = (lane == 0) ? FNV_BASIS_A : FNV_BASIS_B;
	for (int i = 0; i < DGBA_FPRINT_BYTES; i++) {
		h ^= (uint32_t)b[i];
		h *= FNV_PRIME;                            // u32 wrap-around is the algorithm
	}
	return h;
}

uint64_t dgba_fprint_hash(const DgbaFprint* f)
{
	return ((uint64_t)dgba_fprint_lane(f, 0) << 32) | (uint64_t)dgba_fprint_lane(f, 1);
}

// --- diff -----------------------------------------------------------------------------------
// Append one ",name(x!=y)" token, always NUL-terminated, never past outMax. Returns nothing: a
// truncated diff is still honest (it named the first differing fields) and the hashes in the same
// line remain the authority.
static void diff_append(char* out, int outMax, int* used, const char* tok)
{
	if (!out || outMax <= 0) return;
	int room = outMax - 1 - *used;                 // keep one byte for the NUL
	if (room <= 0) return;
	int n = (int)strlen(tok);
	if (n > room) n = room;
	memcpy(out + *used, tok, (size_t)n);
	*used += n;
	out[*used] = '\0';
}

// A 4cc rendered printable ('?' for anything a netlog/HUD line could not survive).
static void code4(const uint8_t src[4], char out[5])
{
	for (int i = 0; i < 4; i++) {
		unsigned char c = src[i];
		out[i] = (c >= 0x20 && c < 0x7F) ? (char)c : (c == 0 ? '-' : '?');
	}
	out[4] = '\0';
}

int dgba_fprint_diff(const DgbaFprint* a, const DgbaFprint* b, char* out, int outMax)
{
	if (out && outMax > 0) out[0] = '\0';
	if (!a || !b) return 0;
	int used = 0, n = 0;
	char tok[64];

	if (memcmp(a->gameCode, b->gameCode, 4) != 0) {
		char ca[5], cb[5]; code4(a->gameCode, ca); code4(b->gameCode, cb);
		snprintf(tok, sizeof tok, "%sgameCode(%s!=%s)", n ? "," : "", ca, cb);
		diff_append(out, outMax, &used, tok); n++;
	}
	if (a->gameRev != b->gameRev) {
		snprintf(tok, sizeof tok, "%sgameRev(%u!=%u)", n ? "," : "", a->gameRev, b->gameRev);
		diff_append(out, outMax, &used, tok); n++;
	}
	if (a->clProtoRev != b->clProtoRev) {
		snprintf(tok, sizeof tok, "%sclProtoRev(%u!=%u)", n ? "," : "", a->clProtoRev, b->clProtoRev);
		diff_append(out, outMax, &used, tok); n++;
	}
	if (a->netProto != b->netProto) {
		snprintf(tok, sizeof tok, "%snetProto(%u!=%u)", n ? "," : "", a->netProto, b->netProto);
		diff_append(out, outMax, &used, tok); n++;
	}
	if (a->modeFlags != b->modeFlags) {
		snprintf(tok, sizeof tok, "%smodeFlags(%u!=%u)", n ? "," : "", a->modeFlags, b->modeFlags);
		diff_append(out, outMax, &used, tok); n++;
	}
	return n;
}

// --- netlog line ----------------------------------------------------------------------------
// "BPEE r1 cl1 np1 m5 h=1A2B3C4D5E6F7788" — one side's surface. %08lX pairs so the format is
// identical on devkitARM newlib and the PC host (no %llX / PRIx64 divergence).
static int side(char* out, int outMax, const DgbaFprint* f, uint64_t h)
{
	char c[5]; code4(f->gameCode, c);
	return snprintf(out, (size_t)outMax, "%s r%u cl%u np%u m%u h=%08lX%08lX",
	                c, f->gameRev, f->clProtoRev, f->netProto, f->modeFlags,
	                (unsigned long)(uint32_t)(h >> 32), (unsigned long)(uint32_t)h);
}

int dgba_fprint_format(const DgbaFprint* local, uint64_t localHash,
                       const DgbaFprint* peer, uint64_t peerHash, int peerValid,
                       char* out, int outMax)
{
	if (!out || outMax <= 0 || !local) return -1;
	char ls[64]; side(ls, (int)sizeof ls, local, localHash);

	if (!peerValid || !peer) {
		// UNKNOWN, never "match" — the peer's surface simply never arrived (D6.5).
		snprintf(out, (size_t)outMax, "# fprint local=%s peer=none verdict=unknown", ls);
		return (int)strlen(out);
	}
	char ps[64]; side(ps, (int)sizeof ps, peer, peerHash);
	char d[192];
	int nd = dgba_fprint_diff(local, peer, d, (int)sizeof d);
	if (nd == 0) snprintf(out, (size_t)outMax, "# fprint local=%s peer=%s verdict=MATCH", ls, ps);
	else         snprintf(out, (size_t)outMax, "# fprint local=%s peer=%s verdict=DIFF:%s", ls, ps, d);
	return (int)strlen(out);
}

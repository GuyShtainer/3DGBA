// fingerprint.h — D6 LINK-SURFACE FINGERPRINT: "can these two consoles link at all?", answered
// BEFORE the link starts instead of by a mystery hardware run.
// ============================================================================================
// Phase-13-prep slice D6 (docs/phase13-diagnostics/PHASE.md scope row D6,
// docs/phase13-diagnostics/SPEC-suite-hardening.md §D6). PURE C — <stdint.h>/<string.h>/<stdio.h>
// only, no libctru, no mGBA, no celiolink (CLAUDE.md rule #4) — so every byte of it dual-compiles
// on the PC and is pinned by TEST 16/17 in test/test_celiolink.c.
//
// THE BORROWED DESIGN (docs/kb/external/gen1-link.md "Fingerprint", gen1-parity.md §9):
// gen1recomp hashes a LINK SURFACE — "only the registries that change what a lockstep turn or a
// rebuilt trade mon looks like" (Handshake.lua:20-24) — and ships the digest in the hello, so two
// builds that cannot interoperate say so on a screen instead of desyncing mid-battle.
//
// THE GOVERNING LESSON — over-coverage is the failure mode (gen1recomp issue #511,
// Fingerprint.lua:131-137 via gen1-link.md): hashing `catchRate` split Red/Blue from Yellow even
// though the real cable links them freely, because no link mode ever READS catchRate. Fingerprint
// over-coverage manifests as FALSE incompatibility between actually-compatible peers.
//   => "Hash what the protocol reads, not the ROM."
// So our surface is 8 bytes and there is deliberately NO ROM CRC in it (that would split FR rev0
// from rev1, and EM from FR, which trade perfectly on real hardware — SPEC §D6.1 exclusion list).
//
// The struct below IS the wire format: its packed bytes, in declared order, are the canonical
// serialization that gets hashed and that crosses the radio as PK_FPRINT's opaque surface[8].
// Fields are APPENDED ONLY, never reordered (Handshake.lua:1-8 additive-versioning discipline:
// "absence of a field decodes to the legacy behavior"). Any change here is a PARITY CHANGE:
// re-pin the goldens in TEST 16 and log it (see CL_PROTO_REV's comment in celiolink.h).
// ============================================================================================
#pragma once
#include <stdint.h>

// The link-strategy default this build enters a wireless session with — state F, Celio local
// termination (gbacore.c's s_netExp; A-E remain the KEY_Y bisection fallbacks). SHARED here so
// gbacore.c's session init and the fingerprint fill can never drift apart (SPEC §D6.1 field 5:
// "a single source of truth"). NOT a live read of s_netExp: at lobby time that static is stale.
#define DGBA_NET_EXP_DEFAULT 5

#define DGBA_FPRINT_BYTES 8   // sizeof(DgbaFprint) — the canonical wire length

// The link surface. 8 bytes, packed, fixed order — this byte sequence IS the serialization.
typedef struct __attribute__((packed)) {
	uint8_t gameCode[4];   // ROM header 0x080000AC..AF, raw ("BPEE"/"BPRE"/...). Diagnostic only:
	                       //   EM<->FR<->LG trades are legitimate, so a diff is never refuse-grade.
	uint8_t gameRev;       // ROM header 0x080000BC ("software version", GBATEK header layout).
	                       //   Revisions share the RAM map and trade freely -> forensic value only:
	                       //   the netlog now SAYS which rev ran (run-#12 had to prove FR rev1 by
	                       //   cb2 fingerprinting; never guess a symbol map again).
	uint8_t clProtoRev;    // CL_PROTO_REV (celiolink.h) — the one field that tracks OUR FSM's
	                       //   wire-visible behavior + CL_EV_* semantics.
	uint8_t netProto;      // DGBA_PROTO (netlink.h) — the transport framing revision. Already in
	                       //   the beacon since M1 but never actually COMPARED until now.
	uint8_t modeFlags;     // low nibble = the session-entry link strategy (DGBA_NET_EXP_DEFAULT);
	                       //   bit 7 reserved 0. Mid-link KEY_Y re-toggles are NOT re-exchanged
	                       //   (mid-link KEY_Y is already forbidden — HANDOFF run-#11 note).
} DgbaFprint;

// Fill the surface from the live values. gameCode4 may be NULL/short (missing bytes stay 0 —
// the "no core loaded yet" lobby case, exactly like today's empty advertisement game code).
void dgba_fprint_fill(DgbaFprint* f, const char* gameCode4, uint8_t gameRev,
                      uint8_t clProtoRev, uint8_t netProto);

// Two-lane FNV-1a over the canonical bytes, folded to one u64 (gen1's construction,
// Fingerprint.lua:21-63: "two 32-bit lanes, different offset bases -> 64-bit hex").
// lane index 0 = lane A (the HIGH 32 bits of the fold), 1 = lane B (the LOW 32 bits).
uint32_t dgba_fprint_lane(const DgbaFprint* f, int lane);
uint64_t dgba_fprint_hash(const DgbaFprint* f);

// Name EXACTLY what differs, e.g. "clProtoRev(1!=2),gameCode(BPEE!=BPRE)" — the gen1 modDiff
// lesson (Handshake.lua:215-236 via gen1-link.md): show the difference on the incompat surface
// "instead of a silent mid-battle draw". Fields are comma-separated, never space-separated, so a
// netlog line stays one greppable token. Returns the NUMBER of differing fields (0 = identical);
// *out is always NUL-terminated (empty on 0) and never overruns outMax.
int dgba_fprint_diff(const DgbaFprint* a, const DgbaFprint* b, char* out, int outMax);

// Format the one-line netlog record (SPEC §D6.6). Returns strlen(out), or -1 on a bad buffer.
//   # fprint local=BPEE r1 cl1 np1 m5 h=... peer=BPRE r0 cl1 np1 m5 h=... verdict=DIFF:gameRev(1!=0)
//   # fprint local=BPEE r1 cl1 np1 m5 h=... peer=none verdict=unknown
// UNKNOWN DISCIPLINE (peerValid == 0): the peer's surface never arrived -> the verdict is
// `unknown`, printed as such and NEVER as a match (the run-#11 lost-log lesson generalized:
// absence of evidence is not a PASS).
int dgba_fprint_format(const DgbaFprint* local, uint64_t localHash,
                       const DgbaFprint* peer, uint64_t peerHash, int peerValid,
                       char* out, int outMax);

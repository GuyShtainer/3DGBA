// celiolink_payloads.h — Gen-3 trade block layouts the local-termination synthesis ships.
// ============================================================================================
// CLEAN-ROOM, header-free pure C (<stdint.h>/<string.h>). Layouts mirror Celio-Link (GPL-3.0)
//   src/payloads/{linkPlayer,pokemon,mail,trainerCard}.* and are cross-checked vs the verified
//   digest docs/kb/celio/payloads-usb.md and pret (pokeemerald / pokefirered).
//
// SIZE NOTE (verified by compiling the struct, not by trusting the digest's hand-math):
//   payloads-usb.md says LinkPlayerBlock = "56 B = magic16 + LinkPlayer24 + magic16". That is a
//   hand-math ERROR. The actual `struct LinkPlayer` fields span 0x00..0x1B (version..language) =
//   28 bytes (0x1C), NOT 24 — `linkType` at 0x14 is a u32 and `id`/`language` follow at 0x18/0x1A.
//   With natural alignment (RP2040 and ARM both) the struct is 28 B, so LinkPlayerBlock is
//   16 + 28 + 16 = 60 B, and Celio's `blockCommandSetup(blk, sizeof(*blk), sizeof(*blk))` advertises
//   `sizeof` = 60 on the wire too. We therefore use 60, matching what Celio actually transmits.
//   (The exact byte count is what the receiver reassembles; advertised size is sizeof either way.)
// ============================================================================================

#ifndef CELIOLINK_PAYLOADS_H
#define CELIOLINK_PAYLOADS_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------
// LinkPlayer / LinkPlayerBlock  (Celio payloads/linkPlayer.{c,h})
// ---------------------------------------------------------------------------------------------
typedef struct {
	/* 0x00 */ uint16_t version;        // demo 0x4004
	/* 0x02 */ uint16_t lp_field_2;     // 0x8000
	/* 0x04 */ uint16_t trainerId;
	/* 0x06 */ uint16_t secretId;
	/* 0x08 */ uint8_t  name[8];        // GBA-charset, 0xFF-terminated
	/* 0x10 */ uint8_t  progressFlags;  // &0x0F=hasNationalDex, &0xF0=hasClearedGame
	/* 0x11 */ uint8_t  neverRead;
	/* 0x12 */ uint8_t  progressFlagsCopy;
	/* 0x13 */ uint8_t  gender;
	/* 0x14 */ uint32_t linkType;       // overwritten per-call (the LINKTYPE_* of this section)
	/* 0x18 */ uint16_t id;
	/* 0x1A */ uint16_t language;       // 0x0005 = English
} ClLinkPlayer;                          // = 28 bytes (0x1C)

typedef struct {
	char         magic1[16];            // "GameFreak inc."
	ClLinkPlayer linkPlayer;            // 28 bytes
	char         magic2[16];            // "GameFreak inc."
} ClLinkPlayerBlock;                     // = 60 bytes

#define CL_LINKPLAYER_BLOCK_SIZE  ((uint16_t)sizeof(ClLinkPlayerBlock)) // 60

// ---------------------------------------------------------------------------------------------
// Party (Celio payloads/pokemon.cpp): 600 bytes = 6 slots x 100-byte mons. Shipped as 3 x 200-byte
// blocks (subspan 0/200/400, 200). Each mon = 80B BoxPokemon + 20B battle stats = 100B (0x64).
// ---------------------------------------------------------------------------------------------
#define CL_PARTY_BYTES        600
#define CL_PARTY_SLOT_BYTES   100   // 0x64
#define CL_PARTY_SLOTS        6
#define CL_PARTY_CHUNK_BYTES  200   // transfer-chunk size (NOT slot size); 3 chunks = 600B

// ---------------------------------------------------------------------------------------------
// Mail: the empty-mail payload Celio ships is a 220-byte all-0xFF blob (payloads/mail.cpp), NOT
// the 34-byte per-entry struct. blockCommandSetup(0,0,220) advertises 220 bytes, zero data ->
// blockCommand zero-pads the whole thing (advertised-size-gated termination).
// ---------------------------------------------------------------------------------------------
#define CL_MAIL_BYTES   220

// ---------------------------------------------------------------------------------------------
// Ribbons: blockCommandSetup(nullptr,0,40) -> 40 zero bytes advertised.
// ---------------------------------------------------------------------------------------------
#define CL_RIBBONS_BYTES 40

// ---------------------------------------------------------------------------------------------
// Trainer card (Celio payloads/trainerCard.{c,h}): 96 bytes (0x60), declared block size 0x64.
// On the wire the games exchange a 100-byte block (pret sBlockRequests[BLOCK_REQ_SIZE_100] =
// {gBlockSendBuffer, 100}); the receiver parses it as `struct TrainerCard` USING THE VERSION IN
// THE SENDER'S LINKPLAYER identity (pret CopyTrainerCardData / FR Task_LinkupAwaitTrainerCardData).
// ---------------------------------------------------------------------------------------------
#define CL_TRAINERCARD_BYTES        96   // 0x60
#define CL_TRAINERCARD_ADVERTISED   0x64 // Celio declares 0x64 in blockCommandSetup
#ifndef CL_TRAINERCARD_WIRE_BYTES        // normally from celiolink.h (included first by celiolink.c)
#define CL_TRAINERCARD_WIRE_BYTES   100
#endif

// Build a WELL-FORMED canned 100-byte trainer card, coherent with the canned LinkPlayer identity
// (version low-byte 4 = FireRed, trainerId 0x529E, the same GBA-charset name). Run-#12 lesson: the
// old ALL-ZERO card black-screened Emerald's card viewer the first time it was rendered — a
// 0x00-filled playerName has NO 0xFF terminator (0x00 is a printable char), and zero version/gender
// feed sprite/facility-class lookups. Field offsets are the shared TrainerCardRSE prefix, identical
// in FRLG and Emerald (pret include/trainer_card.h): trainerId@0x0E, easyChatProfile@0x28 (u16[4],
// 0xFFFF = EC_EMPTY_WORD), playerName@0x30 (8 bytes, 0xFF-terminated), version@0x38 (FRLG field;
// harmless padding under an RS/Emerald parse).
static inline void cl_make_trainer_card(uint8_t out[CL_TRAINERCARD_WIRE_BYTES])
{
	memset(out, 0, CL_TRAINERCARD_WIRE_BYTES);
	out[0x0E] = 0x9E; out[0x0F] = 0x52;                    // trainerId 0x529E (LE), matches the identity
	memset(out + 0x28, 0xFF, 8);                           // easyChatProfile[4] = EC_EMPTY_WORD x4
	out[0x30] = 0xC8; out[0x31] = 0xDD;                    // the canned identity's name...
	out[0x32] = 0xE0; out[0x33] = 0xE7;
	memset(out + 0x34, 0xFF, 4);                           // ...0xFF-terminated + 0xFF padding
	out[0x38] = 0x04;                                      // version = FireRed (canned identity's 0x4004)
}

// ---------------------------------------------------------------------------------------------
// The canonical 100-byte "filler/empty slot" mon (Celio pokemon.cpp g_fillerPkmnArray). Used to
// fill our synthesized partner's empty party slots so the game sees a well-formed party.
// ---------------------------------------------------------------------------------------------
static const uint8_t CL_FILLER_PKMN[CL_PARTY_SLOT_BYTES] = {
	0x57,0xe5,0x88,0x2a,0x39,0x30,0x31,0xd4,0xc0,0xc3,
	0xc6,0xc6,0xbf,0xcc,0xff,0x00,0x00,0x00,0x02,0x02,
	0xc8,0xdd,0xe0,0xe7,0xff,0xff,0x00,0x00,0xfd,0xd1,
	0x00,0x00,0x7e,0x14,0xa0,0x9c,0xfc,0x25,0x99,0xc7,
	0x6e,0xd5,0xbd,0xfe,0x6e,0xd5,0xb9,0xfe,0x6e,0xd5,
	0xb9,0xfe,0x6e,0xd5,0xb9,0xfe,0x83,0xd5,0xb9,0xfe,
	0x6e,0xd5,0xb9,0xfe,0x76,0xd5,0xb9,0xfe,0xa7,0xd5,
	0xfd,0xfe,0x67,0xe8,0xb9,0xfe,0x6d,0x93,0xb9,0xfe,
	0x00,0x00,0x00,0x00,0x19,0xFF,0x3f,0x00,0x3f,0x00,
	0x2e,0x00,0x24,0x00,0x1d,0x00,0x2d,0x00,0x20,0x00
};

// Fill a 60-byte LinkPlayerBlock with the canned identity (the magic strings + demo values),
// stamping linkType. This is our own/default identity; over UDS the PEER's real one replaces it.
static inline void cl_make_linkplayer_block(ClLinkPlayerBlock* blk, uint32_t linkType)
{
	memset(blk, 0, sizeof(*blk));
	memcpy(blk->magic1, "GameFreak inc.", 14);
	memcpy(blk->magic2, "GameFreak inc.", 14);
	blk->linkPlayer.version       = 0x4004;
	blk->linkPlayer.lp_field_2    = 0x8000;
	blk->linkPlayer.trainerId     = 0x529E;
	blk->linkPlayer.secretId      = 0x1805;
	blk->linkPlayer.name[0] = 0xC8; blk->linkPlayer.name[1] = 0xDD;
	blk->linkPlayer.name[2] = 0xE0; blk->linkPlayer.name[3] = 0xE7;
	blk->linkPlayer.name[4] = 0xFF;
	blk->linkPlayer.progressFlags = 0xFF;
	blk->linkPlayer.linkType      = linkType;
	blk->linkPlayer.id            = 0x0000;
	blk->linkPlayer.language      = 0x0005;
}

#ifdef __cplusplus
}
#endif

#endif // CELIOLINK_PAYLOADS_H

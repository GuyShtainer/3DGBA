// fieldgate.h — the ONE "is this game in a meaningful field state" predicate, shared by the
// phase-14 tilt gate (G5-G8, tilt.c:143-156) and phase-15 presence (P-G6, presence.c).
// ============================================================================================
// SPEC-data.md D4.6 ("factor, do not copy"): rules P-G6 and tilt's G5-G8 ARE the same predicate,
// so they get one implementation. Header-only + `static inline` + pure C (no <stdint.h> even) so
// both header-free modules can include it with no link edge and the host suites keep
// dual-compiling on the PC (CLAUDE.md #4).
//
// D4.6.1 (regression proof): this refactor is behaviour-preserving BY CONSTRUCTION — the four
// lines it replaces in tilt_target_level were, verbatim:
//     if (!in->ok)                     return 0;   // G5
//     if (in->ctx != TILT_CTX_FIELD)   return 0;   // G6
//     if (!in->sb1Valid || in->px < 0) return 0;   // G7
//     if (in->textDlg)                 return 0;   // G8
// The proof is that test/host/test_tilt.c's 1694 checks pass UNMODIFIED (TEST 1-3 are the
// exhaustive gate truth table). If a tilt check ever needs editing to accommodate this file, the
// refactor is wrong — revert it rather than adjust the test.
//
// D4.6.2 (disclosed residual, inherited not solved): `ctx == GCTX_OVERWORLD` is game_read's
// FALL-THROUGH branch (gamestate.c:163-164, which sets ctxResolved = false there), so every
// full-screen screen the classifier cannot name — pokedex, town map, party SUMMARY, trainer card,
// mart/PC lists, the naming keyboard, the title/intro — reads as "in the field" here. tilt.c:85-106
// documents the promotion path (harvest the real cb2 constants from the phase-13 gs log and put
// them in GameProfile). Presence mitigates it with its own P-G7 object-agreement gate
// (SPEC-data D4.5) and does NOT invent a second, divergent field predicate.
#pragma once

// (2026-10-06: the phase-14 tilt was deleted — docs/REMOVED-3D-ATTEMPTS.md. The tilt.c/test_tilt.c
//  references above are historical; presence is now this predicate's only user.)
// == GCTX_OVERWORLD (gamestate.h:12). The enum crosses this boundary pinned at the CALL SITE by
//    _Static_assert(GCTX_OVERWORLD == FIELD_CTX_OVERWORLD, ...)   // main.c
// so a future insert into GameCtx is a COMPILE ERROR, not a silent mis-gate.
#define FIELD_CTX_OVERWORLD 1

// All args are plain ints so this header stays free of GameCtx/libctru/stdint types.
//   ok        game_read returned true for that game (i.e. profile_for found a profile)
//   ctx       GameState.ctx as read, compared against FIELD_CTX_OVERWORLD
//   sb1Valid  GameState.sb1Valid — gSaveBlock1Ptr deref'd, so px/py/map* are real
//   px        GameState.px (-1 = gSaveBlock1Ptr not ready)
//   textDlg   GameState.textDlg — a field textbox is up = a script is talking
// Returns 1 only when the game is in the free-roam overworld with a loaded save and no textbox.
static inline int field_state_ok(int ok, int ctx, int sb1Valid, int px, int textDlg) {
	if (!ok)                        return 0;   // no profile for this game            (tilt G5)
	if (ctx != FIELD_CTX_OVERWORLD) return 0;   // free-roam overworld only            (tilt G6)
	if (!sb1Valid || px < 0)        return 0;   // the save is actually loaded         (tilt G7)
	if (textDlg)                    return 0;   // a script is talking                 (tilt G8)
	return 1;
}

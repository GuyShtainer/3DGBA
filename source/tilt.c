// tilt.c — Phase 14 slice T1: HD-2D "diorama" tilt math + tween. Pure C, host-testable.
// ============================================================================================
// Spec: docs/phase14-tilt/SPEC-render.md §R1/R2/R6 (projection, cover, module contract) and
// SPEC-integration.md §2 (tween). Source of truth for the projection: gen1recomp
// src/render/Tilt.lua:110-153, read out in docs/kb/external/gen1-render.md findings 1, 3 and 4.
// Contract, invariants and the "why no fragment shader is needed" argument: see tilt.h.
//
// NOTHING here knows about the GPU, the screen rect or the scale mode. Callers compose:
//     tilt_project (frame space)  ->  calc_xform (main.c:709-715, affine)  ->  vertex
// affine o homography = homography, so the hardware's perspective-correct interpolation is still
// exact after the screen fit, for all three scale modes (SPEC-render R1.7).
#include <math.h>

#include "tilt.h"

// SPEC-render R2.5.5. Level 0 must be EXACTLY 0.0f: it is the only value for which the whole
// module collapses to the identity map, which is what PHASE.md invariant 1 rests on.
const float TILT_ANGLE_DEG[TILT_LEVELS] = { 0.0f, 10.0f, 15.0f, 20.0f };

float tilt_angle_deg_for_level(int level) {
	if (level < 0)              level = 0;
	if (level > TILT_LEVELS - 1) level = TILT_LEVELS - 1;
	return TILT_ANGLE_DEG[level];
}

float tilt_angle_for_level(int level) { return tilt_angle_deg_for_level(level) * TILT_DEG2RAD; }

// ---- gate + tween policy (SPEC-integration §1-§2, §5) --------------------------------------

// THE GATE, SPEC-integration §1.4 — the ONE place any of these rules exists (I1.11). Ordered
// ladder; the first rule that hits wins and every one of them returns 0, so the answer is
// order-independent (TEST 1 asserts that two simultaneous rules still give 0).
//
//  G1  userLevel <= 0                     the user says off
//  G2  !isN3DS                            tier clamp (I5.2): the row stays adjustable and the
//                                         SAVED preference is never rewritten (I5.1) — only the
//                                         LIVE level is clamped, so moving the SD card to a New
//                                         3DS restores what the user picked. Old 3DS is
//                                         best-effort by CLAUDE.md convention #1 and already
//                                         boots with an "expect slowdown" splash; spending render
//                                         budget on a taste effect there is the wrong trade.
//  G3  menuOpen                           §2.4 — and it TWEENS down, because main.c keeps calling
//                                         this (and tilt_tween_step) above the menu split (I2.5).
//  G4  wlOn || netOn                      I5.5. Argued, not assumed: under wlOn the peer core is
//                                         PAUSED (a frozen still — tilting it is pure cost), the
//                                         participant runs at ~4-5 fps during a Gen-3 trade, and
//                                         the render thread IS the link's clock (PHASE.md
//                                         invariant 3). The cheapest way to keep the trade path
//                                         untouched (invariant 2) is to run no new GPU work
//                                         beside it. linkOn — the in-process cable, both cores
//                                         free-running at full speed — is deliberately NOT here.
//  G5  !ok                                I1.6: profile_for returns NULL for any unmapped game
//                                         code, game_read then memsets + returns false, so a
//                                         non-Pokemon ROM lands here. Tested explicitly (rather
//                                         than leaning on ctx == GCTX_NONE) so a future refactor
//                                         of the sentinels cannot open the gate on a frame we
//                                         know nothing about.
//  G6  ctx != TILT_CTX_FIELD              I1.3: honored ONLY in the free-roam overworld. This is
//                                         strictly stronger than gen1recomp's own gate
//                                         (Tilt.lua:116-118 approximates it); gamestate.c reads
//                                         the game's live task table. BUT SEE THE RESIDUAL BELOW —
//                                         G6 is a NEGATIVE test, not a positive "free-roam" signal.
//  G7  !sb1Valid || px < 0                I1.5. ctx == GCTX_OVERWORLD is game_read's FALL-THROUGH
//                                         branch (gamestate.c:161-164) and it sets ctxResolved =
//                                         false there, so ctxResolved is useless as a positive
//                                         signal. These two are the project's existing "really in
//                                         the field" predicate — CtlIn.fieldValid (main.c:2329,
//                                         SPEC-control-replay D4.9), exercised on hardware by the
//                                         D4 walk scripts.
//  G8  textDlg                            I1.5: a field textbox = a script is talking = exactly
//                                         gen1's "no script runner" clause. textBanner (the
//                                         map-name popup) is NOT a rule — it is a ~2 s transient
//                                         and gating on it would flap.
//  G9  screen 1 && touchActive            §3 / PHASE.md invariant 4 — see the note below.
//  G10 screen 0 && stereoEngaged          I5.6: on the top screen STEREO WINS, so tilt can never
//                                         add cost to the 3-render stereo frame and the flat-rect
//                                         pop/warp overdraws never fight a tilted plane. The 3D
//                                         slider becomes a live A/B switch between the two
//                                         dioramas.
//  G11 fsOn && screen != focScreen        I5.4: frameskip exists to starve the UNFOCUSED game;
//                                         tilting it would spend the budget we just freed, on the
//                                         screen the player is not looking at, at ~20 fps (where
//                                         warp shimmer reads worst).
//
// KNOWN RESIDUAL OF G6 — DISCLOSED, NOT AN OVERSIGHT (SPEC-integration I1.7; re-confirmed by the
// 2026-08-04 fix pass). GCTX_OVERWORLD is game_read's FALL-THROUGH (gamestate.c:163), so every
// full-screen screen the classifier cannot name — pokedex, town map, party SUMMARY, trainer card,
// mart / PC list menus, the naming keyboard, the title/intro, and the cable-club TRADE menus while
// the in-process link is up (linkOn is deliberately not a rule, I5.5) — reads as ctx=OVERWORLD,
// sb1Valid=1, px>=0, textDlg=0 and therefore TILTS. G7/G8 cannot discriminate: sb1Valid stays true
// once a save exists, px keeps the last field tile, and textDlg (sFieldMessageBoxMode) is a
// field-only signal that reads 0 off the field. This is presentation-only, tweened and reversible,
// and it ships behind a default of level 0 (I5.8), so no existing frame changes until a user opts
// in. The two fixes an adversarial review proposes were BOTH considered and rejected here:
//   (a) a positive `cb2 == CB2_Overworld` GameProfile entry is the RIGHT answer, but the addresses
//       exist nowhere in this tree (no pret sym map is vendored) and a guessed one fails SILENTLY
//       in the worst direction — cb2 never matches => the whole feature is dead on hardware with
//       no error. House rule: verify-on-hw, never ship a blind address.
//   (b) the BG0 >=55%-coverage backstop (I1.7's own optional) is top-screen-only, would make the
//       two screens behave differently, and is a heuristic that can only be falsified on hardware.
// THE PROMOTION PATH IS ALREADY BUILT AND IS THE ACTION ITEM: the phase-13 gs logger records the
// raw cb2 of every screen visited (gamestate.c:339 CSV column `cb2`, with `resolved=0` marking
// exactly these fall-through screens) and slice T4 added the d_tiltLvl/d_tiltAngT columns beside
// it, so one hardware session with tilt on yields the cb2 constants for the screens that actually
// misbehave. Promote those into GameProfile and G6 becomes a real gate. Do NOT invent a second
// detector before that data exists (I1.7, verbatim).
//
// G9 IS PHASE.md INVARIANT 4, AND IT IS STRUCTURAL. SPEC-integration §3.2 chose (b) SUPPRESSION
// over (c) "implement the inverse": touch_to_gba (main.c:621-631) is consulted only when
// tmEff == TOUCH_SMART, and whenever tmEff != TOUCH_OFF this rule pins the bottom screen's target
// to 0 — so the touch mapping is bit-identical to today BY CONSTRUCTION, not by arithmetic. The
// three reasons (c) was rejected, recorded so nobody re-derives them: (1) the correct inverse
// depends on tw.ang, which moves every frame during a 250 ms tween, while touch is sampled in the
// parked window and the tween is stepped in the render phase — a NEW correctness class inside a
// hardware-validated feature; (2) the homography compresses the far rows, so a 1 px touch error
// there becomes 2-3 GBA px at 15 deg, which the 8-px bag/menu row hit-tests (touch.c:349) would
// not survive; (3) the standing instruction "finish/verify smart-touch on hardware one feature at
// a time; stop blind batches". TOUCH_PAD is suppressed too even though it never calls
// touch_to_gba (I3.6): its overlay (touch_draw, main.c:3043) is drawn in FLAT screen space over
// the game image, and a tilted game under a flat D-pad reads as a bug. One rule, two reasons.
// The closed-form inverse exists and is host-tested anyway (tilt_unproject, TEST 12), so enabling
// bottom-screen touch+tilt later is a one-line change and not a re-derivation.
//
// WHAT "STRUCTURAL" DOES AND DOES NOT COVER (2026-08-04 fix pass, review finding 5 — stated so the
// claim above is not read as more than it is). Structural: touch_to_gba itself is byte-identical to
// today and the gate can never hand a touched bottom screen a nonzero TARGET. NOT structural: the
// ANGLE reaches that target through the 250 ms tween (I2.4), so there is a bounded window in which
// touch is live over a bottom screen that is still unwinding. Its length is set by G3, not by G9 —
// touchMode can only change from inside the pause menu, and G3 already pinned the target to 0 when
// that menu opened — so the residual is (250 ms - however long the menu was open) and is worst when
// the user taps the TOUCH tab's Smart/Pad preview button (which closes the menu, main.c:2624-2628)
// within a few frames of opening it. During that window the flat inverse mis-maps by at most the
// flat-vs-tilted difference at the residual angle: ~10 GBA px vertically at 15 deg, decaying with
// the tween. The one-frame amplifier that made this materially worse — main.c feeding the gate a
// frame-top tmEff snapshot taken BEFORE those preview buttons run, which re-opened the gate for a
// frame and restarted the segment — is FIXED (main.c now reads the live touchMode; TEST 19 pins the
// sequence). The remainder is inherent to tweening and is deliberately not "fixed": snapping is
// forbidden by PHASE.md invariant 5, and suppressing the first taps would change a hardware-
// validated input path to buy a sub-quarter-second cosmetic. If hardware ever shows a real mis-tap
// here, the sanctioned lever is SPEC-render R6.3's ASYMMETRIC ease (slow in / ~3-frame out), which
// SPEC-integration I2.4's symmetric 250 ms overrode — it would cut this window to ~50 ms without
// touching either the gate or the touch path. verify-on-hw.
int tilt_target_level(const TiltGateIn* in) {
	if (in->userLevel <= 0)                                   return 0;   // G1
	if (!in->isN3DS)                                          return 0;   // G2
	if (in->menuOpen)                                         return 0;   // G3
	if (in->wlOn || in->netOn)                                return 0;   // G4
	// G5-G8 are the SHARED field predicate (fieldgate.h), factored out in phase 15 so tilt and
	// presence can never drift (SPEC-data D4.6). Same four rules, same order, same short-circuit —
	// test_tilt.c's 1694 checks (TEST 1-3 = the exhaustive gate truth table) pass unmodified.
	if (!field_state_ok(in->ok, in->ctx, in->sb1Valid, in->px, in->textDlg)) return 0;   // G5-G8
	if (in->screen == 1 && in->touchActive)                   return 0;   // G9
	if (in->screen == 0 && in->stereoEngaged)                 return 0;   // G10
	if (in->fsOn && in->screen != in->focScreen)              return 0;   // G11
	return (in->userLevel > TILT_LEVELS - 1) ? TILT_LEVELS - 1 : in->userLevel;
}

void tilt_tween_reset(TiltTween* t) {
	t->angFrom = 0.0f;
	t->angTo   = 0.0f;
	t->ang     = 0.0f;
	t->tMs     = TILT_TWEEN_MS;   // "already arrived" — a fresh tween never eases out of nowhere
	t->level   = 0;
}

// SPEC-integration I2.4, steps in order. Fed EVERY frame — including frames where the gate is
// closed, the pause menu is open (the osGetTime read at main.c:1777 sits above the menu split,
// so the tween keeps running) or the app just resumed — which is what makes every gate change a
// 250 ms tween instead of a snap (I2.5, PHASE.md invariant 5).
void tilt_tween_step(TiltTween* t, int level, float angTo, float dtMs) {
	// 1. clamp dt to [0, 100] ms. apt_hook suspends the app (main.c:1764 sleeps and `continue`s,
	//    skipping the nowMs read), so a HOME-menu excursion returns one enormous delta; clamping
	//    makes the resume look like a 100 ms step instead of teleporting the angle (I2.3).
	//    The !(>0) form also swallows a NaN delta.
	if (!(dtMs > 0.0f))          dtMs = 0.0f;
	if (dtMs > TILT_DT_MAX_MS)   dtMs = TILT_DT_MAX_MS;

	// 2. retarget: a mid-tween target change restarts the segment from the CURRENT angle, so ang
	//    is C0-continuous (never jumps). It is not C1 — velocity kinks on retarget; accepted, and
	//    cheaper than a spring (I2.4 step 2).
	if (angTo != t->angTo) { t->angFrom = t->ang; t->angTo = angTo; t->tMs = 0.0f; }

	t->level = level;                                     // 3.
	t->tMs  += dtMs;                                      // 4.

	// 5. Terminate by ASSIGNMENT, not by evaluating the lerp at u == 1: a completed down-tween
	//    must leave ang EXACTLY 0.0f (the host suite asserts it with ==, not an epsilon), because
	//    only then does tilt_active() return 0 and the flat path become byte-identical to today's
	//    render_game blit (I2.7 / PHASE.md invariant 1).
	if (t->tMs >= TILT_TWEEN_MS) {
		t->tMs = TILT_TWEEN_MS;
		t->ang = t->angTo;
	} else {
		float u = t->tMs / TILT_TWEEN_MS;
		float s = u * u * (3.0f - 2.0f * u);              // smoothstep — gen1's easing, monotone on [0,1]
		t->ang  = t->angFrom + (t->angTo - t->angFrom) * s;
	}
}

int tilt_active(const TiltTween* t) { return t->level > 0 || t->ang > TILT_EPS_DEG; }

// ---- projection / corners / view growth (SPEC-render R1, R2, R6) ----------------------------

// gen1recomp Tilt.viewGrowth (Tilt.lua:132-138) publishes topScale and their centre-pinned cover
// growth base = 1/(cos a * topScale); botScale has no analogue there because their camera is
// pinned to the viewport centre and the near overshoot is simply thrown away.
float tilt_top_scale(float angleRad) { return 1.0f / (1.0f + 0.5f * sinf(angleRad)); }
float tilt_bot_scale(float angleRad) { return 1.0f / (1.0f - 0.5f * sinf(angleRad)); }

// THE key observation (SPEC-render R2.3): the homography expands the near edge almost exactly as
// much as it shrinks the far one, so if we stop pinning the CENTRE the tilt is nearly
// height-preserving. Projected height at k = 1 is vh*cos a/(1 - 0.25 sin^2 a), hence
//     kfit = (1 - 0.25 sin^2 a) / cos a
// which is 1.018 at 15 deg against gen1's base = 1.169 — their whole "17 % overscan at 15 deg"
// cost is an artifact of centre-pinning and evaporates when the NEAR edge is pinned instead.
// Retained frame: 95.9 % (fit) vs 77.8 % (their way) at 15 deg.
float tilt_cover_fit(float angleRad) {
	float s = sinf(angleRad);
	return (1.0f - 0.25f * s * s) / cosf(angleRad);
}

// The scale at which even the far corners reach the frame edge, i.e. zero void — bought at ~20
// points of retained frame (R2.4). Reachable through TILT_COVER_MIX without a code change.
float tilt_cover_full(float angleRad) { return 1.0f + 0.5f * sinf(angleRad); }

float tilt_cover(float angleRad, float mix) {
	float kf = tilt_cover_fit(angleRad);
	if (mix == 0.0f) return kf;                            // shipped path: exactly kfit, no rounding
	return kf + mix * (tilt_cover_full(angleRad) - kf);
}

void tilt_view_init(TiltView* v, float angleRad, float vw, float vh, float coverMix) {
	v->angle = angleRad;
	v->sinA  = sinf(angleRad);                             // exactly 0.0f at angleRad == 0.0f
	v->cosA  = cosf(angleRad);                             // exactly 1.0f at angleRad == 0.0f
	v->vw    = vw;
	v->vh    = vh;
	v->d     = TILT_FOCAL * vh;                            // 160 — do NOT invent another focal
	v->k     = tilt_cover(angleRad, coverMix);
	// Bottom-anchored: pin the NEAR edge to cy = vh so the rows carrying the player sprite, the
	// ledge in front of them and any textbox are never cropped (R1.6/R2.5.4). The image therefore
	// rides upward by (vh/2 - yc) frame px — 6.95/10.35/13.68 at 10/15/20 deg — which is what a
	// camera does when it pitches down and re-aims. Constant per angle, so it rides the tween and
	// never pops. Together with k = kfit this makes the projected quad span EXACTLY fy in [0, vh].
	v->yc    = vh - v->k * (vh * 0.5f) * v->cosA / (1.0f - 0.5f * v->sinA);
}

void tilt_project(const TiltView* v, float cx, float cy, float* fx, float* fy, float* q) {
	float u  = cx - v->vw * 0.5f;
	float w  = cy - v->vh * 0.5f;                          // > 0 = toward the viewer (R1.10)
	// q > 0 everywhere for every a < 90 deg since d = vh: d - w*sin a >= d*(1 - 0.5 sin a) > 0.
	// No near-plane guard, and deliberately NO runtime clamp — a clamp would silently hide a
	// units bug (R1.2). At angleRad == 0 this is exactly d/d == 1.0f.
	float qq = v->d / (v->d - w * v->sinA);
	*fx = v->vw * 0.5f + v->k * u * qq;
	*fy = v->yc       + v->k * w * v->cosA * qq;
	*q  = qq;
}

// Inverse of the above (R6.5). Yp == w*q, then solve q out:
//     fy - yc = k*cos a * w*q          =>  Yp = (fy - yc)/(k*cos a)
//     Yp*(d - w*sin a) = w*d           =>  w  = Yp*d/(d + Yp*sin a)
// The denominator is bounded away from zero for every shipped angle (at 20 deg the frame maps to
// Yp in [-68.3, +96.5], so d + Yp*sin a stays in [136.6, 193.0]).
void tilt_unproject(const TiltView* v, float fx, float fy, float* cx, float* cy) {
	float Yp = (fy - v->yc) / (v->k * v->cosA);
	float w  = Yp * v->d / (v->d + Yp * v->sinA);
	float qq = v->d / (v->d - w * v->sinA);
	*cx = (fx - v->vw * 0.5f) / (v->k * qq) + v->vw * 0.5f;
	*cy = w + v->vh * 0.5f;
}

float tilt_disp_scale(const TiltView* v, float cy) {
	float w = cy - v->vh * 0.5f;
	return v->k * (v->d / (v->d - w * v->sinA));           // exactly 1.0f at angle 0
}

// R2.4's three edge figures, all GBA px per side. They are the two signs of one quantity, the
// per-row projected width ratio k*q: where k*q < 1 the frame rect is UNCOVERED by (vw/2)(1-k*q)
// destination px (the top-corner wedges, filled by the theme background — R2.5.2, zero extra
// geometry); where k*q > 1 the row overhangs and (vw/2)(1 - 1/(k*q)) SOURCE columns per side fall
// outside and are cropped. Cross-check of the near-row figure: k*q_near = kfit*botScale =
// (1 + 0.5 sin a)/cos a = gen1's own base(a), so cropNear = (vw/2)(1 - 1/base) exactly.
void tilt_coverage(const TiltView* v, float* voidFarPerSide,
                   float* cropMidPerSide, float* cropNearPerSide) {
	float half  = v->vw * 0.5f;
	float hh    = v->vh * 0.5f;
	float qFar  = v->d / (v->d + hh * v->sinA);            // q at cy = 0
	float qNear = v->d / (v->d - hh * v->sinA);            // q at cy = vh
	if (voidFarPerSide)  *voidFarPerSide  = half * (1.0f - v->k * qFar);
	if (cropMidPerSide)  *cropMidPerSide  = half * (1.0f - 1.0f / v->k);            // q == 1 at cy = vh/2
	if (cropNearPerSide) *cropNearPerSide = half * (1.0f - 1.0f / (v->k * qNear));
}

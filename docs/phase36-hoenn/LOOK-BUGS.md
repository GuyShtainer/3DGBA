# Phase 36: look bugs reported by Guy (2026-10-08)

Guy reviewed the night's Azahar evidence (H3 to H5) on 2026-10-08 and marked four bugs on the screenshots. His words are quoted
as he wrote them; the "seen" and "likely" lines are mine, and every "likely" is **unverified**: confirm each in `art` /
`preview` and on screen before fixing. None of them is fixed yet. Fix them one at a time, each with an Azahar before / after
of the same spot.

| # | Guy's words | Where (evidence shot) | Seen | Likely (unverified) |
|---|---|---|---|---|
| 1 | "the side of a right house is not natural as it should come down to ground with right angel and it seems to recede without a right angel" | Lilycove, the wooden house `lilycove_wood` (H3), `evidence/h3-hw-before-after.png`, the house on the right of the frame | the house's west (left) side face slants back as it goes down, instead of standing vertical and meeting the ground at a right angle | the end wall made by look L5 (`rg_close_sides`, `rg_bspecs.c:1047`) for this three-roof profile follows a slope down to the ground, not a vertical wall |
| 2 | "there is no left side for the mall" | Lilycove Department Store `lilycove_store` (H3), `evidence/h3-sm-before-after.png`, top right | the store's front stands, but there is no west side wall behind its left edge: the ground shows through | the west end is not closed (side closing skipped, or its face culled / wound the wrong way) |
| 3 | "the ground for plants looks like a liquid that spills, but should be straight" | Route 119 above the Weather Institute (0/34, player at 6,24), `evidence/h5-wib-before-after.png`, top right | a planted patch on the cliff top (the heart-shaped bed) and the cliff under it have slanted, smeared edges like spilled liquid where they should be straight | **not a building bug**: the same shape is in the BEFORE half too, so it predates H5. It is the terrain / relief rendering of that cliff and bed edge |
| 4 | "the rocket is missing a model" | Mossdeep Space Center, Emerald (0/6, player at 64,6), `evidence/h4-sc2-before-after.png` | the rocket and the two red gantries stay flat map art | they are upper-layer art in the cell rows above the `mossdeep_space` rect (rows 2-7 above it, BUILDLOG-H4); no recipe covers them. A rocket model (a round body with a nose cone) and two lattice gantry towers are needed. Related backlog: from the south, the Space Center's 82 px wall hides the rocket |

The smaller look items each slice logged are in the BUILDLOG "Next" sections: H3 (Contest Hall seam, cave-arch base lines), H4
(Space Center hides the rocket from the south; pointed Sootopolis dwellings not provably 3D from the default camera; door-less
twins flat), H5 (Weather Institute side-face fin and slab; Route 112 station roof as a block; Trick House roof over the player's
legs, ghosted).

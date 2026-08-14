# Audit-page review — 2026-08-14

Three adversarial reviewers (theme/CSS, information design, accessibility/responsive) + a
verifying synthesizer, run against the base64-stripped skeleton rather than the 2.5 MB page.
34 raw findings; the synthesizer dropped 6 as wrong or superseded. Everything below was
re-verified by hand before it was applied.

## Applied

1. **The status table contradicted the page.** "Ruby + Sapphire … no Ruby or Sapphire ROM exists
   here, so not one has ever been executed" sat two screens below 12 Ruby captures, 3 Sapphire
   captures and a Ruby+Sapphire co-op group. Stale since the user supplied their ROM library.
   Rewritten, along with three other rows that had drifted (co-op now includes RS; text size is
   re-baked and awaiting the hardware re-check, not "next up"; door pathing has now been driven
   in a live overworld). **This was the worst defect on the page** — an "Honest status" section
   disproved by its own screenshots.
2. **The restructure had over-trimmed: 11 of 238 thumbnails visible on load, FireRed showing
   zero.** The default-open group was picked by list index, which always landed on the four boot
   screens. Now opens the locations group per gallery → 52 of 246 visible (a 76% cut from the
   original 213-image wall, but every section shows real evidence).
3. **Light-theme contrast inversion on the active nav pill.** `--acc` #BE7A16 is 3.51:1 on the
   panel — *dimmer* than the resting `--dim` label at 5.33:1, so "you are here" was the least
   legible pill in the rail. Dark theme is 10.11:1, which is why it survives a dark-mode read.
   Added `--acc-ink` (#8A5A0F, 5.92:1) defined in all three theme states, used for small-text
   amber only (active pill, button hover, disclosure glyph); borders/backgrounds keep `--acc`.
4. **No intrinsic image sizes.** Opening a group — or "Open all" on 97 FireRed shots — expanded
   to caption height then snapped as the WebPs decoded. All 246 imgs now carry width/height.
   Two consequences handled: `align-items:start` (mixed 320×240 / 400×240 groups were stretching
   short cards into a slab of bare panel), and the img backdrop moved #000 → `--panel2` so a
   pre-decode "Open all" does not flash a field of black rectangles across a parchment page.
5. **`.gsubn` opacity:.75** took `--dim` from 4.47:1 to 2.86:1 — below even the non-text floor —
   on exactly the two labels ("covers Ruby / Sapphire", "covers LeafGreen") that explain why
   those games have no separate census. Alpha removed.
6. **Nav rail**: mask-image fade as a scroll affordance (7→8 pills overflow ~390px phones),
   inset focus ring (`overflow-x:auto` computes overflow-y, clipping an outset one), an opaque
   `background` fallback before the `color-mix()` (unsupported → transparent labels over
   scrolling screenshots), `-webkit-backdrop-filter`, and 82%→92% opacity so dark screenshots
   stop bleeding through the labels.
7. **Labelling.** `#impl` was 49% not-touch — split the first boots into their own `#firsts`
   section with its own nav link; `n_firsts` had been counting co-op captures as boots;
   `evidence/sapphire/` (3 real captures) was on disk and never rendered.
8. **Nav idempotence.** `if 'class="qnav"' not in html` meant any future change to NAV would
   silently never appear on the published page — the 8th link would have been swallowed. The
   rail is now stripped and re-emitted every run.

## Rejected, with reasons

- Merging the sharp-text and type-role sections (18 images → 6, a table). Deletes the page's
  central evidence that the text fix is real, to save about one screen of scroll.
- `aspect-ratio:4/3 + object-fit:contain` on thumbnails — superseded by real width/height, and
  it would stamp permanent letterbox bars into the 400×240 and 900×540 shots.
- Wrapping captions to two lines: 19 of 246 ids overflow; `title=` recovers them all on hover
  without making row heights ragged across a 35-image grid.
- Sweeping the hand-authored sections' literal backdrops to tokens — those are deliberate crops
  matched to their own content, sit under opaque images, and are outside the managed block.
- Moving the rail above the stat tiles. Measured as a phone-only win (~631px → ~470px) at the
  cost of interrupting the hero between its lede and its headline numbers. The rail is sticky,
  so one short scroll pins it permanently.

The reviewers reported the page's own HTML as living in `tools/` — it did not, it was only ever
in a session scratchpad. Fixed by actually putting it there: `tools/3dgba-ui-audit.html` is now
the versioned source, with `gallery/page-skeleton.html` (base64-stripped, ~50 KB) checked in
alongside so the markup stays reviewable without cloning 2.5 MB of data URIs.

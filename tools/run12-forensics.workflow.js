export const meta = {
  name: 'run12-forensics',
  description: 'Root-cause HW run #12: HOST trainer-card black screen + JOIN room-exit black screen',
  phases: [
    { title: 'Evidence', detail: '6 parallel readers: logs, pret protocol, celiolink code, Celio modes' },
  ],
}

phase('Evidence')

const PROJ = '/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA'

const REPORT_SCHEMA = {
  type: 'object',
  properties: {
    summary: { type: 'string', description: '5-10 sentence executive summary of what you found' },
    report: { type: 'string', description: 'Full detailed markdown report with file:line citations for every claim' },
    open_questions: { type: 'string', description: 'What you could not determine and why' },
  },
  required: ['summary', 'report', 'open_questions'],
}

const CONTEXT = `
CONTEXT (project 3DGBA, dir ${PROJ}): a New-3DS homebrew running two Gen-3 Pokemon games (Emerald=HOST console, FireRed=JOIN console) on embedded mGBA, linked over UDS wireless via "Celio local termination" (state F): each console's SIO driver locally synthesizes the ENTIRE link partner for its game (PacketLayer: single-word handshake B9A0->8FFF, then 9-word frames [CRC][8 cmd words]); only semantic events (party blocks, select/confirm) cross the radio. Code: source/celiolink.c (FSM: sections SETUP=0/CONNECTION=1/DISCONNECT=2/LOUNGE=3), wired in source/gbacore.c, transport source/netlink.c.
HW run #12 (2026-07-15, logs netlogs/*0715*): the trade itself COMPLETED and SAVED on both consoles. Then two NEW failures:
(A) HOST (Emerald, master game, seat 0): user checked "the friend's trainer ID" (in the trade-center room, post-trade) -> it started, then BLACK SCREEN + SOUND STUTTERING.
(B) JOIN (FireRed, slave game, seat 1): user walked out of the trade room -> BLACK SCREEN (this is the KNOWN open bug from runs #10/#11; a LOUNGE-section rebuild in run #11 did NOT cure it).
Known prior signature (run #10): JOIN stuck at CB2_Overworld sending 0000 while partner spams CAFE keepalives in LOUNGE.
Log formats: net log header lines start with #; "# celio" line = final FSM state; "# celio-trace f=<emuframe> cmd=<word> sec=<section> st=<state> blk=<blockSeq>" = FSM transition trace (32 entries, transition-only). Ring rows: idx,transferN,emuFrame,isCrcSlot,rtt_us,...,F,w0,w1,ok,sub where w0/w1 are the two 16-bit words of that transfer (one direction is the game's SIOMLT_SEND, the other is our synthesized partner's answer — determine orientation from the code or data). gs log: game-state samples, header explains columns; cb2 = raw gMain.callback2 pointer (Thumb-stripped) — map via the .sym files. Sym maps: /tmp/EMERALD.sym, /tmp/FIRERED.sym (rev1), /tmp/FIRERED_rev0.sym (rev0). NOTE: the app's own header comment says CB2_PrintErrorMessage=0800AF2C for FR which matches REV0 — determine which rev the log cb2 values actually match before mapping.
Gen-3 link background: LINKCMD words are 0xXXXX 16-bit commands in slot cmd[0] of the 8-word frame; 0xCAFE-prefixed frames carry held keys; RECEIVED_NOTHING (all-zero frame) makes the game SKIP its whole frame update when in SendingKeysOverCable (pret link.c ~1793). The partner heartbeats [0xCAFE, 0x0011=LINK_KEY_CODE_EMPTY].
`

const results = await parallel([
  // 1 — JOIN timeline
  () => agent(`${CONTEXT}
YOU ARE THE JOIN-CONSOLE FORENSICS ANALYST. Files:
- ${PROJ}/netlogs/3DGBA_net_JOIN_0715_162349.txt (read ALL of it: every # header line, every celio-trace line, and the full ring)
- ${PROJ}/netlogs/3DGBA_gs_JOIN_0715_162350.txt (read all; heartbeat samples + edge-triggered link-state dumps)
- Role-less segments ${PROJ}/netlogs/3DGBA_gs_0715_162447.txt and ${PROJ}/netlogs/3DGBA_gs_0715_163056.txt — decide which (if either) belongs to the JOIN console (match by game addresses FR vs EM, map ids, frame continuity) and use it if relevant. There is also 3DGBA_touch_0715_162447.txt if useful for attribution.
- Sym maps at /tmp (see above; pick the FR rev whose known symbols make the observed cb2 values land INSIDE plausible functions — check both rev0 and rev1; use Bash grep/sort on the sym file to map each distinct cb2/cb1 value to the containing symbol: symbols are 'ADDR l SIZE name' sorted by addr, find the greatest symbol addr <= cb2).
DELIVER:
1. Which FR revision the user's ROM matches (evidence).
2. A full timeline of the session: link start -> room entry -> trade -> post-trade -> exit attempt -> freeze. Anchor each step to emu-frame numbers using celio-trace + gs rows + net ring.
3. The freeze signature: final cb1/cb2 (mapped to symbol names), lerr/lstat/lbuf/lnotrecv values, celio section/state at the end, and the exact repeating word pattern in the ring tail — including WHICH side sends what (the game sends w?=0000 forever? our partner sends CAFE/0011?). Note sioMode/siocnt (601F) meaning.
4. CRITICAL QUESTION: during the exit attempt, did the FR game EVER send a nonzero command (0xCCCC READY_EXIT_STANDBY, 0x5FFF, ANY LINKCMD)? Scan the entire ring + trace for the last nonzero game-side words and timestamp them. Also: is the game still clocking transfers at the end (gate engaged) and at what rate?
5. Compare against the run-#10 signature (CB2_Overworld + 0000 vs CAFE): same freeze or different?
6. Anything anomalous: forceN=1, drops=52, resetN=0, the CB0F CRC value (what CRC does CB0F correspond to — constant or varying), gaps in emu-frame progression (frozen game = frame counter stalls).`,
    { label: 'forensics:JOIN', phase: 'Evidence', schema: REPORT_SCHEMA }),

  // 2 — HOST timeline
  () => agent(`${CONTEXT}
YOU ARE THE HOST-CONSOLE FORENSICS ANALYST. Files:
- ${PROJ}/netlogs/3DGBA_net_HOST_0715_162959.txt (read ALL: headers, celio-trace, full ring). NOTE its startN=53124 but the file starts at 16:29:59 — the netlog file gets REWRITTEN on link re-attach, so this file covers only the FINAL attach; counters are cumulative. Figure out what triggered the re-attach if you can.
- ${PROJ}/netlogs/3DGBA_gs_HOST_0715_163000.txt (read all)
- Role-less segments ${PROJ}/netlogs/3DGBA_gs_0715_162447.txt and ${PROJ}/netlogs/3DGBA_gs_0715_163056.txt — decide which (if either) belongs to the HOST console (Emerald) and use it; 163056 starts ~1 min after the HOST files and may be a post-freeze re-attach segment.
- /tmp/EMERALD.sym for cb1/cb2 mapping (symbols 'ADDR l SIZE name'; map cb2 to the containing symbol = greatest addr <= value).
DELIVER:
1. Timeline of what this segment covers: is the trade inside it (celio-trace shows sec=1 blk=4->5 with 8888/CCCC/BBBB commands early)? Then the post-trade room, then the trainer-card check, then the freeze.
2. The trainer-card ("check friend's trainer ID") event: find it in the gs log (cb2 change to a trainer-card CB2? task list change? map every distinct cb2 in the session to a symbol) and in the net ring (what command words did the Emerald game emit right before/during? any words our FSM would not recognize — e.g. block-INIT 0xAAAA/0xAAAB, 0x2FFE, card-specific LINKCMDs?).
3. The freeze signature: final cb2 (mapped), lerr/lstat/lbuf/lnotrecv, celio section/state (header says section=3 LOUNGE state=2 frames=5812 tradeComplete=1), the repeating ring tail (961E CRC + CAFE/0011 BOTH directions — decode: is the GAME also sending CAFE/0011 i.e. SendingKeysOverCable, or is that our partner echo?).
4. SOUND STUTTERING hypothesis from the data: stutter usually means the emu worker thread is being blocked/held or the game loops a few frames. Look at emu-frame progression rate in the ring (frames column vs transfers), rtt/pace columns, and any hold/serve diagnostics. Is the game still advancing frames at the end? At what fps?
5. Compare HOST's final state vs JOIN's: HOST game actively exchanges CAFE/0011 key frames both directions while black — what game state does that correspond to (still in room link loop = SendingKeysOverCable, screen black = a CB2 that never renders)?
6. What got lost to the re-attach wipe and what triggered it (net log re-attach happens when the link is restarted — did the user restart the link on HOST after the freeze? correlate with gs_0715_163056).`,
    { label: 'forensics:HOST', phase: 'Evidence', schema: REPORT_SCHEMA }),

  // 3 — pret room exit protocol
  () => agent(`${CONTEXT}
YOU ARE THE PRET ROOM-EXIT PROTOCOL RESEARCHER. Sources: /tmp/pret/pokefirered (the frozen exiter is FireRed) and /tmp/pret/pokeemerald (cross-check; Emerald names are often cleaner). These are full decomp source trees — grep them.
QUESTION: what EXACTLY happens, at the link-protocol level, when a player walks out of the Trade Center room (post-trade), on the SLAVE (non-master) game? The observed failure: the FR slave game fades to black and then sends 0x0000 on SIO forever (SIOCNT still MULTI+IRQ 0x601F), never progressing, while the synthesized partner sends [CAFE,0011] key frames every ~8 words.
INVESTIGATE (with file:line citations):
1. The Trade Center exit flow: the map scripts (data/maps/TradeCenter/scripts.inc or similar), the on-frame/warp scripts, specials called (e.g. CallFrontierUtilFunc? Special_...? ExitLinkRoom?). In FRLG likely 'CableClub_EventScript_ExitLinkRoom' or similar in data/scripts/cable_club.inc.
2. The C side: SetLinkStandbyCallback / SetCloseLinkCallback / Task_WaitForLinkPlayerConnection / sub chain in src/cable_club.c + src/link.c. What LINKCMDs flow (exact hex values from include/link.h or link.c BuildSendCmd): READY_EXIT_STANDBY? 0xCCCC? LINKCMD_READY_CLOSE_LINK 0x5FFF? 0xAAAA? In what order, and what must the PARTNER send for each stage to complete?
3. On the SLAVE specifically: while waiting in these stages, what does the game put in SIOMLT_SEND each transfer? Under what conditions would it send constant 0x0000 (empty send queue? gSendCmd cleared? not in SendingKeysOverCable anymore?), and is a slave sending 0000 while still MULTI+IRQ consistent with being stuck in (a) the standby wait task, (b) the fade/warp, or (c) after CloseLink but before SIO shutdown?
4. What does the game WAIT ON at each exit stage (e.g. all players' linkPlayerCount? a specific received cmd? gReceivedRemoteLinkPlayers=0?). Identify the EXACT predicate that, unsatisfied, leaves the screen black. Note: the fade to black happens first, THEN the link wait — confirm order.
5. Also trace the NORMAL post-trade menu-cancel path (trade menu -> cancel -> back to room) vs the room-exit path — the trade->room re-entry worked; exit does not.
6. Answer concretely: what should a synthesized partner SEND (sequence of 8-word command frames) to let the slave complete the exit? Does the partner ALSO need to stop sending CAFE key frames at some point (does a lingering CAFE keyframe corrupt the exit-standby exchange)?
Also check: does the master game (Emerald) gate the slave's exit (i.e. must the 'other player' also leave)? What is LINKCMD 0xCB0F or how could a CRC value CB0F arise (probably just a CRC, ignore if so).`,
    { label: 'pret:room-exit', phase: 'Evidence', schema: REPORT_SCHEMA }),

  // 4 — pret trainer card protocol
  () => agent(`${CONTEXT}
YOU ARE THE PRET TRAINER-CARD PROTOCOL RESEARCHER. Sources: /tmp/pret/pokeemerald (the freezing game is Emerald, the MASTER game) and /tmp/pret/pokefirered (cross-check).
SCENARIO: post-trade, both players back in the Trade Center room. On the Emerald console the user checked "the friend's trainer ID" — most likely talking to the OTHER PLAYER's NPC in the room (our app synthesizes that partner NPC locally with a canned identity), or possibly a PC/board in the room. The screen went to the trainer-card view start (user says "it started"), then BLACK SCREEN + sound stuttering.
INVESTIGATE (file:line citations):
1. Find the interaction: Trade Center / Cable Club room scripts (data/maps/TradeCenter/, data/scripts/cable_club.inc): what happens when you interact with the partner player object in the room? Is there a 'trainer card' show? Which special (e.g. Special_ShowTrainerCard? sub_80B3AEC?) and which C entry (src/trainer_card.c ShowTrainerCardInLink / CB2_ShowTrainerCardForLinkPlayer?).
2. Does showing the partner's card use LOCALLY CACHED data (gTrainerCards[] filled during room ENTRY block exchange — check src/cable_club.c: which block is exchanged when entering the room, does it include a TrainerCard struct? sBlockRequest types / sBlockRequestLookupTable {size,addr}: type 2 = trainer card?) or does it trigger a FRESH link exchange when viewed?
3. If a fresh exchange: exact protocol (LINKCMDs, block request type, sizes) and what the partner must answer. If cached: what in the VIEW flow still touches the link (e.g. a standby/sync 0xCCCC handshake before/after showing the card — FRLG/Emerald often wrap field specials in SetLinkStandbyCallback), and what predicate unsatisfied = black screen.
4. The card view lifecycle: CB2 chain (CB2_ShowTrainerCard...), the fade out (black) before the card renders — where between 'user pressed A' and 'card visible' can it stall waiting on the link? The user said it STARTED then black — so likely the fade-out completed and the wait predicate never satisfied.
5. Sound stuttering: does the card-show path stop calling m4aSoundMain / does a blocked link wait skip frame updates (RECEIVED_NOTHING skip at link.c ~1793) — what would make audio stutter vs silence?
6. What data does the card view need about the partner (gTrainerCards entry, easy-chat words, stars, playtime...)? If our canned LinkPlayer identity never provided a real TrainerCard block, what happens — garbage render, or a wait for a block that never arrives?
7. Answer concretely: what must the synthesized partner do so 'check friend's card' completes: (a) at room entry (send a proper trainer-card block — spec: block type/size/content), and/or (b) at view time (ack a standby exchange — exact frames)?
Note: on the wire during the freeze, BOTH directions show repeating [CRC][CAFE][0011][0000 x6] — i.e. the Emerald game itself still sends CAFE/0011 key frames. Reconcile that with your protocol trace (does the field keep SendingKeysOverCable while a special waits for a link sync?).`,
    { label: 'pret:trainer-card', phase: 'Evidence', schema: REPORT_SCHEMA }),

  // 5 — celiolink code audit
  () => agent(`${CONTEXT}
YOU ARE THE CELIOLINK CODE AUDITOR. Read COMPLETELY: ${PROJ}/source/celiolink.c (1190 lines), ${PROJ}/source/celiolink.h, plus the state-F wiring in ${PROJ}/source/gbacore.c (grep for cl_ / celio / net_celio) and the net_event_* layer in ${PROJ}/source/netlink.c. Also skim ${PROJ}/test/test_celiolink.c to see what IS covered (esp. TEST 13 LOUNGE re-establishment).
DELIVER (file:line citations):
1. A precise map of the LOUNGE (CL_SEC_LOUNGE) section handler as-built: which incoming game commands it recognizes and answers (READY_EXIT_STANDBY? which hex? BBBB/CCCC/DDDD/AABB? LinkPlayer INIT/CONT 0xAAAA/0xAAAB?), the roomKeys heartbeat gating (when set/cleared, when the proactive heartbeat fires), and the section transition rules in/out of LOUNGE.
2. Same for SETUP (the section that WORKS for room entry) — diff SETUP vs LOUNGE: what does SETUP do that LOUNGE doesn't (walk script, READY_EXIT_STANDBY answer, block serving, holds)?
3. The hold mechanics: which events hold the section (whole-idle holds, per-chunk serve holds), what a hold does to the emu thread (does it block the frame -> would it STUTTER AUDIO on the HOST?), and what releases each hold.
4. Unknown-command behavior: when the game sends a command word the section switch doesn't handle (e.g. a trainer-card block request), what happens — ignored? echoed? Does the partner keep heartbeating CAFE? Could that hold something forever?
5. The exit path: is there ANY handling for the game LEAVING the room (READY_CLOSE_LINK 0x5FFF, exit standby)? What would the FSM do when the FR slave game starts its room-exit sequence? Explain the observed JOIN signature: game sends 0000 every transfer, partner sends [CAFE,0011] frames, section=3 state=2, gate still clocking (gateN huge), forceN=1.
6. Explain the observed HOST signature: game actively sends [CAFE,0011] frames (mirrored by partner), section=3, black screen, audio stutter — which code path keeps answering key frames, and is anything holding the emu (serve holds) that would stutter audio?
7. The trace/log encodings: exact meaning of celio-trace fields, ring w0/w1 orientation per role (which column is the game's word on HOST vs JOIN — check the netlog writer in gbacore.c), CRC-slot values (would CB0F/961E be expected CRCs), and 'st' values at trace time.
8. List the top candidate defects (be specific: line numbers, missing cases) that could produce EACH of the two failures.`,
    { label: 'code:celiolink', phase: 'Evidence', schema: REPORT_SCHEMA }),

  // 6 — Celio upstream modes research
  () => agent(`${CONTEXT}
YOU ARE THE CELIO-UPSTREAM MODES RESEARCHER. Local digests: ${PROJ}/docs/kb/celio/*.md (read PORT-SPEC.md, trade-fsm.md, callbacks.md, emu-module.md, server-relay.md, client-emitter.md, payloads-usb.md) and ${PROJ}/docs/kb/celio/audit/*.md. Then find the actual Celio project on GitHub (WebSearch for it — a GPL-3.0 open-source project doing Gen-3 GBA link-cable trades online with local termination; the kb docs cite paths like src/sections/tradeSetup.cpp, packetLayer.hpp — search e.g. 'github celio gba link trade packetLayer' or terms from the docs) and read its repo structure.
QUESTIONS:
1. How does upstream Celio separate TRADE vs BATTLE modes? Enumerate its modes/sections (the kb cites src/sections/{tradeSetup,tradeConnection,tradeDisconnected,tradeLounge}.cpp — is there a battle*.cpp family? a colosseum mode?). For each battle-related mode: is it LOCAL TERMINATION (synthesis) like trade, or frame RELAY (RTT-bound)?
2. What link-type words distinguish trade vs battle in Gen-3 (we observe 0x1133=trade-setup family, 0x1122, 0x1144, 0x1111 per session; battle announces a different linktype -> 'chose differently' refusal). Where in the handshake/section flow does the game announce it, and could our FSM AUTO-DETECT the requested mode from that word alone (no RAM read needed)? Cross-check with /tmp/pret/pokeemerald src/cable_club.c (gLinkType values: LINKTYPE_TRADE 0x1122? LINKTYPE_BATTLE 0x2211? exact constants from include/link.h).
3. If Celio supports battles: what does its battle section do (what blocks does it exchange — battle party? what per-turn data), and how feasible is porting it onto our existing PacketLayer port? If Celio does NOT support battles: say so plainly and note what its README/issues say about why.
4. The user's goal: seamless AUTO mode switching (one wireless session; the app detects whether the games opened the TRADE or BATTLE counter flow and runs the right synthesis). Given (2), sketch what the detection needs: the linkType word the game announces (from cable_club attendant choice) is in the wire handshake already? or read gLinkType from EWRAM (address from the sym maps: grep gLinkType /tmp/EMERALD.sym /tmp/FIRERED.sym)? Recommend the cleanest signal.
5. Also check upstream Celio for anything about the ROOM-EXIT and TRAINER-CARD interactions (does its tradeLounge handle exit standby / card views? cite code) — its handling is the reference for our two bugs.
Use WebFetch on specific GitHub files once you find the repo (raw.githubusercontent.com URLs work well).`,
    { label: 'research:celio-modes', phase: 'Evidence', schema: REPORT_SCHEMA }),
])

const [join, host, exitProto, card, code, modes] = results
return {
  join: join || 'AGENT FAILED',
  host: host || 'AGENT FAILED',
  exitProto: exitProto || 'AGENT FAILED',
  card: card || 'AGENT FAILED',
  code: code || 'AGENT FAILED',
  modes: modes || 'AGENT FAILED',
}
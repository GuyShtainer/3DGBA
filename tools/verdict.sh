#!/bin/sh
# =============================================================================================
# D7d — POST-RUN NETLOG VERDICT  (SPEC-suite-hardening.md §D7d)
#
#   tools/verdict.sh <run-folder>
#
# <run-folder> holds ONE hardware run's copied logs — the established workflow is: wipe
# sdmc:/cias/netlogs/ BEFORE the run, copy everything out AFTER it, one run = one folder.
# The script reads the logs and prints a verdict per HANDOFF "Next steps #2" checklist item
# (the run-#13 list) plus health lines. It NEVER writes anything.
#
# THE PRIME DIRECTIVE: a missing log, or a field this build never wrote, is **UNKNOWN — never
# PASS**. That rule is the run-#11 lesson made mechanical: that run's JOIN netlog was lost to a
# stray Quit, the room-exit fix went unproven, and two more hardware runs were spent before the
# gap was noticed. Absence of evidence is not evidence.
#
# Every verdict line ends with the LITERAL pattern it used, so a wrong verdict is diagnosable
# from its own output without reading this script.
#
# Exit code: nonzero iff any FAIL. UNKNOWNs exit 0 but are counted in the summary (they are the
# things to go re-run, not the things that broke).
#
# Writers each pattern comes from (do not invent patterns — grep the writer):
#   gbacore.c:936   `# 3DGBA netlog role=%s seat=%d …`
#   gbacore.c:977   `# fprint local=… peer=… verdict=…`   (rendered by fingerprint.c:131/137/138)
#   gbacore.c:1003  `# celio section=… frames=… tradeComplete=… gateN=… resetN=… forceN=…
#                     sioMode=… siocnt=… exitP=… sessEnd=… pCard=… idReal=…`
#   gbacore.c:1027  `# celio-trace f=… cmd=… sec=… st=… blk=…`
#   gbacore.c:1031  `# event txSeq=… txAcked=… rxDelivered=… overflow=… retransmits=…`
#   gamestate.c:320 gs header naming the red-screen cb2 values; :321 the gs CSV column header
#   main.c:106/121/133 the log file names 3DGBA_net_/gs_/touch_<ROLE>_MMDD_HHMMSS.txt
# =============================================================================================

set -u

DIR="${1:-}"
if [ -z "$DIR" ] || [ ! -d "$DIR" ]; then
	echo "usage: tools/verdict.sh <run-folder>    (a folder of ONE run's copied sdmc:/cias/netlogs files)"
	exit 2
fi

FAILS=0
UNKNOWNS=0

# say <item> <verdict> <evidence...>   — one aligned line; counts FAIL/UNKNOWN.
say() {
	item="$1"; verdict="$2"; shift 2
	case "$verdict" in
		FAIL)    FAILS=$((FAILS + 1)) ;;
		UNKNOWN) UNKNOWNS=$((UNKNOWNS + 1)) ;;
	esac
	printf '%-26s %-7s %s\n' "$item" "$verdict" "$*"
}

# newest <prefix>  — the LAST file by name (timestamped names sort chronologically). Sessions
# re-attach, so a folder can hold several per role; the last one is the one that ran the trade.
newest() { ls "$DIR" 2>/dev/null | grep "^$1" | sort | tail -1; }
countof() { ls "$DIR" 2>/dev/null | grep -c "^$1"; }

# celio_line <file> — the `# celio ` header line ONLY (the trailing space excludes `# celio-trace`).
celio_line() { grep -m1 '^# celio ' "$1" 2>/dev/null; }

# fld <line> <key> — value of `key=` as a whole space-delimited token; empty when ABSENT.
fld() { printf '%s\n' "$1" | tr ' ' '\n' | grep "^$2=" | head -1 | cut -d= -f2; }

echo "=== 3DGBA run verdict — $DIR ==="

# ---------------------------------------------------------------------------------------------
# log inventory (presence is itself a verdict — see the prime directive)
# ---------------------------------------------------------------------------------------------
HN=$(newest '3DGBA_net_HOST_'); JN=$(newest '3DGBA_net_JOIN_')
HG=$(newest '3DGBA_gs_HOST_');  JG=$(newest '3DGBA_gs_JOIN_')
printf 'logs host-net=%s join-net=%s host-gs=%s join-gs=%s\n' \
	"${HN:-MISSING}" "${JN:-MISSING}" "${HG:-MISSING}" "${JG:-MISSING}"
printf 'counts net HOST=%s JOIN=%s | gs HOST=%s JOIN=%s | csv=%s wd=%s hang=%s control=%s rec=%s\n' \
	"$(countof '3DGBA_net_HOST_')" "$(countof '3DGBA_net_JOIN_')" \
	"$(countof '3DGBA_gs_HOST_')"  "$(countof '3DGBA_gs_JOIN_')" \
	"$(countof '3DGBA_csv_')" "$(countof '3DGBA_wd_')" "$(countof '3DGBA_hang_')" \
	"$(countof '3DGBA_control_')" "$(countof '3DGBA_rec_')"
echo "---------------------------------------------------------------"

HNF=""; JNF=""; HC=""; JC=""
[ -n "$HN" ] && HNF="$DIR/$HN" && HC=$(celio_line "$HNF")
[ -n "$JN" ] && JNF="$DIR/$JN" && JC=$(celio_line "$JNF")

# ---------------------------------------------------------------------------------------------
# 13a — the trade completes on BOTH consoles      (checklist #2a: "trade as usual — must still work")
# ---------------------------------------------------------------------------------------------
if [ -z "$HNF" ] || [ -z "$JNF" ]; then
	say "13a trade-completes" UNKNOWN "a net log is MISSING (host='${HN:-none}' join='${JN:-none}') [pattern: ^# celio .*tradeComplete=]"
elif [ -z "$HC" ] || [ -z "$JC" ]; then
	say "13a trade-completes" UNKNOWN "no '# celio ' header in one of the logs (build predates it) [pattern: ^# celio ]"
else
	hT=$(fld "$HC" tradeComplete); jT=$(fld "$JC" tradeComplete)
	hP=$(fld "$HC" partnerPartyB); jP=$(fld "$JC" partnerPartyB)
	hR=$(grep -m1 '^# event ' "$HNF" | tr ' ' '\n' | grep '^rxDelivered=' | cut -d= -f2)
	jR=$(grep -m1 '^# event ' "$JNF" | tr ' ' '\n' | grep '^rxDelivered=' | cut -d= -f2)
	if [ -z "$hT" ] || [ -z "$jT" ]; then
		say "13a trade-completes" UNKNOWN "tradeComplete field absent [pattern: tradeComplete=]"
	elif [ "$hT" = "1" ] && [ "$jT" = "1" ]; then
		say "13a trade-completes" PASS "HOST+JOIN tradeComplete=1 (partnerPartyB=$hP/$jP rxDelivered=${hR:-?}/${jR:-?}) [pattern: tradeComplete=1]"
	else
		side="HOST"; [ "$hT" = "1" ] && side="JOIN"
		say "13a trade-completes" FAIL "$side did not complete (HOST=$hT JOIN=$jT partnerPartyB=$hP/$jP) [pattern: tradeComplete=1]"
	fi
fi

# ---------------------------------------------------------------------------------------------
# 13b — the friend's trainer card                 (checklist #2b)
# The RENDER is not log-provable: the log can only say WHICH card was served. Never auto-PASS the
# "a card renders" half — it always prints needs-eyes.
# ---------------------------------------------------------------------------------------------
if [ -z "$HC" ] && [ -z "$JC" ]; then
	say "13b trainer-card" UNKNOWN "no '# celio ' header [pattern: ^# celio .*idReal=]"
else
	hI=$(fld "$HC" idReal); jI=$(fld "$JC" idReal)
	hK=$(fld "$HC" pCard);  jK=$(fld "$JC" pCard)
	if [ -z "$hI" ] && [ -z "$jI" ]; then
		say "13b trainer-card" UNKNOWN "no idReal= field — this build predates the run-#12 card work [pattern: idReal=]"
	elif [ "${hI:-0}" = "1" ] || [ "${jI:-0}" = "1" ]; then
		say "13b trainer-card" PASS "real identity served (idReal=${hI:-?}/${jI:-?} pCard=${hK:-?}/${jK:-?}) — needs-eyes: the card must also RENDER [pattern: idReal=1]"
	else
		say "13b trainer-card" INFO "canned card expected on a first-ever linkup (idReal=${hI:-?}/${jI:-?} pCard=${hK:-?}/${jK:-?}) — needs-eyes: confirm a well-formed card renders, then link again for the real one [pattern: idReal=0]"
	fi
fi

# ---------------------------------------------------------------------------------------------
# 13c — room exit, both games leave, no black screen   (checklist #2c)
# ---------------------------------------------------------------------------------------------
if [ -z "$HC" ] || [ -z "$JC" ]; then
	say "13c room-exit" UNKNOWN "a net log / '# celio ' header is missing [pattern: exitP= sessEnd=]"
else
	hE=$(fld "$HC" exitP); jE=$(fld "$JC" exitP)
	hS=$(fld "$HC" sessEnd); jS=$(fld "$JC" sessEnd)
	if [ -z "$hE" ] || [ -z "$jE" ]; then
		say "13c room-exit" UNKNOWN "no exitP=/sessEnd= fields — pre-run-#12 build, the exit fix cannot be judged [pattern: exitP=]"
	elif [ "$hE" = "1" ] && [ "$jE" = "1" ] && [ "$hS" = "1" ] && [ "$jS" = "1" ]; then
		say "13c room-exit" PASS "both consoles exited and ended the session (exitP=1/1 sessEnd=1/1) [pattern: exitP=1 sessEnd=1]"
	elif [ "$hE" = "1" ] || [ "$jE" = "1" ]; then
		say "13c room-exit" FAIL "exit started but was not answered on both sides (exitP=$hE/$jE sessEnd=$hS/$jS) [pattern: exitP=1 sessEnd=1]"
	else
		say "13c room-exit" UNKNOWN "no exit was ever committed (exitP=0/0) — was the room exit actually walked? [pattern: exitP=1]"
	fi
fi

# ---------------------------------------------------------------------------------------------
# 13d — re-link after the exit                     (checklist #2d)
# The tool cannot tell "re-link not attempted" from "re-link failed" (SPEC Open Question 6) —
# so a zero here is UNKNOWN with that caveat printed, never FAIL.
# ---------------------------------------------------------------------------------------------
if [ -z "$HC" ] && [ -z "$JC" ]; then
	say "13d re-link-after-exit" UNKNOWN "no '# celio ' header [pattern: resetN=]"
else
	hRs=$(fld "$HC" resetN); jRs=$(fld "$JC" resetN)
	freshSetup=0
	for f in "$HNF" "$JNF"; do
		[ -n "$f" ] || continue
		# a trace edge back to sec=0 (SETUP) AFTER a later section == a fresh establishment
		if awk '/^# celio-trace /{ for(i=1;i<=NF;i++) if ($i ~ /^sec=/) { s=substr($i,5)+0;
		         if (s>0) seen=1; if (seen && s==0) { found=1 } } } END { exit(found?0:1) }' "$f"; then
			freshSetup=1
		fi
	done
	if [ -z "$hRs" ] && [ -z "$jRs" ]; then
		say "13d re-link-after-exit" UNKNOWN "no resetN= field on this build [pattern: resetN=]"
	elif [ "${hRs:-0}" -gt 0 ] 2>/dev/null || [ "${jRs:-0}" -gt 0 ] 2>/dev/null; then
		say "13d re-link-after-exit" PASS "a session reset happened (resetN=${hRs:-?}/${jRs:-?}, fresh-SETUP trace edge=$freshSetup) [pattern: resetN=[1-9]]"
	else
		say "13d re-link-after-exit" UNKNOWN "resetN=0 on both — the tool cannot tell 'not attempted' from 'failed'; if you DID walk back to the counter, this is a FAIL [pattern: resetN=[1-9]]"
	fi
fi

# ---------------------------------------------------------------------------------------------
# health: the event plane must never silently drop (netlink.h reliability invariant)
# ---------------------------------------------------------------------------------------------
if [ -z "$HNF" ] && [ -z "$JNF" ]; then
	say "hp event-channel" UNKNOWN "no net log [pattern: ^# event .*overflow=]"
else
	ovBad=""; ovSeen=0
	for f in "$HNF" "$JNF"; do
		[ -n "$f" ] || continue
		e=$(grep -m1 '^# event ' "$f")
		[ -n "$e" ] || continue
		ovSeen=1
		ov=$(printf '%s\n' "$e" | tr ' ' '\n' | grep '^overflow=' | cut -d= -f2)
		rt=$(printf '%s\n' "$e" | tr ' ' '\n' | grep '^retransmits=' | cut -d= -f2)
		[ "${ov:-0}" != "0" ] && ovBad="$ovBad $(basename "$f"):overflow=$ov"
		ovInfo="${ovInfo:-}$(basename "$f"):ov=${ov:-?}/rt=${rt:-?} "
	done
	if [ "$ovSeen" = "0" ]; then
		say "hp event-channel" UNKNOWN "no '# event ' line [pattern: ^# event ]"
	elif [ -n "$ovBad" ]; then
		say "hp event-channel" FAIL "the event queue OVERFLOWED —$ovBad [pattern: overflow=0]"
	else
		say "hp event-channel" PASS "${ovInfo:-}[pattern: overflow=0]"
	fi
fi

# ---------------------------------------------------------------------------------------------
# health: the edge-gate wedge escape (run #6 precedent — forceN=21 with a healthy session => WARN)
# ---------------------------------------------------------------------------------------------
if [ -z "$HC" ] && [ -z "$JC" ]; then
	say "hp wedge-escape" UNKNOWN "no '# celio ' header [pattern: forceN=]"
else
	hF=$(fld "$HC" forceN); jF=$(fld "$JC" forceN)
	if [ -z "$hF" ] && [ -z "$jF" ]; then
		say "hp wedge-escape" UNKNOWN "no forceN= field on this build [pattern: forceN=]"
	elif [ "${hF:-0}" = "0" ] && [ "${jF:-0}" = "0" ]; then
		say "hp wedge-escape" PASS "the capture gate never wedged (forceN=0/0) [pattern: forceN=0]"
	else
		say "hp wedge-escape" WARN "the gate wedged but escaped (forceN=${hF:-?}/${jF:-?}) — healthy per run #6, watch it [pattern: forceN=[1-9]]"
	fi
fi

# ---------------------------------------------------------------------------------------------
# health: the game's own link error / red screen  (gs log — gamestate.c:320-321,338)
# cb2 0800B1A0 = CB2_PrintErrorMessage (Emerald), 0800AF2C = FireRed. lerr = gLinkErrorOccurred.
# Columns are located BY NAME from the gs CSV header, so a column added later cannot shift us.
# ---------------------------------------------------------------------------------------------
gs_check() {
	f="$1"; who="$2"
	[ -n "$f" ] || { echo "MISSING"; return; }
	awk -F, '
		/^idx,frame,scr,/ { for (i = 1; i <= NF; i++) { if ($i == "lerr") L = i; if ($i == "cb2") C = i } next }
		/^[0-9]/ {
			if (L && $L == 1)                                   { print "ERR lerr=1 row " $1; exit }
			if (C && ($C == "0800B1A0" || $C == "0800AF2C"))    { print "ERR cb2=" $C " row " $1; exit }
		}
		END { }
	' "$f"
}
if [ -z "$HG" ] && [ -z "$JG" ]; then
	say "hp link-error" UNKNOWN "no gs log in the folder — the game's own error flag is unobserved [pattern: lerr / cb2=0800B1A0|0800AF2C]"
else
	hit=""
	for pair in "HOST:$HG" "JOIN:$JG"; do
		who=${pair%%:*}; nm=${pair#*:}
		[ -n "$nm" ] || continue
		r=$(gs_check "$DIR/$nm" "$who")
		[ -n "$r" ] && hit="$hit $who($nm): $r;"
	done
	if [ -n "$hit" ]; then
		say "hp link-error" FAIL "the GAME flagged a link error —$hit [pattern: lerr==1 or cb2=0800B1A0|0800AF2C]"
	else
		say "hp link-error" PASS "no lerr=1 row and no red-screen cb2 in ${HG:-–}/${JG:-–} [pattern: lerr==1 or cb2=0800B1A0|0800AF2C]"
	fi
fi

# ---------------------------------------------------------------------------------------------
# health: the D6 link-surface fingerprint — were the two consoles even running the same build?
# ---------------------------------------------------------------------------------------------
if [ -z "$HNF" ] && [ -z "$JNF" ]; then
	say "hp link-surface" UNKNOWN "no net log [pattern: ^# fprint .*verdict=]"
else
	fv=""; fseen=0
	for f in "$HNF" "$JNF"; do
		[ -n "$f" ] || continue
		l=$(grep -m1 '^# fprint ' "$f")
		[ -n "$l" ] || continue
		fseen=1
		v=$(printf '%s\n' "$l" | tr ' ' '\n' | grep '^verdict=' | cut -d= -f2-)
		fv="$fv $(basename "$f"):$v"
	done
	# REFUSE-GRADE vs FORENSIC fields (review fix 2026-08-03). A DIFF is NOT automatically a failed
	# run: fingerprint.h:41-46 says gameCode and gameRev diffs are legitimate by design — Emerald
	# trades with FireRed, and FR rev0 with rev1, on real hardware, which is precisely why no ROM
	# CRC is in the surface (the gen1recomp #511 false-incompatibility lesson). The user's own
	# run-#13 setup is EM <-> FR rev1, so failing on any DIFF scored every real run a failure.
	# Only OUR protocol fields mean the two builds cannot interoperate: clProtoRev / netProto /
	# modeFlags.
	if [ "$fseen" = "0" ]; then
		say "hp link-surface" UNKNOWN "no '# fprint ' line — build predates D6 [pattern: ^# fprint ]"
	elif printf '%s' "$fv" | grep -qE 'clProtoRev\(|netProto\(|modeFlags\('; then
		say "hp link-surface" FAIL "the two consoles' PROTOCOL surfaces DIFFER —$fv — the builds cannot interoperate [pattern: verdict=DIFF:...clProtoRev|netProto|modeFlags]"
	elif printf '%s' "$fv" | grep -q 'DIFF'; then
		say "hp link-surface" PASS "$fv — game-side diff only (gameCode/gameRev are forensic, never refuse-grade: EM<->FR and FR rev0<->rev1 trade for real) [pattern: verdict=DIFF with no protocol field]"
	elif printf '%s' "$fv" | grep -q 'unknown'; then
		say "hp link-surface" UNKNOWN "the peer surface never arrived —$fv [pattern: verdict=unknown]"
	else
		say "hp link-surface" PASS "$fv [pattern: verdict=MATCH]"
	fi
fi

# ---------------------------------------------------------------------------------------------
# health: the D1/D2 freeze artifacts — their mere EXISTENCE is the finding
# ---------------------------------------------------------------------------------------------
wd=$(countof '3DGBA_wd_'); hang=$(countof '3DGBA_hang_')
if [ "$wd" = "0" ] && [ "$hang" = "0" ]; then
	say "hp freeze-artifacts" PASS "no watchdog and no hang dump was written [pattern: 3DGBA_wd_* / 3DGBA_hang_*]"
else
	say "hp freeze-artifacts" FAIL "$wd watchdog + $hang hang dump(s) — read them first, they name the parked loop [pattern: 3DGBA_wd_* / 3DGBA_hang_*]"
fi

echo "---------------------------------------------------------------"
printf 'summary: %d FAIL, %d UNKNOWN\n' "$FAILS" "$UNKNOWNS"
[ "$UNKNOWNS" -gt 0 ] && echo "note: UNKNOWN is not PASS — it is the list of things this run did not prove."
exit $([ "$FAILS" -gt 0 ] && echo 1 || echo 0)

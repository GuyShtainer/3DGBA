---
name: handoff
description: >-
  Save the current chat's work on a project into a durable, resumable per-project
  handoff document — and resume from one in a fresh chat. Use this skill whenever the
  user wants to checkpoint, save progress, "summarize where we are", wrap up, hand off,
  or close out a chat for a project; AND whenever a new chat should pick up, resume,
  continue, "start where we left off", or catch up on a project. Especially in a
  multi-repo workspace (a foundation repo plus per-tool repos), it writes the handoff
  into the repo that owns the work, so each project's state lives with that project and
  survives across chats. Reach for it proactively at the end of a substantive session,
  even if the user doesn't name the skill.
---

# Handoff — save & resume a project's session

Capture everything this chat established about a project — goal, status, decisions, where
things live, blockers, next steps, how to build/run — into one living **per-project
`HANDOFF.md`**, so the chat can be closed and a future chat resumes exactly where this one
stopped. Also reads an existing handoff to resume. This complements (does not replace) any
auto-memory and any hand-curated `SESSION_SUMMARY.md`.

## 1. Decide the mode

- **SAVE** (default) — the user is wrapping up / checkpointing / "save where we are", or you've
  done substantive work worth preserving. Write/update the handoff.
- **RESUME** — the user says "resume", "where did we leave off", "continue project X", or is
  clearly starting fresh. Read the latest handoff, brief them, then continue the work.

When ambiguous: if there is unsaved session context worth preserving → SAVE; if a handoff already
exists and little new happened → RESUME.

## 2. Identify the project (multi-repo aware)

The handoff belongs to the repo that owns the changed work, not "the current directory":

```bash
git -C <dir-of-the-work> rev-parse --show-toplevel   # repo root that owns it
git -C <repo> branch --show-current
```

In a foundation-plus-tools layout, a sub-tool under `projects/<name>/` is its **own repo** (the
foundation often git-ignores `/projects/`), and the foundation root is a separate repo. **If this
chat touched more than one project, handle each separately and tell the user which handoff went
where** — one `HANDOFF.md` per repo.

## 3. SAVE — write/update the handoff

1. **Locate the managed file:** `<repo-root>/docs/HANDOFF.md` if a `docs/` dir exists, else
   `<repo-root>/HANDOFF.md`. This is the one file this skill owns. Do **not** overwrite a curated
   `SESSION_SUMMARY.md` or similar — link to it from the handoff instead.
2. **Gather ground truth + chat context.** Pull the real state, don't rely on memory alone:
   ```bash
   git -C <repo> log --oneline -15
   git -C <repo> status --short
   ```
   Then from THIS chat collect: the overall goal and this session's intent (quote the user where
   it pins intent), what is **Done** (with commit hashes), **In progress** (and exactly where),
   **Not started**; the **decisions and their rationale**; the **key files/functions** touched
   (as `file:symbol` pointers); **blockers / things needing the user** (e.g. hardware sign-off, a
   pending answer); the ordered **next steps** to resume; the **build/test/run** commands; and any
   **project-specific gotchas** to remember.
3. **Fill the template** at `assets/handoff-template.md`. Rules:
   - Keep the **top two sections — "Current status" and "Next steps" — always current.** That is
     the resume point; a reader with zero memory of the chat must be able to continue from them
     alone.
   - **Prepend** a dated `### Session — <YYYY-MM-DD>` entry to the **Session log**; never delete
     old log entries.
   - If `HANDOFF.md` already exists, **update** the top sections in place and prepend the new log
     entry — don't start over.
4. Write only this managed file. Show the user the path and the refreshed top section. Do **not**
   `git commit` unless the user asks (offer it).

## 4. RESUME — read & brief

1. Find the handoff (`docs/HANDOFF.md` or `HANDOFF.md`) in the relevant repo. If several projects
   have one and it's unclear which, list them and ask.
2. Read it; follow its pointers (open the cited commits/files) as needed to reload context.
3. Brief the user crisply: the goal, where we stopped, and the immediate next steps from the top
   section — then continue the work.

## 5. Quality rules

- **Resumable, not a transcript.** Optimize the top section so future-you can act immediately;
  push narrative into the session log.
- **Absolute dates, real hashes, real names.** Convert "yesterday" to a date; quote commit hashes;
  name `file:symbol`, not vague areas.
- **Record the WHY** behind each decision — the diff can't reconstruct intent.
- **Keep it current.** Every SAVE refreshes status + next steps and appends one session entry.
- **Portable.** This skill works in any repo; it writes into whichever project owns the work, with
  no hardcoded paths. Copy the whole `handoff/` folder into another repo's `.claude/skills/` to use
  it there.

## Template

`assets/handoff-template.md` — the structure to fill in. Read it before writing a fresh handoff.

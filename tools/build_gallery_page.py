#!/usr/bin/env python3
"""Rebuild the audit page's galleries: grouped + collapsible, with a sticky quick-nav,
and every embedded image re-encoded to LOSSLESS WebP (identical pixels, ~41% the bytes).

Idempotent — safe for the 2-hourly loop republish."""
import base64, os, re, subprocess, hashlib

P = "/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA"
EV = f"{P}/docs/phase21-touch-census/evidence"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = f"{HERE}/3dgba-ui-audit.html"
CACHE = f"{HERE}/webpcache"
MARK_START, MARK_END = "<!--GALLERY-START-->", "<!--GALLERY-END-->"
CSS_START, CSS_END = "/*GNAV-CSS-START*/", "/*GNAV-CSS-END*/"
os.makedirs(CACHE, exist_ok=True)

# ---------------------------------------------------------------- image encoding
def webp_uri(png_path):
    """Lossless WebP data URI, cached by content hash."""
    with open(png_path, "rb") as f:
        raw = f.read()
    key = hashlib.sha1(raw).hexdigest()[:16]
    dst = f"{CACHE}/{key}.webp"
    if not os.path.exists(dst):
        try:
            subprocess.run(["cwebp", "-lossless", "-z", "9", "-quiet", png_path, "-o", dst],
                           check=True, capture_output=True)
        except Exception:
            return "data:image/png;base64," + base64.b64encode(raw).decode()
    with open(dst, "rb") as f:
        return "data:image/webp;base64," + base64.b64encode(f.read()).decode()

def shrink_inline_pngs(html):
    """Re-encode the hand-authored sections' base64 PNGs to lossless WebP in place."""
    def sub(m):
        b64 = m.group(1)
        key = hashlib.sha1(b64.encode()).hexdigest()[:16]
        dst, src = f"{CACHE}/inline_{key}.webp", f"{CACHE}/inline_{key}.png"
        if not os.path.exists(dst):
            try:
                with open(src, "wb") as f:
                    f.write(base64.b64decode(b64))
                subprocess.run(["cwebp", "-lossless", "-z", "9", "-quiet", src, "-o", dst],
                               check=True, capture_output=True)
                os.remove(src)
            except Exception:
                return m.group(0)
        with open(dst, "rb") as f:
            return "data:image/webp;base64," + base64.b64encode(f.read()).decode()
    return re.sub(r'data:image/png;base64,([A-Za-z0-9+/=]+)', sub, html)

# ---------------------------------------------------------------- evidence collection
def shots(sub, maxbytes=400_000):
    """One entry per screen id (prefer the bottom screen — that is the touch surface)."""
    d = f"{EV}/{sub}"
    if not os.path.isdir(d):
        return []
    ids = {}
    for f in sorted(os.listdir(d)):
        m = re.match(r"(.+?)\.(top|bottom)\.png$", f)
        if m:
            sid, which = m.groups()
            if sid not in ids or which == "bottom":
                ids[sid] = f
        elif f.endswith(".png"):
            ids.setdefault(f[:-4], f)
    out = []
    for sid, f in sorted(ids.items()):
        p = os.path.join(d, f)
        if os.path.getsize(p) <= maxbytes:
            out.append((sid, p))
    return out

# The census ids carry the catalog's own section letter (CATALOG.md headings A..L).
CENSUS_SECTIONS = [
    ("A", "Boot, title & file select"),
    ("B", "Overworld & field"),
    ("C", "Battle"),
    ("D", "Battle Frontier"),
    ("E", "Core menus — bag, party, summary, PC"),
    ("F", "Pokédex"),
    ("G", "PokéNav"),
    ("H", "Contests, Pokéblocks & TV"),
    ("I", "Shops & money games"),
    ("J", "Link, wireless & Mystery Gift"),
    ("K", "FR/LG systems"),
    ("L", "Endgame & credits"),
]

IMPL_RULES = [  # (regex on the id, group title) — first match wins
    (r"-P24-G",        "PC storage — the box, worked by touch"),
    (r"^..-naming",    "Naming keyboard — typed by touch alone"),
    (r"^..-(bag|dex)", "Lists — bag rows, Pokédex, search chips"),
    (r"^..-page",      "Summary pages — edge taps to turn"),
    (r"^..-traversal", "Traversal — tap to route"),
    (r"^..-dlg",       "Dialogs & options — tap to advance"),
]

def group_census(rows):
    groups = []
    for letter, title in CENSUS_SECTIONS:
        sel = [r for r in rows if r[0][:1] == letter]
        if sel:
            groups.append((f"{letter} · {title}", sel))
    known = {l for l, _ in CENSUS_SECTIONS}
    rest = [r for r in rows if r[0][:1] not in known]
    if rest:
        groups.append(("Other", rest))
    return groups

def group_impl(rows):
    buckets, order = {}, []
    for sid, p in rows:
        title = "Other proofs"
        for rx, t in IMPL_RULES:
            if re.search(rx, sid):
                title = t
                break
        if title not in buckets:
            buckets[title] = []
            order.append(title)
        buckets[title].append((sid, p))
    pref = [t for _, t in IMPL_RULES] + ["Other proofs"]
    return [(t, buckets[t]) for t in pref if t in buckets]

# ---------------------------------------------------------------- markup
def cells(rows):
    return "\n".join(
        f'<figure class="cshot"><img src="{webp_uri(p)}" alt="{sid}" loading="lazy" />'
        f'<figcaption>{sid}</figcaption></figure>' for sid, p in rows)

def group_block(title, rows, open_first=False):
    return (f'<details class="ggroup"{" open" if open_first else ""}>'
            f'<summary><span class="gtitle">{title}</span>'
            f'<span class="gcount">{len(rows)}</span></summary>'
            f'<div class="cgrid">\n{cells(rows)}\n</div></details>')

def gallery(groups, first_open=True):
    return "\n".join(group_block(t, r, open_first=(first_open and i == 0))
                     for i, (t, r) in enumerate(groups))

impl   = group_impl(shots("impl"))
firsts = [("Ruby & Sapphire — first boots", shots("ruby")),
          ("LeafGreen — first boot",        shots("leafgreen")),
          ("Ruby + Sapphire co-op, one console", shots("rs-coop"))]
firsts = [(t, r) for t, r in firsts if r]
em = group_census(shots("emerald"))
fr = group_census(shots("firered"))

n_impl   = sum(len(r) for _, r in impl)
n_firsts = sum(len(r) for _, r in firsts)
n_em     = sum(len(r) for _, r in em)
n_fr     = sum(len(r) for _, r in fr)

section = f"""{MARK_START}
  <h2 id="impl">Touch, actually working</h2>
  <p class="h2note">
    Proof captures from the implementation runs — the naming keyboard typed by touch alone, list
    drag-scrolling, the PC box worked by touch — plus the first-ever Ruby, Sapphire and LeafGreen
    boots. <strong>{n_impl}</strong> implementation proofs, <strong>{n_firsts}</strong> first boots.
    Sections open on click.
  </p>
  <div class="gtools"><button class="gbtn" data-scope="impl-wrap" data-act="open">Open all</button>
    <button class="gbtn" data-scope="impl-wrap" data-act="close">Close all</button></div>
  <div id="impl-wrap">
{gallery(impl)}
{gallery(firsts, first_open=False)}
  </div>

  <h2 id="census">The screen census</h2>
  <p class="h2note">
    An automated sweep visited every reachable screen, photographing both 3DS screens and
    identifying each by reading the game's own callback pointer and resolving it against the
    decompilation's symbols — certain identification, not guesswork. Grouped by the catalog's own
    sections. <strong>{n_em}</strong> Emerald and <strong>{n_fr}</strong> FireRed screens.
  </p>
  <div class="gtools"><button class="gbtn" data-scope="census-wrap" data-act="open">Open all</button>
    <button class="gbtn" data-scope="census-wrap" data-act="close">Close all</button></div>
  <div id="census-wrap">
    <h3 class="gsub" id="census-em">Emerald <span class="gsubn">covers Ruby / Sapphire</span></h3>
{gallery(em)}
    <h3 class="gsub" id="census-fr">FireRed <span class="gsubn">covers LeafGreen</span></h3>
{gallery(fr, first_open=False)}
  </div>
{MARK_END}
"""

# ---------------------------------------------------------------- quick-nav
NAV = [("sharp", "Sharp text"), ("roles", "Type roles"), ("coop", "Co-op"),
       ("earlier", "Earlier fixes"), ("impl", "Touch proofs"),
       ("census", "Screen census"), ("status", "Status")]

nav_html = ('<nav class="qnav" aria-label="Sections"><div class="qnav-in">'
            + "".join(f'<a href="#{i}">{t}</a>' for i, t in NAV)
            + '</div></nav>')

CSS = f"""{CSS_START}
  .qnav {{ position:sticky; top:10px; z-index:20; margin:34px 0 8px; }}
  .qnav-in {{ display:flex; gap:2px; overflow-x:auto; scrollbar-width:none; padding:5px 6px;
              background:color-mix(in srgb,var(--panel) 82%,transparent); backdrop-filter:blur(10px);
              border:1px solid var(--line); border-radius:999px; box-shadow:var(--shadow); }}
  .qnav-in::-webkit-scrollbar {{ display:none; }}
  .qnav a {{ flex:none; text-decoration:none; color:var(--dim); font-family:var(--mono);
             font-size:10.5px; letter-spacing:.13em; text-transform:uppercase; white-space:nowrap;
             padding:6px 11px; border-radius:999px; border:1px solid transparent; }}
  .qnav a:hover {{ color:var(--text); background:color-mix(in srgb,var(--acc) 10%,transparent); }}
  .qnav a.on {{ color:var(--acc); border-color:color-mix(in srgb,var(--acc) 40%,transparent);
                background:color-mix(in srgb,var(--acc) 12%,transparent); }}
  .qnav a:focus-visible {{ outline:2px solid var(--acc); outline-offset:2px; }}
  html {{ scroll-behavior:smooth; }}
  @media (prefers-reduced-motion:reduce) {{ html {{ scroll-behavior:auto; }} }}
  [id] {{ scroll-margin-top:72px; }}

  .gtools {{ display:flex; gap:8px; margin:0 0 14px; }}
  .gbtn {{ font-family:var(--mono); font-size:10px; letter-spacing:.12em; text-transform:uppercase;
           color:var(--dim); background:var(--panel); border:1px solid var(--line);
           border-radius:999px; padding:6px 13px; cursor:pointer; }}
  .gbtn:hover {{ color:var(--acc); border-color:color-mix(in srgb,var(--acc) 45%,transparent); }}
  .gbtn:focus-visible {{ outline:2px solid var(--acc); outline-offset:2px; }}

  .ggroup {{ background:var(--panel); border:1px solid var(--line); border-radius:12px;
             margin:0 0 8px; box-shadow:var(--shadow); overflow:hidden; }}
  .ggroup > summary {{ display:flex; align-items:center; gap:10px; cursor:pointer; padding:13px 16px;
                       font-family:var(--mono); font-size:11.5px; letter-spacing:.06em; color:var(--text);
                       list-style:none; }}
  .ggroup > summary::-webkit-details-marker {{ display:none; }}
  .ggroup > summary::before {{ content:"+"; flex:none; width:15px; color:var(--acc);
                               font-size:13px; text-align:center; }}
  .ggroup[open] > summary::before {{ content:"–"; }}
  .ggroup[open] > summary {{ border-bottom:1px solid var(--line); }}
  .ggroup > summary:focus-visible {{ outline:2px solid var(--acc); outline-offset:-2px; }}
  .gtitle {{ flex:1 1 auto; }}
  .gcount {{ flex:none; font-size:10px; color:var(--dim); font-variant-numeric:tabular-nums;
             border:1px solid var(--line); border-radius:999px; padding:2px 8px; }}
  .ggroup .cgrid {{ padding:14px 16px 16px; }}

  .cgrid {{ display:grid; gap:10px; grid-template-columns:repeat(auto-fill,minmax(168px,1fr)); }}
  .cshot {{ margin:0; background:var(--panel2); border:1px solid var(--line); border-radius:8px;
            overflow:hidden; }}
  .cshot img {{ width:100%; display:block; image-rendering:pixelated; background:#000; }}
  .cshot figcaption {{ padding:5px 8px; font-family:var(--mono); font-size:9.5px; color:var(--dim);
                       letter-spacing:.04em; overflow:hidden; text-overflow:ellipsis;
                       white-space:nowrap; }}
  .gsub {{ font-size:12px; font-family:var(--mono); letter-spacing:.14em; text-transform:uppercase;
           color:var(--dim); margin:26px 0 10px; display:flex; align-items:baseline; gap:10px; }}
  .gsubn {{ font-size:10px; letter-spacing:.08em; text-transform:none; opacity:.75; }}
{CSS_END}"""

JS = """<script>
(function () {
  document.querySelectorAll('.gbtn').forEach(function (b) {
    b.addEventListener('click', function () {
      var host = document.getElementById(b.dataset.scope);
      if (!host) return;
      var want = b.dataset.act === 'open';
      host.querySelectorAll('details.ggroup').forEach(function (d) { d.open = want; });
    });
  });
  var links = [].slice.call(document.querySelectorAll('.qnav a'));
  var targets = links.map(function (a) { return document.getElementById(a.hash.slice(1)); });
  function mark() {
    var best = 0, top = 1e9;
    targets.forEach(function (t, i) {
      if (!t) return;
      var d = Math.abs(t.getBoundingClientRect().top - 70);
      if (t.getBoundingClientRect().top < window.innerHeight * 0.6 && d < top) { top = d; best = i; }
    });
    links.forEach(function (a, i) { a.classList.toggle('on', i === best); });
  }
  addEventListener('scroll', mark, { passive: true });
  mark();
})();
</script>"""

# ---------------------------------------------------------------- assemble
html = open(OUT, encoding="utf-8").read()

# 1. gallery block (idempotent)
if MARK_START in html:
    html = re.sub(re.escape(MARK_START) + r".*?" + re.escape(MARK_END), "", html, flags=re.S)
anchor = "\n  <h2>Honest status</h2>"
anchor2 = '\n  <h2 id="status">Honest status</h2>'
if anchor in html:
    html = html.replace(anchor, "\n" + section + anchor2, 1)
elif anchor2 in html:
    html = html.replace(anchor2, "\n" + section + anchor2, 1)
else:
    raise SystemExit("status anchor missing")

# 2. ids on the hand-authored sections, for the nav
for pat, hid in [(r'<h2>(Sharp text[^<]*)</h2>', 'sharp'),
                 (r'<h2>(Every text role[^<]*)</h2>', 'roles'),
                 (r'<h2>(Co-op, working[^<]*)</h2>', 'coop'),
                 (r'<h2>(Earlier fixes[^<]*)</h2>', 'earlier')]:
    html = re.sub(pat, lambda m, h=hid: f'<h2 id="{h}">{m.group(1)}</h2>', html, count=1)

# 3. sticky nav immediately before the first content section (once)
if 'class="qnav"' not in html:
    if '<h2 id="sharp">' not in html:
        raise SystemExit("nav anchor missing — the #sharp id was not applied")
    html = html.replace('<h2 id="sharp">', nav_html + '\n\n  <h2 id="sharp">', 1)

# 4. CSS (replaceable block)
if CSS_START in html:
    html = re.sub(re.escape(CSS_START) + r".*?" + re.escape(CSS_END), CSS, html, flags=re.S)
else:
    # drop the previous ad-hoc gallery css, then append the managed block
    html = re.sub(r'\n\s*\.cgrid \{.*?\.gsub \{[^}]*\}\n', "\n", html, flags=re.S)
    html = html.replace("</style>", CSS + "\n</style>", 1)

# 5. JS (once, before </body> equivalent — the file has no body tag, so append)
html = re.sub(r'<script>\n\(function \(\) \{\n  document\.querySelectorAll\(\'\.gbtn\'\).*?</script>',
              "", html, flags=re.S)
html = html.rstrip() + "\n" + JS + "\n"

# 6. lossless WebP for every remaining inline PNG
before = len(html)
html = shrink_inline_pngs(html)

open(OUT, "w", encoding="utf-8").write(html)
print(f"impl {n_impl} in {len(impl)} groups · firsts {n_firsts} · EM {n_em} in {len(em)} · "
      f"FR {n_fr} in {len(fr)}")
print(f"page {before//1024} KB -> {os.path.getsize(OUT)//1024} KB")

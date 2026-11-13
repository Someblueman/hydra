"""The contact sheet: every captured state, every variant, before/after."""

from __future__ import annotations

import html
import json
from pathlib import Path

import render

STYLE = """
body{font:15px/1.4 system-ui,sans-serif;margin:24px;background:#f4f4f2;color:#222}
h1{font-size:22px}h2{font-size:18px;margin:36px 0 4px;border-bottom:1px solid #ccc}
h3{font-size:14px;margin:18px 0 2px}
.meta{color:#555;font-size:13px}
.variants{display:flex;flex-wrap:wrap;gap:14px;align-items:flex-start}
.variant{max-width:100%}
.variant .label{font-size:12px;color:#444;margin-bottom:2px}
pre.screen{font:11px/1.25 "DejaVu Sans Mono",Menlo,Consolas,monospace;padding:6px;border-radius:4px;margin:0;overflow:auto;white-space:pre}
.cursor{outline:1px solid #e5a000}
.pair{display:flex;gap:8px}
.pair>div{flex:1 1 0;min-width:0}
.pair .tag{font-size:11px;font-weight:600;margin-bottom:2px}
.same{color:#2a7}.changed{color:#c40}
.findings{font-size:12px;margin:4px 0 0 0;padding-left:18px}
.findings .new{color:#b00}.findings .known{color:#775}
table.summary{border-collapse:collapse;font-size:13px}
table.summary td,table.summary th{border:1px solid #ccc;padding:3px 8px;text-align:left}
nav a{margin-right:10px;font-size:13px}
details summary{cursor:pointer}
"""


def load_screen(directory: Path, identifier: str) -> dict | None:
    path = directory / "captures" / f"{identifier}.json"
    if not path.exists():
        return None
    return json.loads(path.read_text())


def same_cells(a: dict, b: dict) -> bool:
    return a["cells"] == b["cells"]


def write(output: Path, manifest: dict, findings: dict, baseline: Path | None) -> None:
    renders = output / "renders"
    renders.mkdir(exist_ok=True)
    by_capture: dict[str, list[dict]] = {}
    for f in findings["findings"]:
        by_capture.setdefault(f["capture"], []).append(f)
    states: dict[tuple[str, str], list[dict]] = {}
    for entry in manifest["captures"]:
        states.setdefault((entry["flow"], entry["state"]), []).append(entry)
    flows_seen = []
    for flow, _ in states:
        if flow not in flows_seen:
            flows_seen.append(flow)

    parts = [
        "<!doctype html><meta charset=\"utf-8\"><title>Hydra visual review</title>",
        f"<style>{STYLE}</style>",
        "<h1>Hydra visual review: installed native TUI, fixture states</h1>",
        f"<p class=meta>commit {html.escape(manifest['commit'])} · {len(manifest['captures'])} captures in "
        f"{manifest['seconds']}s · lint: {len(findings['findings'])} findings, {findings['known']} known, "
        f"{findings['new']} new"
        + (f" · baseline: {html.escape(str(baseline))}" if baseline else "")
        + "</p>",
        "<p>Each screen is the exact cell grid a terminal of that size would show, rendered with the theme's own "
        "colours through the product's terminal interpreter (termviz). Captures are deterministic fixtures reached "
        "with real key presses over a PTY; they establish layout, colour, wrapping and focus, not aesthetic quality, "
        "which remains the reviewer's judgement on this page. The usual sizes are 80x24 and 140x40; the variants are "
        "the dark and light themes and no-colour mode.</p>",
    ]
    if manifest.get("failures"):
        parts.append("<h2>Capture sessions that failed</h2><ul>")
        for failure in manifest["failures"]:
            parts.append(f"<li><b>{html.escape(failure['flow'])} {html.escape(failure['variant'])}</b>"
                         f"<pre>{html.escape(failure['error'][:4000])}</pre></li>")
        parts.append("</ul>")
    parts.append("<nav>" + " ".join(f'<a href="#{html.escape(flow)}">{html.escape(flow)}</a>' for flow in flows_seen) + "</nav>")
    parts.append("<h2>Lint findings</h2>")
    if findings["findings"]:
        parts.append("<table class=summary><tr><th>capture</th><th>row</th><th>rule</th><th>message</th><th>status</th></tr>")
        for f in findings["findings"]:
            status = f"known: {html.escape(f['reason'])}" if f["known"] else "new"
            parts.append(f"<tr><td><a href=\"#{html.escape(f['capture'])}\">{html.escape(f['capture'])}</a></td>"
                         f"<td>{f['row']}</td><td>{html.escape(f['rule'])}</td><td>{html.escape(f['message'])}</td>"
                         f"<td class={'known' if f['known'] else 'new'}>{status}</td></tr>")
        parts.append("</table>")
    else:
        parts.append("<p>none</p>")
    if findings.get("resolved"):
        parts.append("<h2>Known findings no longer seen</h2><ul>")
        for r in findings["resolved"]:
            parts.append(f"<li><b>{html.escape(r['rule'])}</b> {html.escape(r['capture'])} "
                         f"'{html.escape(r['message'])}': remove its row from lint-known.tsv ({html.escape(r['reason'])})</li>")
        parts.append("</ul>")

    current_flow = None
    for (flow, state), entries in states.items():
        if flow != current_flow:
            current_flow = flow
            parts.append(f'<h2 id="{html.escape(flow)}">{html.escape(flow)}</h2>')
        first = entries[0]
        parts.append(f"<h3>{html.escape(state)}</h3>")
        meta = "markers: " + ", ".join(html.escape(m) for m in first["markers"])
        if first.get("note"):
            meta += " · " + html.escape(first["note"])
        if first.get("group"):
            meta += f" · group {html.escape(first['group'])}"
        parts.append(f"<p class=meta>{meta}</p><div class=variants>")
        for entry in sorted(entries, key=lambda e: (e["columns"], ["dark", "light", "nocolor"].index(e["theme"]))):
            identifier = entry["id"]
            screen = load_screen(output, identifier)
            if screen is None:
                continue
            no_colour = entry["theme"] == "nocolor"
            current = render.to_html(screen, no_colour)
            (renders / f"{identifier}.svg").write_text(render.to_svg(screen, no_colour))
            (renders / f"{identifier}.html").write_text(
                f"<!doctype html><meta charset=\"utf-8\"><title>{html.escape(identifier)}</title><style>{STYLE}</style>" + current)
            label = (f'<div class=label id="{html.escape(identifier)}">{entry["theme"]} {entry["columns"]}x{entry["rows"]} · '
                     f'<a href="renders/{html.escape(identifier)}.svg">svg</a> · <a href="captures/{html.escape(identifier)}.raw">bytes</a> · '
                     f'<a href="captures/{html.escape(identifier)}.txt">text</a></div>')
            body = current
            if baseline:
                before = load_screen(baseline, identifier)
                if before is None:
                    verdict = '<div class="tag changed">no baseline capture</div>'
                    body = f"<div class=pair><div>{verdict}</div><div><div class=tag>after</div>{current}</div></div>"
                else:
                    same = same_cells(before, screen)
                    tag = '<div class="tag same">before (identical)</div>' if same else '<div class="tag changed">before (differs)</div>'
                    body = (f"<div class=pair><div>{tag}{render.to_html(before, no_colour)}</div>"
                            f"<div><div class=tag>after</div>{current}</div></div>")
            notes = by_capture.get(identifier, [])
            items = "".join(
                f"<li class={'known' if f['known'] else 'new'}>row {f['row']} {html.escape(f['rule'])}: {html.escape(f['message'])}"
                + (f" <i>({html.escape(f['reason'])})</i>" if f["known"] else "") + "</li>" for f in notes)
            parts.append(f"<div class=variant>{label}{body}" + (f"<ul class=findings>{items}</ul>" if items else "") + "</div>")
        parts.append("</div>")
    (output / "index.html").write_text("\n".join(parts) + "\n")

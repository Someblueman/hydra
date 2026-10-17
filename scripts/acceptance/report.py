"""Acceptance matrix report: one HTML page and one JSON file per run."""

from __future__ import annotations

import html
import json
from pathlib import Path

from acceptance import matrix

HUMAN_GATES = {
    "U6": "Perceptual flicker in a real terminal app: review the retained recordings.",
    "U7": "Plain-language judgement of the wording.",
    "U8": "Discoverability by a first-time user (goal-driven trial).",
    "U9": "Whether the summaries answer a real user's question.",
    "U3": "Live provider sign-in flows.",
    "U17": "A live provider authoring a plan.",
}


def rows_of(result: dict) -> list[dict]:
    rows = []
    for case in result["cases"]:
        for r in case["results"]:
            rows.append({**r, "case": case["id"], "item": matrix.item_of(r["bullet"])})
    order = {b: i for i, b in enumerate(matrix.BULLETS)}
    rows.sort(key=lambda r: (order.get(r["bullet"], 999), r["case"]))
    return rows


def status_of(row: dict) -> str:
    if row.get("blocked"):
        return "blocked"
    return "passed" if row["passed"] else "failed"


def summary(result: dict) -> str:
    rows = rows_of(result)
    lines = ["Acceptance matrix summary (bullets: passed/failed/blocked, checks: passed/failed/blocked)"]
    for item in matrix.ITEMS:
        bullets = [b for b in matrix.BULLETS if matrix.item_of(b) == item]
        states = {}
        for b in bullets:
            mine = [status_of(r) for r in rows if r["bullet"] == b]
            if not mine:
                states[b] = "uncovered"
            elif all(s == "passed" for s in mine):
                states[b] = "passed"
            elif any(s == "failed" for s in mine):
                states[b] = "failed"
            else:
                states[b] = "blocked"
        checks = [status_of(r) for r in rows if r["item"] == item]
        lines.append(f"  {item:4} bullets {sum(s == 'passed' for s in states.values())}/"
                     f"{sum(s == 'failed' for s in states.values())}/{sum(s == 'blocked' for s in states.values())}"
                     f" of {len(bullets)}"
                     f"  checks {checks.count('passed')}/{checks.count('failed')}/{checks.count('blocked')}"
                     + ("  (uncovered: " + " ".join(b for b, s in states.items() if s == "uncovered") + ")"
                        if "uncovered" in states.values() else "")
                     + ("  red: " + " ".join(b for b, s in states.items() if s == "failed")
                        if "failed" in states.values() else ""))
    total = [status_of(r) for r in rows]
    lines.append(f"  total checks {total.count('passed')} passed, {total.count('failed')} failed, "
                 f"{total.count('blocked')} blocked; cases "
                 f"{sum(c['status'] == 'passed' for c in result['cases'])}/{len(result['cases'])} passed")
    if result.get("infrastructure_error"):
        lines.append("  infrastructure error: " + result["infrastructure_error"])
    return "\n".join(lines)


def snapshot_pages(output: Path, case_id: str) -> list[str]:
    links = []
    for path in sorted((output / case_id).glob("terminal*/snapshot-*.txt")):
        page = path.with_suffix(".html")
        if not page.exists():
            page.write_text('<meta charset="utf-8"><pre>' + html.escape(path.read_text()) + "</pre>")
        links.append(f'<a href="{html.escape(str(page.relative_to(output)))}">{path.parent.name}/{path.stem}</a>')
    return links


def evidence_links(output: Path, case_id: str, evidence: list[str]) -> str:
    parts = []
    for item in evidence:
        path = output / case_id / item
        if path.suffix == ".txt" and path.exists():
            page = path.with_suffix(".html")
            if not page.exists():
                page.write_text('<meta charset="utf-8"><pre>' + html.escape(path.read_text()) + "</pre>")
            item = str(page.relative_to(output / case_id))
        parts.append(f'<a href="{html.escape(case_id + "/" + item)}">{html.escape(Path(item).name)}</a>')
    return " · ".join(parts)


def write(output: Path, result: dict) -> None:
    rows = rows_of(result)
    result["matrix"] = rows
    (output / "report.json").write_text(json.dumps(result, indent=2, default=str) + "\n")
    table = ["<table><tr><th>Item</th><th>Acceptance bullet</th><th>Check</th><th>Result</th><th>Case</th>"
             "<th>Evidence</th></tr>"]
    for r in rows:
        state = status_of(r)
        detail = f"<div class=detail>{html.escape(r.get('detail', ''))}</div>" if r.get("detail") else ""
        table.append(
            f"<tr class={state}><td>{r['item']}</td><td class=bullet>{r['bullet']}<div class=detail>"
            f"{html.escape(matrix.BULLETS.get(r['bullet'], ''))}</div></td>"
            f"<td>{html.escape(r['check'])}{detail}</td><td class=state>{state}</td>"
            f"<td>{html.escape(r['case'])}</td><td>{evidence_links(output, r['case'], r.get('evidence', []))}</td></tr>")
    table.append("</table>")
    cases = []
    for case in result["cases"]:
        reason = f"<p class=reason>{html.escape(case.get('reason', ''))}</p>" if case.get("reason") else ""
        cases.append(f"<h3>{html.escape(case['id'])}: {case['status']} ({case['seconds']:.0f}s)</h3>{reason}"
                     f'<p><a href="{case["id"]}/proof.json">Actions and checks</a> · '
                     f'<a href="{case["id"]}/">Case directory</a></p><p class=captures>'
                     + " · ".join(snapshot_pages(output, case["id"])) + "</p>")
    gates = "".join(f"<li><b>{item}</b>: {html.escape(text)}</li>" for item, text in HUMAN_GATES.items())
    (output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Hydra acceptance</title>'
        "<style>body{font:15px system-ui;max-width:1400px;margin:24px auto;padding:0 16px}"
        "table{border-collapse:collapse;width:100%}td,th{border:1px solid #ccc;padding:4px 6px;vertical-align:top;"
        "font-size:13px}tr.passed td.state{background:#d9f2d9}tr.failed td.state{background:#f5c6c6}"
        "tr.blocked td.state{background:#f3e3b4}.detail{color:#555;font-size:12px}.bullet{white-space:nowrap}"
        "pre{font:13px monospace;white-space:pre-wrap}.captures{font-size:12px}.reason{color:#a00}</style>"
        "<h1>Installed-build acceptance matrix</h1>"
        f"<p>{html.escape(result['mode'])}. Source commit {html.escape(result.get('commit', ''))}. "
        "Each row is one reproducible check with retained evidence: escaped screen snapshots (ASCII, from the "
        "native observer), styled snapshots re-derived from the raw terminal bytes (colour kept), proof.json "
        "action logs, Git/tmux/state inspection and /proc reads. A failed row is a finding against the build, "
        "not a weakened assertion.</p>"
        "<pre>" + html.escape(summary(result)) + "</pre>"
        + "".join(table)
        + "<h2>Still needs a human</h2><ul>" + gates + "</ul><p>" + html.escape("; ".join(result["unqualified"]))
        + "</p><h2>Cases</h2>" + "".join(cases))

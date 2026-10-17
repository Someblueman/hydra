"""The HTML and JSON report: journeys, goal-driven trials with their
discoverability scores, comprehension trials per concept, and the gates this
run does not qualify."""

from __future__ import annotations

import html
import json
from pathlib import Path

from usability.comprehension import CONCEPTS
from usability.registry import PURPOSE as JOURNEY_PURPOSE
from usability.trials import PURPOSE as TRIAL_PURPOSE

INTRO = (
    "Deterministic local fixtures: a shell fixture and a labelled provider fixture reached "
    "through public launch and headless contracts; this never claims live-model coverage. "
    "Failed cases list every failed desired-behaviour check; a case stops at the first check "
    "that blocks the rest of its flow. Captures are escaped text, not a rendering of terminal "
    "colors. Raw bytes are retained."
)
TRIALS_INTRO = (
    "The evaluator is given only the goal in plain words and each screen; it reads the "
    "controls the screen offers (footer and inline key hints, 'Press x to ...' sentences, "
    "the tab bar once the screen says how tabs are reached, open prompts) and chooses the "
    "control whose label shares the goal's vocabulary, with a fixed rule, no model. Steps "
    "are bounded. Score: steps taken, dead ends (cancelled prompts, backtracks, repeated "
    "choices), help opened, fit (share of steps chosen from a matching label rather than a "
    "fallback) and whether the goal state was reached, which a separate checker verifies "
    "against Git, tmux and Hydra state. A lower step count with fit 1.0 and no help means "
    "the goal was reachable from what the screen said; a goal that needs help or fails is "
    "a discoverability finding, not a special case."
)
COMPREHENSION_INTRO = (
    "For each concept the trial reaches the entry point or empty state and checks the "
    "visible explanation for purpose, when it helps, a concrete example and the next "
    "action, in plain language, inside the visible area at the trial's size; consequences "
    "before approval, interruption and removal; command names cross-checked against hydra help."
)


def captures(output: Path, case_id: str) -> str:
    links = []
    for path in sorted((output / case_id).glob("terminal/snapshot-*.txt")):
        html_path = path.with_suffix(".html")
        html_path.write_text('<meta charset="utf-8"><pre>' + html.escape(path.read_text()) + "</pre>")
        links.append(f'<a href="{html.escape(str(html_path.relative_to(output)))}">{path.stem}</a>')
    return " · ".join(links)


def case_block(output: Path, case: dict, purpose: str, extra: str = "") -> str:
    reasons = "".join(f"<li>{html.escape(r)}</li>" for r in case.get("reason", "").split("; ") if r)
    return (
        f"<h3>{html.escape(case['id'])}: {case['status']}</h3><p>{html.escape(purpose)}</p>"
        + extra
        + (f"<ul>{reasons}</ul>" if reasons else "")
        + f'<a href="{case["id"]}/proof.json">Actions and independent checks</a> · '
        + f'<a href="{case["id"]}/terminal/terminal.raw">Terminal bytes</a><p>'
        + captures(output, case["id"])
        + "</p>"
    )


def score_table(score: dict) -> str:
    if not score:
        return ""
    cells = "".join(f"<td>{html.escape(str(score.get(k, '')))}</td>"
                    for k in ("steps", "dead_ends", "help_opened", "controls_read", "fit", "completed", "bound"))
    return ("<table><tr><th>steps</th><th>dead ends</th><th>help opened</th><th>controls read</th>"
            f"<th>fit</th><th>completed</th><th>bound</th></tr><tr>{cells}</tr></table>")


def facet_table(results: list[dict]) -> str:
    rows = []
    for r in results:
        facets = r.get("facets", {})
        cells = ", ".join(f"{k}: {'ok' if v else 'MISSING'}" for k, v in facets.items())
        if "plain_language" in r:
            cells += f", plain language: {'ok' if r['plain_language'] else 'INTERNAL TERMS'}"
        if r.get("blocked"):
            cells = "entry point not reached"
        rows.append(f"<tr><td>{html.escape(r['concept'])}</td><td>{html.escape(r['entry'])}</td><td>{html.escape(cells)}</td></tr>")
    return "<table><tr><th>concept</th><th>entry point</th><th>facets</th></tr>" + "".join(rows) + "</table>"


def report(output: Path, result: dict) -> None:
    (output / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    journeys = "".join(case_block(output, c, JOURNEY_PURPOSE.get(c["id"].rsplit("-", 1)[0], ""))
                       for c in result["journeys"])
    trials = "".join(case_block(output, c, TRIAL_PURPOSE.get(c["id"].rsplit("-", 1)[0], ""),
                                score_table(c.get("score", {})) + "".join(
                                    f"<p>Goal: {html.escape(p['goal'])} — {'reached' if p['reached'] else 'not reached'} "
                                    f"in {p['steps']} steps</p>" for p in c.get("phases", [])))
                     for c in result["trials"])
    comprehension = "".join(case_block(output, c, "Concepts: " + ", ".join(k.name for k in CONCEPTS),
                                       facet_table(c.get("concepts", [])))
                            for c in result["comprehension"])
    (output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Hydra usability</title>'
        "<style>body{font:16px system-ui;max-width:1100px;margin:32px auto}pre{font:14px monospace}"
        "table{border-collapse:collapse}td,th{border:1px solid #999;padding:2px 8px}</style>"
        f"<h1>Installed Hydra usability</h1><p>{html.escape(INTRO)}</p>"
        f"<p>Mode: {html.escape(result['mode'])}. Commit {html.escape(result['commit'])}.</p>"
        f"<h2>Scripted journeys</h2>{journeys or '<p>None selected.</p>'}"
        f"<h2>Goal-driven screen-only trials</h2><p>{html.escape(TRIALS_INTRO)}</p>{trials or '<p>None selected.</p>'}"
        f"<h2>Comprehension trials</h2><p>{html.escape(COMPREHENSION_INTRO)}</p>{comprehension or '<p>None selected.</p>'}"
        "<h2>Separate acceptance still required (not executed here)</h2><ul>"
        + "".join(f"<li>{html.escape(gate)}</li>" for gate in result["unqualified"])
        + "</ul>"
    )

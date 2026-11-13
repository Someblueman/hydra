"""Evidence bundle validation, Markdown summary, provisional budgets and publishing."""

from __future__ import annotations

import gzip
import hashlib
import json
import shutil
from pathlib import Path
from typing import Any

from perf_matrix_sampling import distribution

ACTION_STATUSES = {"observed", "miss", "scheduled-deadline-miss", "invalid-predicate", "injection-unconfirmed"}


def cells_of(bundle: dict[str, Any]) -> list[dict[str, Any]]:
    return [cell for fixture in bundle.get("fixtures", []) for cell in fixture.get("cells", [])]


def validate_bundle(bundle: dict[str, Any]) -> list[str]:
    """Structural checks; unknown counters must be null with a stated reason, never zero."""
    problems: list[str] = []
    for key in ("schema", "generated_utc", "platform", "variants", "declared", "durations", "planned_cells",
                "unsupported_cells", "attribution_boundaries", "fixtures"):
        if key not in bundle:
            problems.append(f"missing top-level {key}")
    if bundle.get("schema") != "hydra-perf-matrix/1":
        problems.append("unexpected schema")
    for row in bundle.get("unsupported_cells", []):
        if not row.get("reason") or not row.get("status"):
            problems.append(f"unsupported cell without status/reason: {row}")
    for fixture in bundle.get("fixtures", []):
        if "cleanup_ok" not in fixture:
            problems.append(f"fixture {fixture.get('id')} lacks cleanup_ok")
        if "shim_overhead" not in fixture or "provisioning" not in fixture and not fixture.get("errors"):
            problems.append(f"fixture {fixture.get('id')} lacks shim overhead or provisioning")
        for cell in fixture.get("cells", []):
            name = f"{fixture.get('id')}/{cell.get('phase')}"
            if cell.get("status") != "measured":
                problems.append(f"{name} not measured")
                continue
            for window in ("settle", "idle", "interaction"):
                entry = cell.get("windows", {}).get(window)
                if not entry:
                    problems.append(f"{name} lacks {window} window")
                    continue
                for key in ("processes", "families", "launches"):
                    if key not in entry:
                        problems.append(f"{name}/{window} lacks {key}")
                roles = entry.get("processes", {}).get("roles", {})
                if not any(role.startswith("native-tui") for role in roles):
                    problems.append(f"{name}/{window} has no native-tui role")
                for role, group in roles.items():
                    if group.get("wakeups_per_second") is None and not group.get("wakeups_unknown_processes"):
                        problems.append(f"{name}/{window}/{role} wakeups null without unknown processes")
                launches = entry.get("launches", {})
                if launches.get("per_minute") is None:
                    problems.append(f"{name}/{window} launches per minute missing")
            actions = cell.get("actions", [])
            if len(actions) != 4 or {a.get("kind") for a in actions} != {"navigation", "search", "resize", "cancel"}:
                problems.append(f"{name} lacks the four scheduled stimuli")
            for action in actions:
                if action.get("status") not in ACTION_STATUSES:
                    problems.append(f"{name}/{action.get('id')} has status {action.get('status')}")
                if action.get("status") == "observed" and action.get("input_to_observed_ns") is None:
                    problems.append(f"{name}/{action.get('id')} observed without latency")
            if "update_latency" not in cell and "update_latency" not in cell.get("unknowns", {}):
                problems.append(f"{name} update latency neither measured nor explained")
            pressure = cell.get("pressure", {})
            if pressure.get("coalesced_updates") is None and not pressure.get("coalesced_updates_reason"):
                problems.append(f"{name} coalesced updates null without reason")
            if "locality" not in cell or "git_launches_per_unchanged_worktree" not in cell["locality"]:
                problems.append(f"{name} lacks locality attribution")
            if not cell.get("workload_proof"):
                problems.append(f"{name} lacks workload proof")
    return problems


def _ms(value: float | None) -> str:
    return "n/a" if value is None else f"{value / 1e6:.1f}"


def _pct(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f}"


def _mib(value: float | None) -> str:
    return "n/a" if value is None else f"{value / 1048576:.1f}"


def _role(cell: dict[str, Any], window: str, role: str, key: str) -> Any:
    return cell["windows"][window]["processes"]["roles"].get(role, {}).get(key)


def _tui_cpu(cell: dict[str, Any], window: str = "idle") -> float | None:
    roles = cell["windows"][window]["processes"]["roles"]
    tui = roles.get("native-tui")
    return None if tui is None else tui["cpu_percent_one_core"]


def _helpers_cpu(cell: dict[str, Any], window: str = "idle") -> float | None:
    tui = cell["windows"][window]["processes"]["roles"].get("native-tui")
    span = cell["windows"][window]["processes"].get("span_seconds")
    return None if tui is None or not span else 100 * tui["reaped_children_cpu_seconds"] / span


def _observer_cpu(cell: dict[str, Any], window: str = "idle") -> float | None:
    roles = cell["windows"][window]["processes"]["roles"]
    span = cell["windows"][window]["processes"].get("span_seconds")
    total = sum(roles.get(role, {}).get("cpu_seconds", 0.0) for role in ("pty-observer", "controller"))
    return None if not span else 100 * total / span


def _label(cell: dict[str, Any]) -> str:
    flag = "" if cell.get("status") == "measured" else f" [{cell.get('status')}]"
    flag += " [non-default env]" if cell.get("fixture_env") else ""
    run = f" ({cell['run']})" if cell.get("run") else ""
    return f"{cell['heads']}/{cell['mode']}/{cell['clients']}c/{cell['preview']}/{cell['case']}/t{cell['trial']}{run}{flag}"


def propose_budgets(bundle: dict[str, Any]) -> dict[str, Any]:
    """Numbers derived from this run with a 1.5x margin; provisional, not frozen."""
    # Cells measured under a declared non-default configuration are diagnostics, not baseline.
    cells = [c for c in cells_of(bundle) if c["status"] == "measured" and not c.get("fixture_env")]
    quiet = [c for c in cells if c["case"] == "quiescent" and c["clients"] == 1 and c["preview"] == "closed"]
    per_heads: dict[int, dict[str, Any]] = {}
    for cell in quiet:
        entry = per_heads.setdefault(cell["heads"], {})
        entry.setdefault("tui_cpu_pct", []).append(_tui_cpu(cell))
        entry.setdefault("helpers_cpu_pct", []).append(_helpers_cpu(cell))
        entry.setdefault("launches_per_min", []).append(cell["windows"]["idle"]["launches"]["per_minute"])
        entry.setdefault("tui_rss_bytes", []).append(_role(cell, "idle", "native-tui", "max_summed_resident_bytes"))
        entry.setdefault("unchanged_git_per_wt", []).extend(cell["locality"]["git_launches_per_unchanged_worktree"])
    latencies = [a["input_to_observed_ns"] for c in cells for a in c["actions"] if a["status"] == "observed"]
    updates = [v for c in cells for v in c.get("update_latency", {}).get("latency_ns", [])]
    scales = sorted({c["heads"] for c in cells})
    loads = [value for f in bundle.get("fixtures", []) for key in ("load_before", "load_after")
             for value in (f.get(key) or [])[:1]]
    recoveries = [r for c in cells if c["case"] == "sustained-output"
                  for r in c["pressure"]["recovery_after_stop_ns"] if r is not None]

    def ceiling(values: list[float | None], factor: float = 1.5) -> float | None:
        clean = [v for v in values if v is not None]
        return None if not clean else max(clean) * factor

    return {
        "status": "provisional: derived from this first Linux-container baseline with a 1.5x margin; not frozen release budgets",
        "idle_per_heads": {
            str(heads): {
                "tui_cpu_percent_one_core_max": ceiling(entry["tui_cpu_pct"]),
                "helpers_cpu_percent_one_core_max": ceiling(entry["helpers_cpu_pct"]),
                "helper_launches_per_minute_max": ceiling(entry["launches_per_min"]),
                "tui_resident_bytes_max": ceiling(entry["tui_rss_bytes"]),
                "git_scans_per_unchanged_worktree_per_idle_window_observed": distribution(entry["unchanged_git_per_wt"]),
            } for heads, entry in sorted(per_heads.items())
        },
        "interaction_latency_ms": {"observed": distribution([v / 1e6 for v in latencies]),
                                   "proposed_p95_ceiling_ms": ceiling([v / 1e6 for v in latencies]),
                                   "per_heads": {str(heads): {
                                       kind: {"observed": distribution(values), "proposed_max_ms": ceiling(values),
                                              "not_observed": sum(1 for c in cells if c["heads"] == heads
                                                                  for a in c["actions"]
                                                                  if a["kind"] == kind and a["status"] != "observed")}
                                       for kind in ("navigation", "search", "resize", "cancel")
                                       for values in [[a["input_to_observed_ns"] / 1e6 for c in cells if c["heads"] == heads
                                                       for a in c["actions"] if a["kind"] == kind and a["status"] == "observed"]]}
                                       for heads in scales},
                                   "note": "deadline misses must stay zero; the proposed ceiling is 1.5x the observed maximum"},
        "update_latency_ms": {"observed": distribution([v / 1e6 for v in updates]),
                              "proposed_max_ms": ceiling([v / 1e6 for v in updates]),
                              "per_heads": {str(heads): {
                                  "observed": distribution(values), "proposed_max_ms": ceiling(values),
                                  "changes_not_seen_in_window": sum(c["update_latency"]["changes_not_seen_in_window"]
                                                                    for c in cells if c["heads"] == heads and "update_latency" in c)}
                                  for heads in scales
                                  for values in [[v / 1e6 for c in cells if c["heads"] == heads
                                                  for v in c.get("update_latency", {}).get("latency_ns", [])]]},
                              "note": "selected-head changed-file count; bounded below by the 2 s refresh period"},
        "observed_load_average_1m": {"min": min(loads, default=None), "max": max(loads, default=None),
                                     "scope": "1-minute load average recorded before and after each fixture on the shared host"},
        "recovery_after_output_stop_ms": {"observed": distribution([v / 1e6 for v in recoveries]),
                                          "proposed_max_ms": ceiling([v / 1e6 for v in recoveries]),
                                          "scope": "producer backlog drained after the declared end at the declared rates; display catch-up is unmeasured"},
        "locality": {
            "target": "unchanged worktrees should incur zero repeated git scans; the observed per-refresh scan of every worktree is the current cost, not a budget",
        },
    }


def write_summary(bundle: dict[str, Any], path: Path) -> None:
    cells = cells_of(bundle)
    # Proof-failed cells keep their samples and are shown with a status flag.
    measured = [c for c in cells if c["status"] in ("measured", "proof-failed")]
    lines = ["# Hydra performance matrix (Linux container baseline)", ""]
    plat = bundle["platform"]
    lines += [f"Generated {bundle['generated_utc']}; {plat['system']} {plat['release']} {plat['machine']}, "
              f"{plat['logical_cpus']} logical CPUs; tools: {plat['tools']}.",
              f"Load note: {plat['load_note']}; load at start {plat['load_at_start']}, end {bundle.get('load_at_end')}.",
              "", "Revisions: " + "; ".join(f"{v['name']} {v['provenance'].get('git_head', v['provenance'].get('kind'))}"
                                            f" (identities unchanged: {v.get('identities_unchanged')})" for v in bundle["variants"]),
              f"Durations: {bundle['durations']}", f"Declared policy: {bundle['declared']}", "",
              f"Cells planned {len(bundle['planned_cells'])}, measured {len(measured)}, "
              f"unsupported/not planned {len(bundle['unsupported_cells'])}. Validation: {bundle.get('validation')}", ""]
    lines += ["## Idle cost after settling (per cell, idle window)", "",
              "| Cell | TUI CPU % (1 core) | helpers CPU % | tmux CPU % | agents CPU % | observer CPU % | TUI wakeups/s | launches/min (git/tmux/hydra) | sys forks/s | TUI RSS MiB | procs | TUI fds |",
              "|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|"]
    for cell in measured:
        idle = cell["windows"]["idle"]
        roles = idle["processes"]["roles"]
        tmux = sum(roles.get(r, {}).get("cpu_percent_one_core", 0.0) or 0.0 for r in ("tmux-server", "pane-shell"))
        by = idle["launches"]["by_helper"]
        wake = roles.get("native-tui", {}).get("wakeups_per_second")
        lines.append(f"| {_label(cell)} | {_pct(_tui_cpu(cell))} | {_pct(_helpers_cpu(cell))} | {_pct(tmux)} | "
                     f"{_pct(roles.get('agent', {}).get('cpu_percent_one_core'))} | {_pct(_observer_cpu(cell))} | "
                     f"{'n/a' if wake is None else f'{wake:.1f}'} | {idle['launches']['per_minute']:.0f} "
                     f"({by.get('git', 0)}/{by.get('tmux', 0)}/{by.get('hydra', 0)}) | "
                     f"{'n/a' if idle['processes']['system_forks_per_second'] is None else f'{idle['processes']['system_forks_per_second']:.1f}'} | "
                     f"{_mib(roles.get('native-tui', {}).get('max_summed_resident_bytes'))} | {idle['processes']['process_count_max']} | "
                     f"{roles.get('native-tui', {}).get('max_fds', 'n/a')} |")
    lines += ["", "## Interaction latency (scheduled input to matching PTY frame; not input-to-pixel)", "",
              "| Cell | navigation ms | search ms | resize ms | cancel ms | statuses | controller lateness max ms |",
              "|---|---:|---:|---:|---:|---|---:|"]
    for cell in measured:
        by_kind = {a["kind"]: a for a in cell["actions"]}
        lines.append(f"| {_label(cell)} | " + " | ".join(_ms(by_kind[k]["input_to_observed_ns"]) for k in
                                                       ("navigation", "search", "resize", "cancel")) +
                     f" | {','.join(by_kind[k]['status'] for k in ('navigation', 'search', 'resize', 'cancel'))} | "
                     f"{_ms(max(a['controller_lateness_ns'] for a in cell['actions']))} |")
    for kind in ("navigation", "search", "resize", "cancel"):
        values = [a["input_to_observed_ns"] / 1e6 for c in measured for a in c["actions"]
                  if a["kind"] == kind and a["status"] == "observed"]
        misses = sum(1 for c in measured for a in c["actions"] if a["kind"] == kind and a["status"] != "observed")
        dist = distribution(values)
        lines.append(f"- {kind}: n={dist['n']} median {dist['median'] and f'{dist['median']:.1f}'} ms, "
                     f"p95 {dist['p95']}, p99 {dist['p99']}, max {dist['max'] and f'{dist['max']:.1f}'} ms, "
                     f"not observed {misses}; {dist['tail_reason']}")
    lines += ["", "## Update latency (worker change receipt to selected-head changed-file count)", "",
              "| Cell | steps seen | median ms | max ms | lower-bound min ms | changes declared | not seen in window | poll error |",
              "|---|---:|---:|---:|---:|---:|---:|---|"]
    for cell in measured:
        update = cell.get("update_latency")
        if not update:
            continue
        dist = distribution(update["latency_ns"])
        lower = [s["latency_lower_bound_ns"] for s in update["steps"] if "latency_lower_bound_ns" in s]
        lines.append(f"| {_label(cell)} | {dist['n']} | {_ms(dist['median'])} | {_ms(dist['max'])} | "
                     f"{_ms(min(lower) if lower else None)} | {update['changes_declared']} | "
                     f"{update['changes_not_seen_in_window']} | {update['poll_error'] or 'none'} |")
    lines += ["", "Detection/collection/model/render split: unknown; the native TUI exposes no stage timestamps.",
              "Unselected worktrees: no matching display state in the Work view (not applicable).", "",
              "## Change locality (idle window; git launches per unchanged worktree)", "",
              "| Cell | changed | git on changed | per unchanged worktree (min/median/max) | captures per unchanged | hydra adapter runs | adapter wall ms (median/max) |",
              "|---|---|---|---|---|---:|---|"]
    for cell in measured:
        loc = cell["locality"]
        dist = distribution(loc["git_launches_per_unchanged_worktree"])
        caps = distribution(loc["captures_per_unchanged_worktree"])
        wrapped = cell["windows"]["idle"]["launches"]["wrapped_durations_ns"].get("hydra", {})
        lines.append(f"| {_label(cell)} | {','.join(loc['changed_worktrees']) or '-'} | {loc['git_launches_changed'] or '-'} | "
                     f"{dist['min']}/{dist['median']}/{dist['max']} | {caps['max']} | "
                     f"{cell['windows']['idle']['launches']['by_helper'].get('hydra', 0)} | "
                     f"{_ms(wrapped.get('median'))}/{_ms(wrapped.get('max'))} |")
    lines += ["", "## Output pressure (sustained-output cells)", "",
              "| Cell | producers x B/s | peak backlog B | EAGAIN | queue non-empty ms (max) | stop late ms (max) | recovery ms (max) | RSS growth tmux/TUI MiB | PTY reads idle |",
              "|---|---|---:|---:|---:|---:|---:|---|---:|"]
    for cell in measured:
        if cell["case"] != "sustained-output":
            continue
        p = cell["pressure"]
        receipts = p["producer_receipts"]
        growth = p["resident_growth_bytes_by_role"]
        lines.append(f"| {_label(cell)} | {p['declared']['producers']} x {p['declared']['bytes_per_second_per_producer']:.0f} | "
                     f"{max(r['peak_backlog_bytes'] for r in receipts)} | {sum(r['write_eagain_count'] for r in receipts)} | "
                     f"{max(r['queue_nonempty_ns'] for r in receipts) / 1e6:.1f} | "
                     f"{_ms(max(cell.get('stop_lateness_ns', [0])))} | "
                     f"{_ms(max((r for r in p['recovery_after_stop_ns'] if r is not None), default=None))} | "
                     f"{_mib(growth.get('tmux-server'))}/{_mib(growth.get('native-tui'))} | {p['client_pty_reads_idle_window']} |")
    lines += ["", "Result retention after the output phases (headless owners retain every event after the worker exits):", ""]
    for fixture in bundle["fixtures"]:
        retention = fixture.get("result_retention")
        if retention:
            lines.append(f"- {fixture['id']}: {retention.get('status', 'verified')}; "
                         f"{json.dumps({k: v for k, v in retention.items() if k not in ('scope', 'status')})[:400]}")
    lines += ["", "Dropped/coalesced display updates: not exposed by the renderer (null, see cell pressure reason).", "",
              "## Fleet scaling and startup", "", "| Fixture | heads | mode | provisioning ms | spawn ms (median) | client time-to-usable ms | cleanup ok | errors |",
              "|---|---:|---|---:|---:|---|---|---|"]
    for fixture in bundle["fixtures"]:
        prov = fixture.get("provisioning", {})
        usable = [f"{r['time_to_usable_ms']:.0f}" for r in fixture.get("client_startups", [])]
        lines.append(f"| {fixture['id']} | {fixture['heads']} | {fixture['mode']} | {prov.get('total_ms', 0):.0f} | "
                     f"{prov.get('spawn_ms_median', 0):.0f} | {','.join(usable)} | {fixture.get('cleanup_ok')} | "
                     f"{len(fixture['errors'])} |")
    lines += ["", "## Observer overhead", ""]
    for fixture in bundle["fixtures"]:
        over = fixture["shim_overhead"]
        lines.append(f"- {fixture['id']}: shim adds {over['overhead_median_ns'] / 1e6:.2f} ms per counted launch "
                     f"(median of {over['launches']}); uptime clock offset bound {fixture['uptime_clock']['offset_bound_ns']} ns")
    lines += ["", "## Unsupported or unmeasured cells", ""]
    lines += [f"- {row}" for row in bundle["unsupported_cells"]]
    if bundle.get("attention"):
        att = bundle["attention"]
        lines += ["", "## I2 attention qualification (annotated local fixture cases)", "",
                  f"Cases {att['cases_total']}: false positives {att['false_positives_total']}, missed signals "
                  f"{att['missed_signals_total']}, unsupported-by-design silent {att['unsupported_silent_total']}, "
                  f"unknown matched {att['unknown_matched_total']}/{att['unknown_expected_total']}. Complete: {att['complete']}."]
    budgets = propose_budgets(bundle)
    bundle["proposed_budgets"] = budgets
    lines += ["", "## Proposed budgets (Linux-container provisional; not frozen)", "", "```json",
              json.dumps(budgets, indent=1), "```", "", "## Limitations", "",
              "- Deterministic stand-in agents; no authenticated live-agent trials.",
              "- Shared host load is uncontrolled; a short-run maximum is not a worst-case bound.",
              "- PTY frame completion, not visible terminal presentation; the shim counts only git, tmux, hydra, hydra-core and hydra-fleet launches.",
              "- Remote headless, macOS, disconnect/reconnect under pressure and T2/T3 remain unmeasured."]
    path.write_text("\n".join(lines) + "\n")


def compact_bundle(bundle: dict[str, Any]) -> dict[str, Any]:
    """Publishable copy: per-role summaries stay, per-process rows and screens go to the run directory."""
    compact = json.loads(json.dumps(bundle))
    for variant in compact.get("variants", []):
        files = variant.get("provenance", {}).pop("files", {})
        variant["provenance"]["files_count"] = len(files)
        variant["provenance"]["files_sha256"] = hashlib.sha256(
            json.dumps(files, sort_keys=True).encode()).hexdigest()
        identities = variant.pop("identities", {})
        variant["identities"] = {key: value for key, value in identities.items() if not key.startswith("runtime/")}
        variant["identities"]["runtime_sha256"] = hashlib.sha256(json.dumps(
            {k: v for k, v in identities.items() if k.startswith("runtime/")}, sort_keys=True).encode()).hexdigest()
    for fixture in compact.get("fixtures", []):
        for cell in fixture.get("cells", []):
            for window in cell.get("windows", {}).values():
                window["processes"].pop("processes", None)
            for action in cell.get("actions", []):
                action.pop("screen_after_miss", None)
            if "update_latency" in cell:
                cell["update_latency"].pop("frames", None)
            for proof in cell.get("workload_proof", []):
                proof["file_hashes"] = len(proof.get("file_hashes", {}))
                for event in proof.values():
                    if isinstance(event, dict):
                        event.pop("cwd", None)  # the head id names the worktree
    if compact.get("attention"):
        compact["attention"].pop("calls", None)
    compact["compaction"] = ("per-process rows, polled frames, screens after misses, runtime file hashes, attention call "
                             "logs and worktree paths inside workload proofs stay in the run directory")
    return compact


def publish(bundle: dict[str, Any], output: Path, evidence: Path, raw_budget: int) -> None:
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / "raw").mkdir(exist_ok=True)
    shutil.copy(output / "summary.md", evidence / "summary.md")
    candidates = []
    for fixture in bundle["fixtures"]:
        for cell in fixture["cells"]:
            source = Path(cell["directory"]) / "samples.json"
            if source.is_file():
                candidates.append((fixture, cell, gzip.compress(source.read_bytes(), 9, mtime=0)))
    # Smallest first so the budget covers as many cells as it can; diagnostics under a
    # non-default configuration are never published in place of a baseline cell.
    chosen, used = set(), 0
    for fixture, cell, data in sorted(candidates, key=lambda row: len(row[2])):
        if not cell.get("fixture_env") and used + len(data) <= raw_budget:
            chosen.add(id(cell))
            used += len(data)
    index = []
    for fixture, cell, data in candidates:
        name = f"{fixture['id']}-{cell['phase']}-{cell['case']}.samples.json.gz"
        if id(cell) in chosen:
            (evidence / "raw" / name).write_bytes(data)
            index.append({"cell": _label(cell), "file": name, "bytes": len(data)})
        else:
            reason = "diagnostic under a non-default configuration" if cell.get("fixture_env") else "raw budget exhausted"
            index.append({"cell": _label(cell), "file": None, "bytes": len(data),
                          "omitted": f"{reason}; the bundle keeps its per-window summaries"})
    bundle["raw_samples_published"] = {"budget_bytes": raw_budget, "used_bytes": used, "files": index}
    # Minified and gzipped (fixed mtime, reproducible bytes) so a multi-run baseline stays small in the
    # repository; summary.md is the readable copy and the run directory keeps the indented bundle.
    compact = json.dumps(compact_bundle(bundle), separators=(",", ":")) + "\n"
    (evidence / "bundle.json.gz").write_bytes(gzip.compress(compact.encode(), 9, mtime=0))
    (output / "bundle.json").write_text(json.dumps(bundle, indent=1) + "\n")

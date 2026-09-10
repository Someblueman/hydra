"""Two real receiver homes observed through the native fleet TUI."""
import json, os, signal, subprocess, tempfile, time
import shutil
from contextlib import contextmanager
from pathlib import Path
from pty_support import Session
ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "bin/hydra"
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build"))

def run(args, env, cwd):
    result = subprocess.run(args, env=env, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    with (base / "commands.jsonl").open("a") as log:
        log.write(json.dumps({"argv": args, "cwd": str(cwd), "exit": result.returncode,
                              "stdout": result.stdout, "stderr": result.stderr}) + "\n")
    if result.returncode:
        raise AssertionError(f"{args}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def wait_for(read, predicate, label, timeout=40):
    deadline = time.monotonic() + timeout
    observed = None
    while time.monotonic() < deadline:
        observed = read()
        if predicate(observed):
            return observed
        time.sleep(.15)
    raise AssertionError(f"{label}: {observed}")


def select_task(session, task):
    session.until(task, 15)
    for _ in range(8):
        if "> " + task in session.screen.text():
            return
        session.send("j")
        session.pump(.15)
    raise AssertionError("task could not be selected: " + session.screen.text())


@contextmanager
def fixture():
    path = Path(tempfile.mkdtemp(prefix="hydra-v4-real-"))
    succeeded = False
    cleanup_failed = False
    try:
        yield path
        succeeded = True
    finally:
        for name in ["a", "b"]:
            home = path / f"host-{name}"
            if not home.is_dir():
                continue
            for gate in ["release", "release-workflow"]:
                (home / gate).touch()
            cleanup = subprocess.run(["sh", "-c", 
                'root="$1"; fixture="$2"; HYDRA_HOME="$3"; export HYDRA_HOME; '
                '. "$root/tests/workflow_task_cleanup.sh"; '
                'workflow_task_fixture_quiesce "$HYDRA_HOME" && test_tmux_fixture_cleanup "$fixture"',
                "fixture-cleanup", str(ROOT), str(path), str(home)],
                capture_output=True, text=True, timeout=45)
            if cleanup.returncode:
                succeeded = False
                cleanup_failed = True
                print(cleanup.stderr, flush=True)
        if succeeded and os.environ.get("HYDRA_TEST_KEEP_FIXTURE") != "1":
            shutil.rmtree(path)
        else:
            print("V4 evidence:", path, flush=True)
        if cleanup_failed:
            raise AssertionError("fixture cleanup could not confirm quiescence")


with fixture() as td:
    base=Path(td); source=base/"source"; source.mkdir(); transport=base/"transport"; transport.mkdir(); client=base/"client"
    for name in ("a","b"):
        receiver=base/f"receiver-{name}"; receiver.mkdir()
        subprocess.run(["git","init","-q"],cwd=receiver,check=True)
        subprocess.run(["git","config","user.name","V4"],cwd=receiver,check=True)
        subprocess.run(["git","config","user.email","v4@example.invalid"],cwd=receiver,check=True)
        subprocess.run(["git","-c","commit.gpgSign=false","commit","--allow-empty","-qm","initial"],cwd=receiver,check=True)
        run([str(BIN),"init","--no-agent","--json"], {**os.environ,"HYDRA_HOME":str(base/f"host-{name}")}, receiver)
    subprocess.run(["git","init","-q"],cwd=source,check=True)
    subprocess.run(["git","config","user.name","V4"],cwd=source,check=True); subprocess.run(["git","config","user.email","v4@example.invalid"],cwd=source,check=True)
    (source/"payload.sh").write_text("while [ ! -f \"$HYDRA_HOME/release\" ]; do sleep .1; done; printf v4-result > result.txt\n")
    subprocess.run(["git","add","payload.sh"],cwd=source,check=True); subprocess.run(["git","-c","commit.gpgSign=false","commit","-qm","payload"],cwd=source,check=True)
    ssh=base/"ssh"; ssh.write_text(f'''#!/bin/sh
set -eu
while [ "$#" -gt 2 ]; do shift; done
case "$1" in
 loopback-a) [ ! -f "{base}/transport/offline-a" ] || exit 255; export HYDRA_HOME={base}/host-a ;;
 loopback-b) export HYDRA_HOME={base}/host-b ;;
 *) exit 255 ;;
esac
exec /bin/sh -c "$2"
'''); ssh.chmod(0o755)
    env={**os.environ,"PATH":f"{base}:{os.environ['PATH']}","HYDRA_HOME":str(client)}
    (base/"git").symlink_to(subprocess.run(["sh","-c","command -v git"],text=True,stdout=subprocess.PIPE).stdout.strip())
    env["PATH"]=f"{base}:{env['PATH']}"
    run([str(BIN),"init","--no-agent","--json"],env,source)
    run([str(BIN),"remote","add","host-a","loopback-a","--hydra",str(BIN),"--home",str(base/"host-a")],env,source)
    run([str(BIN),"remote","add","host-b","loopback-b","--hydra",str(BIN),"--home",str(base/"host-b")],env,source)
    specs=[]
    for name,host in (("a","host-a"),("b","host-b")):
        spec=base/f"spec-{name}.json"; package=base/f"package-{name}.json"
        receiver=base/f"receiver-{name}"; commit=subprocess.run(["git","rev-parse","HEAD"],cwd=source,text=True,stdout=subprocess.PIPE,check=True).stdout.strip()
        spec.write_text(json.dumps({"schema_version":1,"host":host,"project":str(receiver),"source":{"commit":commit},"work":{"kind":"exec","argv":["sh","payload.sh"]},"inputs":[],"outputs":["result.txt"],"capabilities":["exec"],"completion":"command-exit","limits":{"transport_seconds":30,"queue_seconds":30,"startup_seconds":30,"execution_seconds":30,"cancellation_seconds":5,"log_bytes":4096,"artifact_bytes":4096}}))
        preview=run([str(BIN),"fleet","task","prepare","--source",str(source),"--spec",str(spec),"--output",str(package)],env,source)
        digest=json.loads(preview)["data"]["spec_sha256"]
        receipt=json.loads(run([str(BIN),"fleet","task","submit",host,"--input",str(package),"--key",f"v4-{name}","--trust-spec",digest],env,source))
        specs.append((host,receipt.get("task_id") or receipt["data"]["task_id"]))
    task_a,task_b=specs[0][1],specs[1][1]
    def status(host, task):
        return json.loads(run([str(BIN),"fleet","task","status",host,"--id",task],env,source)).get("data", {})
    tui_env={**env,"HYDRA_BIN_CMD":str(BIN),"TERM":"xterm-256color"}
    s=Session([str(BUILD/"hydra-tui"),"--fleet","--view","overview"],140,40,env=tui_env,cwd=source)
    try:
        s.until(task_a,15); s.until(task_b,15)
        assert "host-a" in s.screen.text()
        s.send("j"); s.pump(.3)
        assert "host-b" in s.screen.text(), s.screen.text()
        owner=0
        for _ in range(50):
            owner=int(status("host-a",task_a).get("runtime",{}).get("owner_pid") or 0)
            if owner: break
            time.sleep(.1)
        assert owner > 0
        child=subprocess.run(["ps","-axo","pid=,ppid="],text=True,stdout=subprocess.PIPE,check=True).stdout.splitlines()
        group=next((int(line.split()[0]) for line in child if len(line.split())>1 and int(line.split()[1])==owner),0)
        assert group > 0
        (transport/"offline-a").write_text("")
        deadline=time.time()+8
        while time.time()<deadline and "stale" not in s.screen.text().lower():
            s.pump(.25)
        s.send("H"); s.pump(.4)
        assert "stale" in s.screen.text().lower(), s.screen.text()
        # Kill exactly host A's recorded owner while its receiver task is gated.
        os.kill(owner, signal.SIGSTOP)
        try: os.killpg(group, signal.SIGKILL)
        except ProcessLookupError: pass
        try: os.kill(owner, signal.SIGKILL)
        except ProcessLookupError: pass
        (transport/"offline-a").unlink()
        deadline=time.time()+5
        while time.time()<deadline:
            try:
                if status("host-a",task_a).get("runtime",{}).get("state")=="outcome_unknown": break
            except subprocess.CalledProcessError: pass
            time.sleep(.1)
        assert status("host-a",task_a).get("runtime",{}).get("state")=="outcome_unknown"
        (transport/"offline-a").write_text("")
        (base/"host-b"/"release").write_text("")
        deadline=time.time()+10
        while time.time()<deadline and status("host-b",task_b).get("runtime",{}).get("state")!="succeeded": time.sleep(.1)
        assert status("host-b",task_b).get("runtime",{}).get("state")=="succeeded"
    finally: s.close(keys=b"q")
    (transport/"offline-a").unlink()
    s=Session([str(BUILD/"hydra-tui"),"--fleet","--view","overview"],140,40,env=tui_env,cwd=source)
    try:
        s.until(task_a,15); s.until(task_b,15)
        first=json.loads(run([str(BIN),"fleet","task","observe","host-b","--id",task_b,"--event-limit","2"],env,source))["data"]
        assert "event_observation" in first and "attempt_history" in first["task"]
        assert first["task"]["task_id"]==task_b
        assert first["task"]["attempt_history"][0]["attempt_id"]=="attempt-1"
        for host, task in (specs[1],):
            completed = wait_for(lambda: status(host, task),
                lambda data: data.get("runtime", {}).get("result_state") == "ready",
                "verified receiver result after successful process exit")
            assert completed["runtime"]["state"] == "succeeded"
            out=base/(task+".result")
            run([str(BIN),"fleet","task","result",host,"--id",task,"--output",str(out)],env,source)
            assert out.stat().st_size > 0
            inspected=json.loads(run([str(BIN),"fleet","task","inspect-result","--input",str(out)],env,source))
            assert inspected.get("ok") is True
            second=json.loads(run([str(BIN),"fleet","task","observe",host,"--id",task,"--cursor",str(first["event_observation"]["next_cursor"]),"--event-limit","2"],env,source))["data"]
            assert second["task"]["task_id"]==task and second["task"]["run_id"]==first["task"]["run_id"]
            assert second["event_observation"]["oldest_cursor"] <= second["event_observation"]["head_cursor"]
    finally: s.close(keys=b"q")
    # A real workflow task supplies retained events and attempt/log evidence.
    wfdir=source/".hydra"/"workflows"; wfdir.mkdir(parents=True)
    (wfdir/"v4.yml").write_text("""version: 1
id: v4-events
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 2
steps:
  - id: create
    kind: spawn
    needs: []
    retry: 0
    idempotent: false
    args:
      branch: v4-worker
      terminal_mode: headless
  - id: work
    kind: exec
    needs: [create]
    retry: 0
    idempotent: true
    args:
      head: v4-worker
      argv: [sh, workflow-work.sh]
  - id: verify
    kind: gate
    needs: [work]
    retry: 0
    idempotent: true
    args:
      head: v4-worker
      name: result
      argv: [test, -s, result.txt]
""")
    (source/"workflow-work.sh").write_text(
        "set -eu\nprintf '%s\\n' workflow-prefix\n"
        "while [ ! -f \"$HYDRA_HOME/release-workflow\" ]; do sleep .1; done\n"
        "printf '%s\\n' workflow-suffix\nprintf workflow-result > result.txt\n")
    subprocess.run(["git","add",".hydra/workflows/v4.yml","workflow-work.sh"],cwd=source,check=True)
    subprocess.run(["git","-c","commit.gpgSign=false","commit","-qm","v4 workflow"],cwd=source,check=True)
    wf_spec=json.loads((base/"spec-b.json").read_text()); wf_spec["limits"]["execution_seconds"]=120; wf_spec["source"]["commit"]=subprocess.run(["git","rev-parse","HEAD"],cwd=source,text=True,stdout=subprocess.PIPE,check=True).stdout.strip(); wf_spec["work"]={"kind":"workflow","path":".hydra/workflows/v4.yml"}; wf_spec["completion"]="workflow-success"; wf_spec["outputs"]=["result.txt"]
    (base/"wf-spec.json").write_text(json.dumps(wf_spec)); wf_package=base/"wf-package.json"
    preview=json.loads(run([str(BIN),"fleet","task","prepare","--source",str(source),"--spec",str(base/"wf-spec.json"),"--output",str(wf_package)],env,source)); wf_digest=preview["data"]["spec_sha256"]
    receipt=json.loads(run([str(BIN),"fleet","task","submit","host-b","--input",str(wf_package),"--key","v4-workflow","--trust-spec",wf_digest],env,source)); wf_id=receipt.get("task_id") or receipt["data"]["task_id"]
    def workflow_logs(offset=0, source_kind="owner", limit=4096):
        selection = [] if source_kind == "owner" else ["--step", "work", "--attempt", "1"]
        return json.loads(run([str(BIN), "fleet", "task", "logs", "host-b", "--id", wf_id,
            "--source", source_kind, *selection, "--offset", str(offset),
            "--limit", str(limit)], env, source))["data"]["log"]

    wait_for(lambda: status("host-b", wf_id),
             lambda data: data.get("runtime", {}).get("run_id"), "workflow run assigned")
    prefix = wait_for(lambda: workflow_logs(limit=64),
        lambda data: len(bytes.fromhex(data.get("hex", ""))) == 64 and not data["eof"],
        "bounded owner-log prefix while execution is gated")
    def workflow_observation():
        return json.loads(run([str(BIN), "fleet", "task", "observe", "host-b", "--id", wf_id,
                              "--event-limit", "2"], env, source))
    observed_document = wait_for(workflow_observation, lambda document: any(
        row.get("step_id") == "work" and row.get("state") == "running"
        for row in document["data"]["task"]["steps"]), "original work attempt running")
    observed = observed_document["data"]
    observation_pages = [observed_document]
    stream = observed["event_observation"]
    assert stream["available"] and stream["events"] and not stream["retention_gap"]
    run_id = observed["task"]["run_id"]
    work_attempt = [row for row in observed["task"]["steps"] if row["step_id"] == "work"]
    assert [(row["attempt_id"], row["state"]) for row in work_attempt] == [("attempt-1", "running")], work_attempt
    (base / "observe-before.json").write_text(json.dumps(observed_document))
    (base / "log-before.json").write_text(json.dumps(prefix))

    observer = Session([str(BUILD / "hydra-tui"), "--fleet", "--view", "overview"],
                       140, 40, env=tui_env, cwd=source)
    try:
        select_task(observer, wf_id)
        observer.until("State: running", 15)
        assert status("host-b", wf_id)["runtime"]["state"] == "running"
    finally:
        observer.close(keys=b"q")
    # The observing process is gone while this original task remains gated.
    assert status("host-b", wf_id)["runtime"]["state"] == "running"
    (transport / "offline-a").write_text("")
    restarted = Session([str(BUILD / "hydra-tui"), "--fleet", "--view", "overview"],
                        140, 40, env=tui_env, cwd=source)
    try:
        select_task(restarted, wf_id)
        restarted.until("State: running", 15)
        assert status("host-b", wf_id)["runtime"]["state"] == "running"
        (base / "host-b" / "release-workflow").write_text("")
        finished = wait_for(lambda: status("host-b", wf_id),
            lambda data: data.get("runtime", {}).get("result_state") == "ready", "original result sealed", 60)
        assert finished["runtime"]["state"] == "succeeded", finished
        assert finished["runtime"]["run_id"] == run_id
        restarted.until("State: succeeded", 15)
        assert "> " + wf_id in restarted.screen.text()
        assert "host host-b" in restarted.screen.text()
        (base / "observer-after.txt").write_text(restarted.screen.text())
    finally:
        restarted.close(keys=b"q")
        (transport / "offline-a").unlink(missing_ok=True)

    seen = [event["sequence"] for event in stream["events"]]
    for page in range(32):
        resumed_document = json.loads(run([str(BIN), "fleet", "task", "observe", "host-b", "--id", wf_id,
            "--cursor", str(stream["next_cursor"]), "--byte-offset", str(stream["next_byte_offset"]),
            "--stream-id", stream["stream_id"], "--event-limit", "2"], env, source))
        observation_pages.append(resumed_document)
        resumed = resumed_document["data"]
        current = resumed["event_observation"]
        assert resumed["task"]["run_id"] == run_id and resumed["task"]["task_id"] == wf_id
        assert current["stream_id"] == stream["stream_id"] and not current["stream_reset"] and not current["retention_gap"]
        seen.extend(event["sequence"] for event in current["events"])
        stream = current
        if stream["next_cursor"] == stream["head_cursor"]:
            break
        assert current["events"], current
    assert seen == list(range(1, stream["head_cursor"] + 1)), seen
    assert stream["head_cursor"] >= 10
    attempts = [row for row in resumed["task"]["attempt_history"] if row["step_id"] == "work"]
    assert [(row["attempt_id"], row["state"]) for row in attempts] == [("attempt-1", "succeeded")], attempts
    suffix = workflow_logs(prefix["next_offset"])
    full_log = workflow_logs()
    assert prefix["next_offset"] == 64 and suffix["hex"]
    assert prefix["hex"] + suffix["hex"] == full_log["hex"]
    assert suffix["next_offset"] == full_log["next_offset"]
    work_log = workflow_logs(source_kind="work")
    work_text = bytes.fromhex(work_log["hex"])
    assert b"workflow-prefix" in work_text and b"workflow-suffix" in work_text, work_text
    output = base / "workflow-result.json"
    run([str(BIN), "fleet", "task", "result", "host-b", "--id", wf_id, "--output", str(output)], env, source)
    inspected = json.loads(run([str(BIN), "fleet", "task", "inspect-result", "--input", str(output)], env, source))
    assert inspected["ok"] is True
    result = json.loads(output.read_text())["result"]
    artifacts = [row for row in result["artifacts"] if row["path"] == "result.txt"]
    assert len(artifacts) == 1 and bytes.fromhex(artifacts[0]["hex"]) == b"workflow-result", artifacts
    assert len(list((base / "host-a/fleet/tasks").glob("task_*/acceptance.json"))) == 1
    assert len(list((base / "host-b/fleet/tasks").glob("task_*/acceptance.json"))) == 2
    assert status("host-a", task_a)["runtime"]["state"] == "outcome_unknown"
    for index, page in enumerate(observation_pages):
        saved = base / f"observe-page-{index}.json"
        saved.write_text(json.dumps(page))
        announced = json.loads(run([str(BIN), "fleet", "task", "announce", "--input", str(saved)], env, source))
        text = announced["data"]["announcement"]
        assert all(ord(char) < 128 for char in text)
        assert "evidence=saved_snapshot" in text and "resume cursor=" in text
        (base / f"announcement-{index}.txt").write_text(text)
    (base / "summary.json").write_text(json.dumps({"task":wf_id,"run":run_id,"attempt":"attempt-1",
        "events":seen,"log_bytes":suffix["next_offset"],"prefix_bytes":prefix["next_offset"],
        "resumed_bytes":len(bytes.fromhex(suffix["hex"])),"result":"workflow-result","no_replay":True},indent=2))

print("PASS V4 real two-receiver TUI restart: same task/run/attempt, complete events/logs/result, isolated owner loss and no replay")

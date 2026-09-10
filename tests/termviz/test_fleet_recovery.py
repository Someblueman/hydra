"""Two real receiver homes observed through the native fleet TUI."""
import json, os, re, signal, subprocess, tempfile, time
from pathlib import Path
from pty_support import Session
ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "bin/hydra"
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build"))

def run(args, env, cwd):
    return subprocess.run(args, env=env, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout

with tempfile.TemporaryDirectory(prefix="hydra-v4-real-") as td:
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
    env={**os.environ,"PATH":f"{base}:{os.environ['PATH']}","HYDRA_TEST_GIT":subprocess.run(["command","-v","git"],shell=True,text=True,stdout=subprocess.PIPE).stdout.strip(),"HYDRA_HOME":str(client)}
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
        os.kill(owner, signal.SIGSTOP); os.killpg(group, signal.SIGKILL); os.kill(owner, signal.SIGKILL)
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
            for _ in range(80):
                document=json.loads(run([str(BIN),"fleet","task","status",host,"--id",task],env,source))
                data=document.get("data",document)
                if data.get("state")=="succeeded": break
                assert data.get("state") not in ("failed","outcome_unknown")
                time.sleep(.1)
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
    (source/"workflow-work.sh").write_text("set -eu\nprintf workflow-result > result.txt\n")
    subprocess.run(["git","add",".hydra/workflows/v4.yml","workflow-work.sh"],cwd=source,check=True)
    subprocess.run(["git","-c","commit.gpgSign=false","commit","-qm","v4 workflow"],cwd=source,check=True)
    wf_spec=json.loads((base/"spec-b.json").read_text()); wf_spec["source"]["commit"]=subprocess.run(["git","rev-parse","HEAD"],cwd=source,text=True,stdout=subprocess.PIPE,check=True).stdout.strip(); wf_spec["work"]={"kind":"workflow","path":".hydra/workflows/v4.yml"}; wf_spec["completion"]="workflow-success"; wf_spec["outputs"]=["result.txt"]
    (base/"wf-spec.json").write_text(json.dumps(wf_spec)); wf_package=base/"wf-package.json"
    preview=json.loads(run([str(BIN),"fleet","task","prepare","--source",str(source),"--spec",str(base/"wf-spec.json"),"--output",str(wf_package)],env,source)); wf_digest=preview["data"]["spec_sha256"]
    receipt=json.loads(run([str(BIN),"fleet","task","submit","host-b","--input",str(wf_package),"--key","v4-workflow","--trust-spec",wf_digest],env,source)); wf_id=receipt.get("task_id") or receipt["data"]["task_id"]
    for _ in range(150):
        wf_status=status("host-b",wf_id)
        if wf_status.get("runtime",{}).get("state")=="succeeded": break
        assert wf_status.get("runtime",{}).get("state") not in ("failed","outcome_unknown"); time.sleep(.1)
    assert wf_status.get("runtime",{}).get("state")=="succeeded"
    observed=json.loads(run([str(BIN),"fleet","task","observe","host-b","--id",wf_id,"--event-limit","2"],env,source))["data"]
    stream=observed["event_observation"]; assert stream["available"] and not stream["retention_gap"] and stream["events"]
    assert observed["task"]["attempt_history"] and observed["task"]["attempt_history"][0]["attempt_id"]
    resumed=json.loads(run([str(BIN),"fleet","task","observe","host-b","--id",wf_id,"--cursor",str(stream["next_cursor"]),"--byte-offset",str(stream["next_byte_offset"]),"--stream-id",stream["stream_id"],"--event-limit","2"],env,source))["data"]["event_observation"]
    assert resumed["stream_reset"] is False and resumed["oldest_cursor"] <= resumed["head_cursor"]

print("PASS V4 real two-receiver TUI observation: distinct task identities, stale reconnect, and verified result packages")

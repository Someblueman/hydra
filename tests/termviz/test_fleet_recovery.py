"""Two real receiver homes observed through the native fleet TUI."""
import json, os, re, subprocess, tempfile, time
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
    (source/"payload.sh").write_text("sleep 2; printf v4-result > result.txt\n")
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
    tui_env={**env,"HYDRA_BIN_CMD":str(BIN),"TERM":"xterm-256color"}
    s=Session([str(BUILD/"hydra-tui"),"--fleet","--view","overview"],140,40,env=tui_env,cwd=source)
    try:
        s.until(task_a,15); s.until(task_b,15)
        assert "host-a" in s.screen.text()
        s.send("j"); s.pump(.3)
        assert "host-b" in s.screen.text(), s.screen.text()
        (transport/"offline-a").write_text("")
        deadline=time.time()+8
        while time.time()<deadline and "stale" not in s.screen.text().lower():
            s.pump(.25)
        s.send("H"); s.pump(.4)
        assert "stale" in s.screen.text().lower(), s.screen.text()
    finally: s.close(keys=b"q")
print("PASS V4 real two-receiver TUI observation: distinct task identities, host rows and stale transport evidence")

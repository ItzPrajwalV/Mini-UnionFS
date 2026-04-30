from flask import Flask, render_template, request, jsonify
import os
import shlex
import signal
import subprocess
import threading
from pathlib import Path

app = Flask(__name__, template_folder="templates", static_folder="static")
PROJECT_DIR = Path(__file__).resolve().parent
BINARY = PROJECT_DIR / "mini_unionfs"
LOG_FILE = PROJECT_DIR / "ui_mount.log"
MOUNT_PROC = None
MOUNT_LOCK = threading.Lock()


def write_log(message: str) -> None:
    timestamp = subprocess.check_output(["date", "+%Y-%m-%d %H:%M:%S"]).decode().strip()
    with LOG_FILE.open("a", encoding="utf-8") as log:
        log.write(f"[{timestamp}] {message}\n")


def ensure_log_exists() -> None:
    if not LOG_FILE.exists():
        LOG_FILE.write_text("", encoding="utf-8")


def read_log_lines(limit: int = 200) -> str:
    ensure_log_exists()
    lines = LOG_FILE.read_text(encoding="utf-8").splitlines()
    return "\n".join(lines[-limit:])


def is_mount_active(mountpoint: Path) -> bool:
    if not mountpoint.exists():
        return False
    try:
        subprocess.run(["mountpoint", "-q", str(mountpoint)], check=True)
        return True
    except subprocess.CalledProcessError:
        return False


def run_command(command, cwd=None, capture_output=True, timeout=None):
    try:
        result = subprocess.run(
            command,
            cwd=cwd or PROJECT_DIR,
            shell=isinstance(command, str),
            capture_output=capture_output,
            text=True,
            timeout=timeout,
            check=True,
        )
        return True, result.stdout.strip() if result.stdout else ""
    except subprocess.CalledProcessError as err:
        stderr = err.stderr.strip() if err.stderr else ""
        return False, (err.stdout or "") + ("\n" + stderr if stderr else "")


def start_mount_process(lower: str, upper: str, mountpoint: str):
    global MOUNT_PROC
    with MOUNT_LOCK:
        if MOUNT_PROC and MOUNT_PROC.poll() is None:
            return False, "A mount process is already running"

        if not BINARY.exists():
            return False, "Binary mini_unionfs not found. Build it first."

        Path(lower).mkdir(parents=True, exist_ok=True)
        Path(upper).mkdir(parents=True, exist_ok=True)
        Path(mountpoint).mkdir(parents=True, exist_ok=True)

        command = [str(BINARY), "-o", f"lower={lower},upper={upper}", mountpoint]
        write_log(f"Starting mount: {' '.join(shlex.quote(p) for p in command)}")

        MOUNT_PROC = subprocess.Popen(
            command,
            cwd=PROJECT_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            preexec_fn=os.setsid,
        )

        def stream_output(proc):
            for line in proc.stdout or []:
                write_log(line.rstrip())
            proc.wait()
            write_log(f"Mount process exited with code {proc.returncode}")

        threading.Thread(target=stream_output, args=(MOUNT_PROC,), daemon=True).start()
        return True, "Mount started successfully"


def stop_mount_process(mountpoint: str):
    global MOUNT_PROC
    with MOUNT_LOCK:
        if MOUNT_PROC and MOUNT_PROC.poll() is None:
            write_log("Stopping mount process")
            try:
                os.killpg(os.getpgid(MOUNT_PROC.pid), signal.SIGTERM)
            except Exception:
                pass
            MOUNT_PROC.wait(timeout=5)
            write_log("Mount process stopped")
            MOUNT_PROC = None

        try:
            subprocess.run(["fusermount3", "-u", mountpoint], check=True, capture_output=True, text=True)
            write_log(f"Unmounted {mountpoint} with fusermount3")
            return True, "Unmount successful"
        except subprocess.CalledProcessError:
            try:
                subprocess.run(["umount", mountpoint], check=True, capture_output=True, text=True)
                write_log(f"Unmounted {mountpoint} with umount")
                return True, "Unmount successful"
            except subprocess.CalledProcessError as err:
                return False, err.stderr or "Failed to unmount filesystem"


def build_binary():
    write_log("Running make build")
    return run_command("make clean && make", cwd=PROJECT_DIR)


def run_tests():
    write_log("Running test_unionfs.sh")
    return run_command("bash test_unionfs.sh", cwd=PROJECT_DIR, timeout=300)


@app.route("/")
def index():
    ensure_log_exists()
    home_dir = str(Path.home())
    return render_template(
        "index.html",
        default_lower=f"{home_dir}/unionfs_test/lower",
        default_upper=f"{home_dir}/unionfs_test/upper",
        default_mount=f"{home_dir}/unionfs_test/mount",
    )


@app.route("/api/status")
def api_status():
    mount_dir = request.args.get("mount", str(PROJECT_DIR / "mount"))
    status_data = {
        "binaryExists": BINARY.exists(),
        "mountActive": is_mount_active(Path(mount_dir)),
        "mountProcessRunning": bool(MOUNT_PROC and MOUNT_PROC.poll() is None),
        "mountPoint": mount_dir,
        "log": read_log_lines(200),
    }
    return jsonify(status_data)


@app.route("/api/build", methods=["POST"])
def api_build():
    success, output = build_binary()
    write_log(output)
    status = "success" if success else "error"
    return jsonify({"status": status, "output": output})


@app.route("/api/test", methods=["POST"])
def api_test():
    success, output = run_tests()
    write_log(output)
    status = "success" if success else "error"
    return jsonify({"status": status, "output": output})


@app.route("/api/mount", methods=["POST"])
def api_mount():
    lower = request.form.get("lower", str(Path.home() / "unionfs_test/lower"))
    upper = request.form.get("upper", str(Path.home() / "unionfs_test/upper"))
    mountpoint = request.form.get("mountpoint", str(Path.home() / "unionfs_test/mount"))
    success, message = start_mount_process(lower, upper, mountpoint)
    return jsonify({"status": "success" if success else "error", "message": message})


@app.route("/api/unmount", methods=["POST"])
def api_unmount():
    mountpoint = request.form.get("mountpoint", str(Path.home() / "unionfs_test/mount"))
    success, message = stop_mount_process(mountpoint)
    return jsonify({"status": "success" if success else "error", "message": message})


@app.route("/api/logs")
def api_logs():
    return jsonify({"logs": read_log_lines(400)})


if __name__ == "__main__":
    ensure_log_exists()
    write_log("UI started")
    app.run(host="0.0.0.0", port=5000, debug=False)

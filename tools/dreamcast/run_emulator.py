# SPDX-License-Identifier: BSD-3-Clause
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RA = Path("F:/Retroarch/retroarch.exe")
CORE = Path("F:/Retroarch/cores/flycast_libretro.dll")
CFG = ROOT / "emulator" / "retroarch.cfg"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("imagepath")
    p.add_argument("--label", default="diagnostic")
    p.add_argument("--frames", type=int, default=900)
    args = p.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
        sys.exit("Invalid label")
    if not (1 <= args.frames <= 36000):
        sys.exit("Invalid frames")

    img = Path(args.imagepath)
    if not img.exists():
        sys.exit("Image not found")
    if not RA.exists() or not CORE.exists() or not CFG.exists():
        sys.exit("RetroArch components missing")

    ss_dir = ROOT / "emulator" / "screenshots"
    log_dir = ROOT / "logs"
    ss_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    ss_path = ss_dir / f"{args.label}.png"
    log_path = log_dir / f"flycast-{args.label}.log"

    argv = [
        str(RA), "-v", "-c", str(CFG), "-L", str(CORE),
        f"--max-frames={args.frames}", "--max-frames-ss",
        f"--max-frames-ss-path={ss_path}", f"--log-file={log_path}",
        str(img)
    ]

    print(json.dumps(argv), flush=True)

    try:
        with open(log_dir / f"flycast-{args.label}-stdout.txt", "w") as f, open(log_dir / f"flycast-{args.label}-stderr.txt", "w") as err:
            r = subprocess.run(
                argv, shell=False, cwd="F:/Retroarch",
                stdout=f, stderr=err, timeout=90,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)
            )
    except subprocess.TimeoutExpired:
        sys.exit(1)

    ok = r.returncode == 0 and ss_path.exists()
    print(f"exitcode={r.returncode} screenshot_exists={ss_path.exists()}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

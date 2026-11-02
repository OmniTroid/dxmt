#!/usr/bin/env python3
"""bench: times and profiles tests/perf/d3d12_render_bench.cpp, for iterating on DXMT's performance.

  tests/perf/bench.py run --build BUILD --wine WINE [--label NAME] [--scenes a,b] [--front-end dxbc|dxil]
                          [--frames N] [--warmup N] [--repeat N] [--no-profile] [--no-install]
  tests/perf/bench.py compare BEFORE.json AFTER.json
  tests/perf/bench.py corpus --build BUILD --wine WINE DIRECTORY

run: each scene --repeat times in a process of its own, timed with no profile on, then once with DXMT_DIAG=profile,
whose overhead stays out of the timings, for which bridge calls took its CPU time. a report
goes to BUILD/perf/NAME.json. pipelines runs twice with DXMT's cache on: cold, then what a second process keeps
(pipelines-warm). streaming is frame with a new pipeline every tenth frame: its hitches and max are the numbers to read. a scene's times are the median over its frames of the repeat with the lowest median.

compare: the scenes' times and calls side by side. a change inside the spread of the repeats is marked as noise, and
a changed image hash is called out: a faster render of something else is not an improvement. GPU clocks drift with heat,
so alternate the builds when a result matters.
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import run as tests  # noqa: E402  (tests/run.py: the builds, the Wine, its prefix, the guards)

ROOT = Path(__file__).resolve().parents[2]
FIELDS = ("record", "submit", "wait", "gpu")
DIAG_TOP = 15


def percentile(values, share):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * share))]


def run_scene(wine, exe, scene, args, env, timeout):
    command = [str(wine), str(exe), args.front_end, scene, str(args.frames), str(args.warmup)]
    started = time.monotonic()
    done = subprocess.run(command, env=env, capture_output=True, text=True, errors="replace", timeout=timeout)
    frames, result = [], None
    for line in done.stdout.splitlines():
        line = line.strip()
        if line.startswith("frame "):
            frames.append([float(x) for x in line.split()[2:6]])
        elif line.startswith("result "):
            result = line.split()
    report = {"ok": result is not None and done.returncode == 0, "seconds": round(time.monotonic() - started, 2)}
    if not report["ok"]:
        report["log"] = (done.stdout + done.stderr)[-1500:]
        return report
    report["hash"] = result[2]
    report["setup_ms"] = float(result[3])
    report["frames"] = len(frames)
    for i, field in enumerate(FIELDS):
        values = [f[i] for f in frames]
        report[field] = {"median": round(statistics.median(values), 3), "p95": round(percentile(values, 0.95), 3),
                         "max": round(max(values), 3)}
    # what the frame took on the CPU's side and past the GPU's own time: encoding on DXMT's threads
    totals = [f[0] + f[1] + f[2] for f in frames]
    report["total"] = {"median": round(statistics.median(totals), 3), "p95": round(percentile(totals, 0.95), 3),
                       "p99": round(percentile(totals, 0.99), 3), "max": round(max(totals), 3),
                       "hitches": sum(t > 2 * statistics.median(totals) for t in totals)}
    return report


def profile_scene(wine, exe, scene, args, env, timeout, directory):
    run_env = dict(env, DXMT_DIAG="profile", DXMT_DIAG_PATH=str(directory))
    for old in directory.glob("*.diag"):
        old.unlink()
    command = [str(wine), str(exe), args.front_end, scene, str(args.frames), str(args.warmup)]
    done = subprocess.run(command, env=run_env, capture_output=True, text=True, errors="replace", timeout=timeout)
    frames = [[float(x) for x in line.split()[2:5]] for line in done.stdout.splitlines() if line.startswith("frame ")]
    profiled = round(statistics.median(sum(f) for f in frames), 3) if frames else None
    records = sorted(directory.glob("*.diag"), key=lambda p: p.stat().st_size)
    if not records:
        return {"error": "no profile record"}
    said = subprocess.run([sys.executable, str(ROOT / "tools" / "dxmt-diag"), "report", "--top", str(DIAG_TOP), str(records[-1])],
                          capture_output=True, text=True, errors="replace").stdout
    kinds = said.split("GPU time by kind of pass", 1)[-1].split("\n\n", 1)[0]
    shares = [float(m[1]) for m in re.finditer(r"^\s+-?[\d.]+\s+(-?[\d.]+)%", kinds, re.M)]
    gpu_table_ok = bool(shares) and all(0 <= share <= 100 for share in shares)
    calls = {}
    section = said.split("CPU time by bridge call", 1)[-1]
    for m in re.finditer(r"^\s+([\d.]+)\s+[\d.]+%\s+(\w+): (\d+) calls", section, re.M):
        calls[m[2]] = {"ms": float(m[1]), "calls": int(m[3])}
    frames = max(args.frames + args.warmup, 1)
    return {"profiled_total": profiled, "gpu_table_ok": gpu_table_ok, "bridge_ms_per_frame": {k: round(v["ms"] / frames, 3) for k, v in calls.items()},
            "bridge_calls_per_frame": {k: round(v["calls"] / frames, 1) for k, v in calls.items()}}


def print_scene(scene, report):
    if not report.get("ok"):
        print(f"{scene:15} FAILED\n{report.get('log', '')}", flush=True)
        return
    total = report["total"]
    print(f"{scene:15} total {total['median']:8.2f} ms (p95 {total['p95']:8.2f}, max {total['max']:8.2f}, {total['hitches']} hitches)"
          f"  record {report['record']['median']:7.2f}  wait {report['wait']['median']:7.2f}  gpu {report['gpu']['median']:7.2f}"
          f"  {report['hash']}", flush=True)


def pipelines_cold_and_warm(wine, exe, args, env):
    """the same new pipelines twice with DXMT's cache on, in two processes: what a second run of a game keeps"""
    frames = min(args.frames, 20)
    with tempfile.TemporaryDirectory() as cache:
        run_env = dict(env, DXMT_SHADER_CACHE_PATH=cache, DXMT_BENCH_SALT=str(int(time.time()) % 100000000))
        run_env.pop("DXMT_SHADER_CACHE", None)
        scene_args = argparse.Namespace(front_end=args.front_end, frames=frames, warmup=0)
        cold = run_scene(wine, exe, "pipelines", scene_args, run_env, args.timeout)
        warm = run_scene(wine, exe, "pipelines", scene_args, run_env, args.timeout)
    return cold, warm


def cmd_run(args):
    tests.guard(args)
    builds, wine, env = tests.environment(args)
    exe = builds[0] / "tests" / "perf" / "d3d12_render_bench.exe"
    if not exe.exists():
        sys.exit(f"{exe}: no benchmark (a build set up with -Denable_tests=true makes one)")
    listed = subprocess.run([str(wine), str(exe), args.front_end, "list"], env=env, capture_output=True, text=True, errors="replace").stdout
    scenes = args.scenes.split(",") if args.scenes else [s.strip() for s in listed.splitlines() if re.fullmatch(r"[a-z]+", s.strip())]
    data = {"label": args.label, "front_end": args.front_end, "frames": args.frames, "warmup": args.warmup,
            "time": time.strftime("%Y-%m-%d %H:%M:%S"), "scenes": {}}
    head = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    data["commit"] = head
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        for scene in scenes:
            if scene == "pipelines":
                cold, warm = pipelines_cold_and_warm(wine, exe, args, env)
                data["scenes"]["pipelines"], data["scenes"]["pipelines-warm"] = cold, warm
                print_scene("pipelines", cold)
                print_scene("pipelines-warm", warm)
                continue
            runs = [run_scene(wine, exe, scene, args, env, args.timeout) for _ in range(args.repeat)]
            good = [r for r in runs if r["ok"]]
            if not good:
                data["scenes"][scene] = runs[-1]
                print(f"{scene:12} FAILED\n{runs[-1].get('log', '')}", flush=True)
                continue
            best = min(good, key=lambda r: r["total"]["median"])
            best["repeats"] = [r["total"]["median"] for r in good]
            if scene != "streaming" and not args.no_profile:
                best["profile"] = profile_scene(wine, exe, scene, args, env, args.timeout, directory)
            data["scenes"][scene] = best
            print_scene(scene, best)
            profiled = best.get("profile", {}).get("profiled_total")
            if profiled:
                print(f"{scene:15} with the profile on: {profiled:.2f} ms, {profiled - best['total']['median']:+.2f} ms a frame", flush=True)
            if not best.get("profile", {}).get("gpu_table_ok", True):
                print(f"{scene:12} the profile's GPU time by kind of pass is impossible: its numbers can't be read", flush=True)
    out = Path(args.out) if args.out else builds[0] / "perf" / f"{args.label}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=1))
    print("wrote", out)


def cmd_corpus(args):
    """how long a game's dumped shaders (neutron start --dump-shaders) take to become pipelines, cold and then with
    the caches a first pass left, in two processes. the shaders are the game's: keep the directory out of any repo"""
    tests.guard(args)
    builds, wine, env = tests.environment(args)
    exe = builds[0] / "tests" / "d3d12" / "d3d12_corpus.exe"
    directory = Path(args.directory).resolve()
    with tempfile.TemporaryDirectory() as cache:
        run_env = dict(env, DXMT_CORPUS="Z:" + str(directory).replace("/", "\\"), DXMT_SHADER_CACHE_PATH=cache)
        run_env.pop("DXMT_SHADER_CACHE", None)
        for name in ("cold", "warm"):
            started = time.monotonic()
            done = subprocess.run([str(wine), str(exe), "dxil"], env=run_env, capture_output=True, text=True, errors="replace",
                                  timeout=args.timeout)
            said = next((line for line in done.stdout.splitlines() if "pipelines made" in line), done.stdout[-300:].strip())
            print(f"{name}  {time.monotonic() - started:8.1f} s  {said}", flush=True)


def cmd_compare(args):
    before, after = json.loads(Path(args.before).read_text()), json.loads(Path(args.after).read_text())
    print(f"{before['label']} ({before.get('commit')}) -> {after['label']} ({after.get('commit')})")
    for scene in sorted(set(before["scenes"]) & set(after["scenes"])):
        a, b = before["scenes"][scene], after["scenes"][scene]
        if not (a.get("ok") and b.get("ok")):
            print(f"{scene:12} failed in {'before' if not a.get('ok') else 'after'}")
            continue
        print(f"{scene}{'  IMAGE CHANGED' if a['hash'] != b['hash'] and scene != 'pipelines' else ''}")
        for field in ("total",) + FIELDS:
            x, y = a[field]["median"], b[field]["median"]
            change = (y - x) / x * 100 if x else 0
            spread = max(a.get("repeats", [x])) - min(a.get("repeats", [x])) + max(b.get("repeats", [y])) - min(b.get("repeats", [y]))
            noise = field == "total" and abs(y - x) <= spread
            print(f"  {field:8} {x:9.2f} -> {y:9.2f} ms  {change:+6.1f}%{'  (within the spread)' if noise else ''}")
        calls_a = a.get("profile", {}).get("bridge_ms_per_frame", {})
        calls_b = b.get("profile", {}).get("bridge_ms_per_frame", {})
        moved = sorted(set(calls_a) | set(calls_b), key=lambda k: -abs(calls_b.get(k, 0) - calls_a.get(k, 0)))[:5]
        for name in moved:
            print(f"  {name:42} {calls_a.get(name, 0):8.3f} -> {calls_b.get(name, 0):8.3f} ms a frame")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run")
    tests.arguments(run)
    run.add_argument("--beside-vms", action="store_true")
    run.add_argument("--jobs", type=int, default=1)
    run.add_argument("--label", default="baseline")
    run.add_argument("--scenes")
    run.add_argument("--front-end", default="dxbc", choices=("dxbc", "dxil"))
    run.add_argument("--frames", type=int, default=60)
    run.add_argument("--warmup", type=int, default=10)
    run.add_argument("--repeat", type=int, default=3)
    run.add_argument("--timeout", type=int, default=300)
    run.add_argument("--no-profile", action="store_true")
    run.add_argument("--out")
    run.set_defaults(handler=cmd_run)
    corpus = commands.add_parser("corpus")
    tests.arguments(corpus)
    corpus.add_argument("--beside-vms", action="store_true")
    corpus.add_argument("--jobs", type=int, default=1)
    corpus.add_argument("--timeout", type=int, default=3600)
    corpus.add_argument("directory")
    corpus.set_defaults(handler=cmd_corpus)
    compare = commands.add_parser("compare")
    compare.add_argument("before")
    compare.add_argument("after")
    compare.set_defaults(handler=cmd_compare)
    args = parser.parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()

"""Inputs on which SMLib crashed (SIGSEGV) in cadler, run with an installed usd-brep wheel, each in
a process of its own: every one must end, with a result or a clean RuntimeError.

    python cadler/crashes/run.py

Each case is <name>.txt (a native Brep, as Brep.write_to_file writes it) and <name>.json (the
operation, the indices of its edges in brep.edges(), its sizes):
- fillet-flange, fillet-round-errors: a fillet meeting a corner SMLib has no case for left the
  vertex mapped to its array of edgeuses, which was then taken for a corner (SmFilletExecutive,
  CreateFilletCorners); from cadler's models 63-flange and 100-round-errors with a number changed.
"""

import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).parent

CASE = r"""
import json, sys
import usd_brep as ub
body = ub.Brep.read_from_file(sys.argv[1], ascii=True, rebuild_uv_trim_curves=True)
case = json.loads(open(sys.argv[2]).read())
every = body.edges()
try:
    if case["op"] == "fillet":
        ub.fillet_edges(body, [every[index] for index in case["edges"]], radius=case["radius"])
    print("result")
except RuntimeError as error:
    print("clean error:", str(error).splitlines()[0][:120])
"""

failed = 0
for spec in sorted(HERE.glob("*.json")):
    process = subprocess.run([sys.executable, "-c", CASE, str(spec.with_suffix(".txt")), str(spec)], capture_output=True, text=True)
    if process.returncode != 0:
        failed += 1
        print(f"{spec.stem}: exit code {process.returncode}\n{process.stderr[-2000:]}")
    else:
        print(f"{spec.stem}: {process.stdout.strip()}")
print(f"{failed} of {len(list(HERE.glob('*.json')))} cases crashed" if failed else "no case crashed")
sys.exit(1 if failed else 0)

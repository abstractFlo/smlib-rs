"""Inputs on which SMLib crashed (SIGSEGV) in cadler, run with an installed usd-brep wheel, each in
a process of its own: every one must end, with a result or a clean RuntimeError.

    python cadler/crashes/run.py

Each case is <name>.txt (a native Brep, as Brep.write_to_file writes it) and <name>.json (the
operation, the indices of its edges in brep.edges(), its sizes, for fillet_per_edge one radius and
one FilletXSect value per edge):
- fillet-flange, fillet-round-errors: a fillet meeting a corner SMLib has no case for left the
  vertex mapped to its array of edgeuses, which was then taken for a corner (SmFilletExecutive,
  CreateFilletCorners); from cadler's models 63-flange and 100-round-errors with a number changed.
- fillet-cube-top-and-corners: a 40 mm cube's top edges rounded by 20 together with its corner
  edges by 19.999, as cadler rounds the seams of a rounded top. Squeezing an edge whose two uses
  form a loop on their own made that loop a vertex loop once per use, and the vertexuse the second
  replaced stayed on the vertex; deleting the top face then walked it after its loop was freed
  (SmBrep::SqueezeEdge, SmBrep::DeleteFace).
- fillet-seams-edge-in-one-face, fillet-seams-stale-vertex-mate: the seams of a cube's rounded top
  (40 mm by 19.96, 20 mm by 9.97), the body as cadler hands it on. After the fillet, an edge
  between two loops of one face was deleted as if it joined two faces, deleting that face and its
  surface (SmMerge::BooleanPostProcess); and a vertex glued away stayed mated, then marked through
  the stale pointer (SmProtoTopologyManager::GlueCoincidentBrepVertices, then DoFilleting).
  Two more crashes there do not come back from a written file, so cadler's kernel tests check
  them: a polar box built from a Bezier decomposition without a surface (SmBezierAux2d), and a
  vertex glued again after an earlier common edge had removed it (100 and 400 mm cubes).
- unclamped-spline-prism: a prism on a closed periodic B-spline with an unclamped knot vector, as
  OpenCASCADE writes them to STEP (the native text cadler made of one before it clamped them).
  Its volume crashed in the Bezier split; SmClampNurbs clamps it on read. The volume must be the
  profile's area times the height.
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
    elif case["op"] == "fillet_per_edge":
        ub.fillet_edges_per_edge(body, [every[index] for index in case["edges"]], case["radii"], [ub.FilletXSect(value) for value in case["xsects"]])
    elif case["op"] == "volume":
        volume = body.volume(1e-6)
        if abs(volume - case["expect"]) > case["tolerance"]:
            sys.exit(f"volume {volume}, expected {case['expect']}")
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

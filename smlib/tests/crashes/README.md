# Crash cases

Inputs on which SMLib once crashed (SIGSEGV), each fixed in this repository. `tests/crashes.rs`
runs them; every one must end with a result or a clean error.

Each case is `<name>.txt` (a body in SMLib's native text format, as `Brep::write_to_file` writes
it) and `<name>.json` (the operation, the indices of its edges in `Brep::edges()`, its sizes; for
`fillet_per_edge` one radius and one cross-section per edge, as SMLib's
`SmFilletSurfaceGeneratorType`: linear 1, circular 2, blend 3).

- **fillet-flange, fillet-round-errors**: a fillet meeting a corner SMLib has no case for left the
  vertex mapped to its array of edgeuses, which was then taken for a corner (`SmFilletExecutive`,
  `CreateFilletCorners`).
- **fillet-cube-top-and-corners**: a 40 mm cube's top edges rounded by 20 together with its corner
  edges by 19.999. Squeezing an edge whose two uses form a loop on their own made that loop a
  vertex loop once per use, and the vertexuse the second replaced stayed on the vertex; deleting
  the top face then walked it after its loop was freed (`SmBrep::SqueezeEdge`,
  `SmBrep::DeleteFace`).
- **fillet-seams-edge-in-one-face, fillet-seams-stale-vertex-mate**: the seams of a cube's rounded
  top (40 mm by 19.96, 20 mm by 9.97). After the fillet, an edge between two loops of one face was
  deleted as if it joined two faces, deleting that face and its surface
  (`SmMerge::BooleanPostProcess`); and a vertex glued away stayed mated, then marked through the
  stale pointer (`SmProtoTopologyManager::GlueCoincidentBrepVertices`, then `DoFilleting`).
- **unclamped-spline-prism**: a prism on a closed periodic B-spline with an unclamped knot vector,
  as OpenCASCADE writes them to STEP. Its volume crashed in the Bezier split; `SmClampNurbs`
  clamps it on read. The volume must be the profile's area times the height.

# Changelog

What changed in smlib-rs, for people who use it. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) (before 1.0, a minor version may break the API).
`smlib` and `smlib-sys` always share one version. Each release says which usd-brep release of
NVIDIA's its SMLib is based on.

## [Unreleased]

### Changed

- On macOS (Apple silicon) and iOS, SMLib no longer fuses multiply-adds (`-ffp-contract=off`), as
  usd-brep's macOS build does since 1.0.2, so it rounds as on x86-64, Linux aarch64 and Windows. With fused multiply-adds, marginal geometry could take another branch
  there: a boolean or a tessellation could give a different result than on the other platforms.

### Fixed

- `Brep::squeeze_face`, and SM_API's `SmApiRemoveFillet` and `SmApiRemoveFilletFaces`, fail and
  leave the body as it was where copying it fails, instead of working on an incomplete copy or
  restoring the body from one (`SmBrep::GetCopyStatus`, new in usd-brep 1.0.2).

## [0.1.4] - 2026-11-23

Based on NVIDIA usd-brep 1.0.2.

### Changed

- SMLib and SM_API are those of NVIDIA usd-brep 1.0.2 (was 1.0.1), with its fixes: booleans of
  crossing cylinders, of cones and of nearly parallel curves that failed or gave invalid solids,
  transforms that set a torus's tolerances to 0, fillet rails that left their start in the wrong
  direction, `create_cylindrical_box` over a full turn, and native files that crashed the reader
  (they now fail with `SM_ERR_INVALID_INPUT`). Results can change: a boolean turns a cylinder, cone
  or other solid of revolution about its axis so that its seams clear the points where it touches
  the other body (the same shape, its faces split elsewhere); booleans of bodies under about 5
  units across merge intersection points less than 0.01 apart; `Brep::is_manifold_solid` is false
  for a body without a solid region; and `create_box`, `Brep::copy`, `Brep::shell`, `Brep::offset`
  (with self-intersection on) and `Edge::length` fail where they returned an incomplete or wrong
  result.

## [0.1.3] - 2026-11-23

Based on NVIDIA usd-brep 1.0.1.

### Added

- `Brep::from_rows` builds a body from `BrepRows`, the rows of SMLib's own B-rep data (regions,
  shells, face-uses, faces, loops, edge-uses, edges, vertices, b-spline curves and surfaces), in
  memory: the same body that writing them to a native file and reading it back with
  `Brep::read_from_file_rebuilding(path, true, true)` gives, without the file or its parsing. Rows
  that point past the others are refused with an error. In C: `cs_brep_from_rows`; in SMLib,
  `SmBSplineCurve::TakeOverNurb` and `SmBSplineSurface::TakeOverNurb` take over a NURB as the file
  readers build one, its knots as given.

## [0.1.2] - 2026-11-16

Based on NVIDIA usd-brep 1.0.1.

### Fixed

- `Edge::parameter_range` no longer reaches a rounding step past the edge's curve: where a boolean
  left an edge's interval starting 1e-16 before its curve's, `Edge::point_at` at the edge's own
  start failed with `SmApiEvaluateCurve` `SM_ERR_INVALID_INPUT`.

## [0.1.1] - 2026-11-14

Based on NVIDIA usd-brep 1.0.1.

### Fixed

- A fillet whose rail starts at a corner just past the end of the neighbouring filleted edge no
  longer fails: where no closest point on that edge is found, the start is looked for on the
  surface instead (a round over a rim that a smooth seam crosses failed at every radius).

## [0.1.0] - 2026-11-10

Based on NVIDIA usd-brep 1.0.1. The first release: SMLib and SM_API from usd-brep, without its
OpenUSD layer, Python bindings and build tooling, with the changes below.

### Added

- `smlib-sys`: compiles SMLib, SM_API and a C interface over them into one static library with
  Cargo, for Linux (x86_64, aarch64), macOS, Windows (MSVC), iOS (device and simulator) and
  `wasm32-unknown-emscripten` (WebAssembly exceptions, Emscripten's libc++ and libc++abi linked).
- `smlib`: safe Rust types over that interface: owned bodies and curves, borrowed faces, edges,
  vertices and surfaces; primitives, sweeps, lofts, booleans, fillets and chamfers, shells and
  offsets, healing, measurement, tessellation, native file reading and writing.
- SMLib in several threads at once: a context per thread and every global an operation writes
  `thread_local`, checked by a harness that compares every operation in 1 to N threads with a
  serial run, under ThreadSanitizer, and by a check of the library's writable globals
  (`tools/threads/`, `docs/concurrency.md`).
- Chamfers with two distances, one per face of the edge.
- Taking fillets off faces that sit on patches of their own, as STEP imports have them, and a
  tiny face squeezed away so that the faces around it meet at a vertex.
- `Brep::memory_used`, the bytes a body holds.
- `scripts/sync-upstream.sh`, which takes a new usd-brep release into the `nvidia` branch.

### Changed

- A planar face with many holes is built in linear time (600 holes: 3.2 s to 0.7 s).
- A large polygon on a plane (1000 corners and more) is tessellated ear by ear as a walk finds
  them (a plate with 600 holes: 3.0 s to 2.1 s, the same triangles).
- Where two G2 rounds meet at a box corner, the end of their intersection is straightened along
  the bisector, so it tessellates closed.
- A curve sweep with caps takes its caps from every side face that reaches the path's start or end.

### Fixed

- Crashes in fillets at corners SMLib had no case for, in booleans that left an edge without its
  vertex, and in rounding the seams of a rounded top (a vertex loop made twice, a box without a
  surface, a vertex used after it was glued away, an edge between two loops of one face deleted
  with that face). The inputs are in `smlib/tests/crashes/`.
- NURBS with unclamped knot vectors, as OpenCASCADE writes them to STEP, are clamped where SMLib
  reads them instead of crashing the volume.
- Builds with gcc 15 under `-Werror`.

[Unreleased]: https://github.com/abstractFlo/smlib-rs/compare/v0.1.4...HEAD
[0.1.4]: https://github.com/abstractFlo/smlib-rs/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/abstractFlo/smlib-rs/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/abstractFlo/smlib-rs/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/abstractFlo/smlib-rs/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/abstractFlo/smlib-rs/releases/tag/v0.1.0

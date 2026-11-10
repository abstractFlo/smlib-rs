# Changelog

What changed in smlib-rs, for people who use it. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) (before 1.0, a minor version may break the API).
`smlib` and `smlib-sys` always share one version. Each release says which usd-brep release of
NVIDIA's its SMLib is based on.

## [Unreleased]

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

[Unreleased]: https://github.com/abstractFlo/smlib-rs/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/abstractFlo/smlib-rs/releases/tag/v0.1.0

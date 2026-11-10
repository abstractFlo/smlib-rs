# smlib-rs

> **Unofficial.** SMLib source taken from NVIDIA's [usd-brep](https://github.com/NVIDIA-Omniverse/usd-brep)
> (Apache-2.0). Not affiliated with, endorsed by or supported by NVIDIA.

Rust bindings to **SMLib**, a NURBS boundary-representation (B-rep) solid modelling kernel:
primitives and sweeps, booleans, fillets and chamfers, shells and offsets, healing, measurement and
tessellation. SMLib was developed by Solid Modeling Solutions and released as open source by
NVIDIA as the kernel layer of usd-brep. This repository holds only that kernel, without usd-brep's
OpenUSD layer, Python bindings and build tooling, and builds it with Cargo.

```rust
use smlib::{boolean_difference, create_box, create_cylinder, thread};

fn main() -> smlib::Result<()> {
    thread::start();
    let plate = create_box([0.0, 0.0, 0.0], 40.0, 20.0, 10.0)?;
    let drill = create_cylinder([20.0, 10.0, -1.0], 3.0, 12.0)?;
    let part = boolean_difference(plate, drill)?;
    println!("{} faces, {:.1} mm³", part.face_count(), part.volume(1e-6)?);
    Ok(())
}
```

A longer one: `cargo run --release --example drilled_box`.

## Crates

| Crate | What it is |
|---|---|
| `smlib` | Safe Rust types: owned bodies and curves, borrowed faces, edges, vertices and surfaces |
| `smlib-sys` | Compiles SMLib, SM_API and a small C interface over them (`smlib-sys/shim/`) into one static library, and declares that interface |

```toml
[dependencies]
smlib = { git = "https://github.com/abstractFlo/smlib-rs" }
```

Building needs a C++17 compiler; the kernel compiles in about a minute. Targets built in CI:
Linux (x86_64, aarch64), macOS, Windows (MSVC), iOS and `wasm32-unknown-emscripten`.

## Threads

SMLib runs in several threads at once in this repository's version, with rules the types keep:
a thread calls `thread::start()` before its first call, a body is used by one thread at a time
(`Send`, not `Sync`), and a body is freed by the thread that built it. The details, and what is
checked how, are in [docs/concurrency.md](docs/concurrency.md); `tools/threads/` holds the harness
that compares every operation in 1 to N threads with a serial run.

## Layout

| Path | What it is |
|---|---|
| `source/SMLib/`, `source/SM_API/` | The kernel and its C++ API |
| `smlib-sys/`, `smlib/` | The two crates |
| `smlib/tests/crashes/` | Inputs SMLib once crashed on, run by `cargo test` |
| `tools/threads/` | Thread harness, ThreadSanitizer run, check of writable globals |
| `scripts/sync-upstream.sh` | Takes a new usd-brep release into the `nvidia` branch |

## Changes to SMLib and upstream

- `nvidia`: NVIDIA's releases, filtered to SMLib, SM_API and the licence. Never changed by hand.
- `main`: `nvidia` plus this repository's changes: SMLib in threads, crash fixes, two-distance
  chamfers, fillet removal, faster planar faces with many holes, NURBS with unclamped knots.

Files changed or added here carry a line saying so below their licence header. A new NVIDIA
release comes in with `scripts/sync-upstream.sh`, which updates `nvidia`; it is then merged into
`main`.

## Licence

Apache License 2.0, see [LICENSE](LICENSE). The SMLib and SM_API sources are
Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.

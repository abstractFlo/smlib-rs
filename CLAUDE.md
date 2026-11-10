# smlib-rs

SMLib and SM_API from NVIDIA's usd-brep (Apache-2.0), with a C interface and the Rust crates
`smlib-sys` and `smlib`. Unofficial; never present it as NVIDIA's.

## Every change carries its changelog entry

A commit that changes what users of the crates get (API, behaviour, results, fixes, supported
targets, build requirements) adds one entry to `## [Unreleased]` in `CHANGELOG.md`, in the same
commit, under Added, Changed, Fixed or Removed: a sentence for someone using the crates, not the
commit subject. Changes users do not see (CI, tests, tooling, comments) need none. Releases follow
`RELEASING.md`.

## Changes to NVIDIA's files

`source/` is NVIDIA's code. A file changed or added here carries, below its licence header,
`// Modified in smlib-rs, not part of an NVIDIA release: the changes are in this repository's git history.`
(or `// Added in smlib-rs, not part of an NVIDIA release.`), as Apache-2.0 §4b asks. NVIDIA's
copyright lines stay. `nvidia` holds NVIDIA's releases as they are (`scripts/sync-upstream.sh`)
and is never changed by hand.

## Identity

Commits and tags are made as `abstractFlo <f.uhlrich@gmx.de>` (the repository's local git config);
no other address appears in this repository's history.

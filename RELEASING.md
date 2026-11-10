# Releasing

A release is a tag `v<version>` on `main`; the release workflow (`.github/workflows/release.yml`)
checks it, builds and tests it, and publishes a GitHub release with the version's section of
`CHANGELOG.md` as its notes. Nothing goes to crates.io yet.

1. **Changelog.** Every change already added its entry under `## [Unreleased]`. Rename that heading
   to `## [<version>] - <date>`, say which usd-brep release it is based on, and put a fresh
   `## [Unreleased]` above it. Update the compare links at the bottom.
2. **Version.** Set `version` in `smlib-sys/Cargo.toml` and `smlib/Cargo.toml`, and the
   `smlib-sys` dependency's `version` in `smlib/Cargo.toml`, to `<version>`. Before 1.0, a change
   that breaks the API raises the minor version (0.1 to 0.2), anything else the patch version.
3. **Check.** `scripts/release-notes.sh v<version>` prints the notes or says what does not fit.
4. **Commit and tag** as `abstractFlo <f.uhlrich@gmx.de>`:
   `git commit -m "Release <version>"`, `git tag -a v<version> -m "smlib-rs <version>"`.
5. **Push** `main` and the tag: `git push origin main v<version>`. The workflow does the rest.

Projects using smlib-rs pin the tag: `smlib = { git = "https://github.com/abstractFlo/smlib-rs", tag = "v<version>" }`.

## A new usd-brep release

`scripts/sync-upstream.sh` brings it into `nvidia`; merge `nvidia` into `main`, add a Changed
entry naming the new usd-brep version, test, and release as above.

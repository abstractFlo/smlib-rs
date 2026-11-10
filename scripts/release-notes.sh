#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Checks that a release tag fits the repository and prints its notes: the tag is v<version>, both
# crates carry that version, and CHANGELOG.md has a section for it (its text is what is printed).
# The release workflow runs it; run it before tagging too.
#
#   scripts/release-notes.sh v0.1.0
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:?usage: scripts/release-notes.sh v<version>}"
VERSION="${TAG#v}"
[ "$TAG" = "v$VERSION" ] || { echo "a release tag is v<version>, not $TAG" >&2; exit 1; }

for manifest in smlib-sys/Cargo.toml smlib/Cargo.toml; do
  found="$(sed -n 's/^version = "\(.*\)"$/\1/p' "$ROOT/$manifest" | head -1)"
  [ "$found" = "$VERSION" ] || { echo "$manifest is at $found, the tag says $VERSION" >&2; exit 1; }
done
grep -q "^smlib-sys = { path = \"../smlib-sys\", version = \"$VERSION\" }$" "$ROOT/smlib/Cargo.toml" \
  || { echo "smlib/Cargo.toml does not depend on smlib-sys $VERSION" >&2; exit 1; }

notes="$(awk -v version="$VERSION" '
  $0 ~ "^## \\[" version "\\]" { inside = 1; next }
  inside && /^## \[/ { exit }
  inside && /^\[[^]]+\]: / { exit }
  inside { print }
' "$ROOT/CHANGELOG.md")"
[ -n "$(printf '%s' "$notes" | tr -d '[:space:]')" ] || { echo "CHANGELOG.md has no section for $VERSION" >&2; exit 1; }
printf '%s\n' "$notes"

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Takes NVIDIA's usd-brep into the `nvidia` branch, filtered to SMLib, SM_API and LICENSE.
# The filter rewrites the same commits to the same ids every time, so commits `nvidia` already has
# keep their ids and the update is a fast-forward; anything else stops with an error. Merging
# `nvidia` into `main` afterwards is left to the person running it.
#
#   scripts/sync-upstream.sh
#
# UPSTREAM and UPSTREAM_BRANCH override where it fetches from (default: NVIDIA's repository, main).
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
UPSTREAM="${UPSTREAM:-https://github.com/NVIDIA-Omniverse/usd-brep.git}"
UPSTREAM_BRANCH="${UPSTREAM_BRANCH:-main}"
KEPT_PATHS='^(source/SMLib/|source/SM_API/|LICENSE$)'

workspace="$(mktemp -d)"
trap 'rm -rf "$workspace"' EXIT

echo "==> fetching $UPSTREAM ($UPSTREAM_BRANCH)"
git clone --quiet --bare --single-branch --branch "$UPSTREAM_BRANCH" --no-tags "$UPSTREAM" "$workspace/upstream.git"

echo "==> filtering to SMLib, SM_API and LICENSE"
(
  cd "$workspace/upstream.git"
  KEPT_PATHS="$KEPT_PATHS" FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch --prune-empty --index-filter '
    git ls-files -z | grep -zvE "$KEPT_PATHS" | xargs -0 -r git rm --cached -q --
  ' -- "$UPSTREAM_BRANCH" >/dev/null
)

cd "$ROOT"
git fetch --quiet --no-tags "$workspace/upstream.git" "$UPSTREAM_BRANCH"
previous="$(git rev-parse nvidia)"
filtered="$(git rev-parse FETCH_HEAD)"

if [ "$previous" = "$filtered" ]; then
  echo "nvidia is up to date ($(git log -1 --format='%h %s' nvidia))"
  exit 0
fi
if ! git merge-base --is-ancestor "$previous" "$filtered"; then
  echo "error: the filtered upstream does not continue nvidia ($previous); nothing changed" >&2
  exit 1
fi

git update-ref refs/heads/nvidia "$filtered" "$previous"
echo "nvidia moved on:"
git log --oneline "$previous..nvidia"
echo
echo "next: on main, git merge nvidia"

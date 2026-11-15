#!/usr/bin/env bash
# Portable shell entry point. The implementation is Python so the same patched-tree preparation runs natively on
# Windows and in the Nix shells without changing the pinned submodule.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$ROOT/tools/ps3recomp_patched.py"

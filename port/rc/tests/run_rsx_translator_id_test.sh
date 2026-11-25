#!/usr/bin/env bash
# Host test of port/rc/rsx_translator_id.cmake (the production code, via cmake -P) against the Python mirror of the rule
# (tools/check_translator_define.py): same id, non-zero, follows a file change, an empty libs/video is refused.
# Run where cmake is on PATH: nix develop --command port/rc/tests/run_rsx_translator_id_test.sh [tree]
# (tree default work/ps3recomp-patched). No build, no game data.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root="$here/../../.."
tree=${1:-"$root/work/ps3recomp-patched"}
python3 -I "$root/tools/test_translator_id_rule.py" "$tree"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/libs/video"
cp "$tree"/libs/video/*.c "$tree"/libs/video/*.h "$tmp/libs/video/"
want() {
    python3 -I -c 'import sys,importlib.util as u;s=u.spec_from_file_location("c",sys.argv[1]);m=u.module_from_spec(s);s.loader.exec_module(m);print(m.tree_id(sys.argv[2])[0])' \
        "$root/tools/check_translator_define.py" "$1"
}
got() { cmake -DTREE="$1" -P "$here/rsx_translator_id_print.cmake" 2>&1 | sed -n 's/.*RSX_TRANSLATOR_ID \([0-9a-f]*\) .*/\1/p'; }
a=$(got "$tmp")
[[ -n "$a" && "$a" != 0000000000000000 && "$a" == "$(want "$tmp")" ]] || { echo "FAIL cmake id '$a' != python $(want "$tmp")"; exit 1; }
printf ' ' >> "$tmp/libs/video/rsx_fp_decompiler.c"
b=$(got "$tmp")
[[ "$b" == "$(want "$tmp")" && "$b" != "$a" ]] || { echo "FAIL id did not follow the file change ($a -> $b)"; exit 1; }
mkdir -p "$tmp/empty/libs/video"
if cmake -DTREE="$tmp/empty" -P "$here/rsx_translator_id_print.cmake" >/dev/null 2>&1; then
    echo "FAIL negative control: an empty libs/video was accepted"
    exit 1
fi
echo "run_rsx_translator_id_test.sh: ok ($a -> $b)"

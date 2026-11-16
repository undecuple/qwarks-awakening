#!/usr/bin/env python3
"""Host test for tools/spu/spu_lift.py's title-specific overlay stack-reset registrations."""
import importlib.util
import json
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("spu_lift", ROOT / "tools/spu/spu_lift.py")
SPU_LIFT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SPU_LIFT)

FAILS = []
DECLARATION = "extern void spu_overlay_register_sig(const uint8_t sig[16], int image_id);\n"
MARKER = "    spu_begin_image(0);\n"
GENERATED = DECLARATION + "\nvoid tods_jobmods_register_all(void)\n{\n" + MARKER + "}\n"


def check(condition, what):
    print(("ok   " if condition else "FAIL ") + what)
    if not condition:
        FAILS.append(what)


def rejects(path, resets, exception, what):
    before = path.read_text(encoding="utf-8")
    try:
        SPU_LIFT.add_overlay_stack_resets(path, resets)
    except exception:
        check(path.read_text(encoding="utf-8") == before, what)
    else:
        check(False, what)


with tempfile.TemporaryDirectory(prefix="spu-lift-test-") as temp:
    generated = Path(temp) / "jobmods_register.c"
    generated.write_text(GENERATED, encoding="utf-8")
    config = json.loads((ROOT / "data/titles/tods_spu_config.json").read_text(encoding="utf-8"))
    SPU_LIFT.add_overlay_stack_resets(generated, config["overlay_stack_resets"])
    result = generated.read_text(encoding="utf-8")
    check(result.count("extern void spu_register_stack_reset_entry(uint32_t entry, int image_id);") == 1,
          "declares the stack-reset registration API once")
    check(result.count("spu_register_stack_reset_entry(0x07808u, 207);") == 1,
          "emits the ToD resident-overlay reset")
    check(result.index("spu_register_stack_reset_entry") < result.index("spu_begin_image(0)"),
          "registers resets before restoring the default image")

    generated.write_text(GENERATED, encoding="utf-8")
    SPU_LIFT.add_overlay_stack_resets(generated, [])
    check(generated.read_text(encoding="utf-8") == GENERATED, "empty reset list leaves output unchanged")

    invalid = [
        ({"entry": "0x7808"}, ValueError, "rejects missing image id"),
        ({"entry": "0x7808", "image_id": 207, "extra": 1}, ValueError, "rejects unknown fields"),
        ({"entry": 1.5, "image_id": 207}, ValueError, "rejects non-integral entry"),
        ({"entry": True, "image_id": 207}, ValueError, "rejects boolean entry"),
        ({"entry": "not-an-address", "image_id": 207}, ValueError, "rejects malformed entry"),
        ({"entry": "0x7809", "image_id": 207}, ValueError, "rejects unaligned entry"),
        ({"entry": "0x40000", "image_id": 207}, ValueError, "rejects entry outside local storage"),
        ({"entry": "0x7808", "image_id": 0}, ValueError, "rejects nonpositive image id"),
        ({"entry": "0x7808", "image_id": True}, ValueError, "rejects boolean image id"),
        ({"entry": "0x7808", "image_id": "207"}, ValueError, "rejects non-integral image id"),
    ]
    for reset, exception, what in invalid:
        generated.write_text(GENERATED, encoding="utf-8")
        rejects(generated, [reset], exception, what)

    generated.write_text(GENERATED, encoding="utf-8")
    rejects(generated, {}, ValueError, "rejects a non-list reset collection")
    generated.write_text("void unexpected(void) {}\n", encoding="utf-8")
    rejects(generated, [{"entry": "0x7808", "image_id": 207}], RuntimeError,
            "rejects an unexpected generator output shape")

if FAILS:
    raise SystemExit("FAILED: " + ", ".join(FAILS))
print("PASS overlay stack-reset generation")

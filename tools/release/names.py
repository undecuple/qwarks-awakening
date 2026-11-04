"""The product's public name (Qwark's Awakening: a fan project's name, after a character from the series; not
official), its tag, links and release asset names in one place, as the Killzone 3 port's tools/release/names.py (same
author, MIT). The release tools take their names from here; public/README.md carries the same name and tag by hand.
"""

# The release version: vMAJOR.MINOR.PATCH tags, a pre-release while the major version is 0. Mirrors:
# port/rc/CMakeLists.txt project VERSION (the exe; tools/build-rc.sh checks RC_RELEASE_TAG against it) and
# CITATION.cff version. tools/release/bump_version.py edits them together; --agree checks. A release tag must be
# "v" + VERSION (the release workflow refuses any other).
VERSION = "0.2.1"
# The game the setup accepts (tools/lift.sh lifts this EBOOT): Tools of Destruction, US disc, PARAM.SFO VERSION
# 02.00, no update. The recomp card (.recomp.json) original.serial / revision repeat these.
SERIAL = "BCUS98127"
REGION = "US"
DISC_VERSION = "02.00"
UPDATE_VERSION = None                               # no update PKG is applied (none exists in the library for ToD)

PRODUCT = "Qwark's Awakening"                       # every title a player reads (art, later installer and launcher)
TAG = "Ratchet & Clank (PS3) // Unofficial PC Port"   # the "GAME // UNOFFICIAL PC PORT" tag (art: upper case)
DESCRIPTION = f"{TAG} (static recompilation)"        # public/README.md's second line
WORDMARK = ("QWARK'S", "AWAKENING")                 # the art's wordmark: gold word, steel word
OWNER = "undecuple"
REPO_SLUG = "qwarks-awakening"                       # github.com/OWNER/REPO_SLUG

REPO_URL = f"https://github.com/{OWNER}/{REPO_SLUG}"
NAME_NOTE = ("Qwark's Awakening is a fan project, not affiliated with or endorsed by Sony Interactive Entertainment "
             "or Insomniac Games.")

# Release assets (the release workflow and pack_zip.py; test_names.py checks the shapes). The launcher zip carries the
# launcher preview only: never a game program, never a game file.
ASSET_STEM = "Qwarks-Awakening"                    # PRODUCT without the apostrophe, words joined by '-'
LAUNCHER_EXE = "Qwarks-Awakening-launcher.exe"      # the launcher's name inside the zip


def launcher_zip_name(version):
    """The launcher's release asset: Qwarks-Awakening-<version>-launcher-windows-x64.zip (+ .sha256 beside it)."""
    return f"{ASSET_STEM}-{version}-launcher-windows-x64.zip"


def launcher_zip_top(version):
    """The one folder every zip entry sits under."""
    return f"{ASSET_STEM}-{version}-launcher"

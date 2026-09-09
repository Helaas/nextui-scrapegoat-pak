#!/usr/bin/env python3
"""Generate resources/systems.json, the shipped platform catalog.

Imports the full ScreenScraper platform list and the libretro cheat-database
inventory, then applies the reviewed decisions in this file: local platform
identities, provider bindings, suffix defaults and suffix candidate sets.

Identity is preserved across regenerations. A ScreenScraper ID keeps the local
slug it was first given, so a user override written on a device survives an
upstream rename. New platforms get a slug proposed once; a collision is a
review item, never an automatic rename.

Credentials come from the environment or .env.local, following the existing
build convention. Credential-bearing URLs are never printed, and the device
runtime never contacts a provider to resolve a mapping.

Standard library only.

    python3 scripts/gen_systems_catalog.py --output /tmp/candidate.json
    python3 scripts/gen_systems_catalog.py --publish
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CATALOG_PATH = REPO_ROOT / "resources" / "systems.json"
BASELINE_PATH = REPO_ROOT / "tests" / "fixtures" / "baseline_mappings.json"
ENV_LOCAL = REPO_ROOT / ".env.local"

SS_API_BASE = "https://api.screenscraper.fr/api2"
SS_SOFT_NAME = "ScrapeGoat-v2.1.0"
LIBRETRO_REPO = "libretro/libretro-database"
GITHUB_API = "https://api.github.com"
HTTP_TIMEOUT = 60

# ── Reviewed platform identities ──────────────────────────────
#
# ScreenScraper ID -> (local id, display name). The local id is permanent: it
# is what a user override on an SD card refers to. Upstream may rename a
# platform; only `name` follows.

IDENTITY: dict[int, tuple[str, str]] = {
    1: ("megadrive", "Mega Drive / Genesis"),
    2: ("mastersystem", "Master System"),
    3: ("nes", "NES / Famicom"),
    4: ("snes", "Super Nintendo / Super Famicom"),
    9: ("gameboy", "Game Boy"),
    10: ("gameboycolor", "Game Boy Color"),
    11: ("virtualboy", "Virtual Boy"),
    12: ("gameboyadvance", "Game Boy Advance"),
    14: ("nintendo64", "Nintendo 64"),
    15: ("nintendods", "Nintendo DS"),
    19: ("sega32x", "Sega 32X"),
    20: ("segacd", "Mega-CD / Sega CD"),
    21: ("gamegear", "Game Gear"),
    22: ("saturn", "Sega Saturn"),
    23: ("dreamcast", "Dreamcast"),
    25: ("neogeopocket", "Neo Geo Pocket"),
    26: ("atari2600", "Atari 2600"),
    27: ("jaguar", "Atari Jaguar"),
    28: ("lynx", "Atari Lynx"),
    29: ("threedo", "3DO"),
    31: ("pcengine", "PC Engine / TurboGrafx-16"),
    40: ("atari5200", "Atari 5200"),
    41: ("atari7800", "Atari 7800"),
    43: ("atari8bit", "Atari 8-bit (400/800/XL/XE)"),
    45: ("wonderswan", "WonderSwan"),
    46: ("wonderswancolor", "WonderSwan Color"),
    48: ("colecovision", "ColecoVision"),
    52: ("gameandwatch", "Game & Watch"),
    57: ("playstation", "PlayStation"),
    61: ("psp", "PlayStation Portable"),
    64: ("amiga", "Commodore Amiga"),
    65: ("amstradcpc", "Amstrad CPC"),
    66: ("commodore64", "Commodore 64"),
    70: ("neogeocd", "Neo Geo CD"),
    73: ("vic20", "VIC-20"),
    75: ("mame", "Arcade (MAME / FBNeo)"),
    82: ("neogeopocketcolor", "Neo Geo Pocket Color"),
    99: ("commodoreplus4", "Commodore Plus/4"),
    104: ("odyssey2", "Magnavox Odyssey² / Philips Videopac"),
    105: ("supergrafx", "PC Engine SuperGrafx"),
    106: ("famicomdisksystem", "Famicom Disk System"),
    109: ("sg1000", "SG-1000"),
    113: ("msx", "MSX"),
    115: ("intellivision", "Intellivision"),
    123: ("scummvm", "ScummVM"),
    211: ("pokemonmini", "Pokémon Mini"),
    222: ("tic80", "TIC-80"),
    231: ("easyrpg", "RPG Maker 2000/2003 (EasyRPG)"),
    234: ("pico8", "PICO-8"),
    240: ("commodorepet", "Commodore PET"),
    290: ("prboom", "Doom (PrBoom)"),
    302: ("j2me", "Java ME (J2ME)"),
}

# ── Reviewed cheat-database bindings ──────────────────────────
#
# local id -> exact libretro-database cht/ directory name. Every entry is a
# directory that exists in the imported inventory; the generator fails if one
# disappears rather than dropping the binding silently.

LIBRETRO_BINDINGS: dict[str, str] = {
    "nes": "Nintendo - Nintendo Entertainment System",
    "gameboy": "Nintendo - Game Boy",
    "gameboyadvance": "Nintendo - Game Boy Advance",
    "gameboycolor": "Nintendo - Game Boy Color",
    "megadrive": "Sega - Mega Drive - Genesis",
    "playstation": "Sony - PlayStation",
    "snes": "Nintendo - Super Nintendo Entertainment System",
    "sega32x": "Sega - 32X",
    "atari2600": "Atari - 2600",
    "atari5200": "Atari - 5200",
    "atari7800": "Atari - 7800",
    "atari8bit": "Atari - 8-bit Family",
    "colecovision": "Coleco - ColecoVision",
    "dreamcast": "Sega - Dreamcast",
    "mame": "FBNeo - Arcade Games",
    "famicomdisksystem": "Nintendo - Family Computer Disk System",
    "gamegear": "Sega - Game Gear",
    "intellivision": "Mattel - Intellivision",
    "jaguar": "Atari - Jaguar",
    "lynx": "Atari - Lynx",
    "msx": "Microsoft - MSX - MSX2 - MSX2P - MSX Turbo R",
    "nintendo64": "Nintendo - Nintendo 64",
    "nintendods": "Nintendo - Nintendo DS",
    "pcengine": "NEC - PC Engine - TurboGrafx 16",
    "psp": "Sony - PlayStation Portable",
    "segacd": "Sega - Mega-CD - Sega CD",
    "mastersystem": "Sega - Master System - Mark III",
    "saturn": "Sega - Saturn",
    "supergrafx": "NEC - PC Engine SuperGrafx",
    "tic80": "TIC-80",
    # Reviewed additions beyond the pre-catalog tables.
    "sg1000": "Sega - SG-1000",
    "prboom": "PrBoom",
}

# Providers reviewed and found to carry nothing for a platform. Recorded so the
# audit can separate "checked, unavailable" from "not verified yet".
VERIFIED_UNAVAILABLE: dict[str, list[str]] = {
    "commodorepet": ["libretro_dir"],
    "commodoreplus4": ["libretro_dir"],
    "vic20": ["libretro_dir"],
    "amstradcpc": ["libretro_dir"],
    "amiga": ["libretro_dir"],
    "neogeocd": ["libretro_dir"],
    "odyssey2": ["libretro_dir"],
    "wonderswan": ["libretro_dir"],
    "wonderswancolor": ["libretro_dir"],
    "gameandwatch": ["libretro_dir"],
    "j2me": ["libretro_dir"],
    "scummvm": ["libretro_dir"],
    "easyrpg": ["libretro_dir"],
    "pico8": ["libretro_dir"],
    "pokemonmini": ["libretro_dir"],
    "virtualboy": ["libretro_dir"],
    "neogeopocket": ["libretro_dir"],
    "neogeopocketcolor": ["libretro_dir"],
    "threedo": ["libretro_dir"],
}

# ── Reviewed suffix decisions ─────────────────────────────────
#
# An unambiguous suffix gets a bundled default. A suffix whose emulator spans
# several scraping platforms gets candidates and no default, so no folder is
# silently misclassified.

TAG_DEFAULTS: dict[str, str] = {
    # NextUI base
    "FC": "nes", "GB": "gameboy", "GBA": "gameboyadvance",
    "GBC": "gameboycolor", "MD": "megadrive", "PS": "playstation",
    "SFC": "snes",
    # NextUI extras
    "32X": "sega32x", "A2600": "atari2600", "A5200": "atari5200",
    "A7800": "atari7800", "COLECO": "colecovision", "CPC": "amstradcpc",
    "C64": "commodore64", "FBN": "mame", "FDS": "famicomdisksystem",
    "GG": "gamegear", "LYNX": "lynx", "MGBA": "gameboyadvance",
    "MSX": "msx", "NGP": "neogeopocket", "NGPC": "neogeopocketcolor",
    "P8": "pico8", "PCE": "pcengine", "PET": "commodorepet",
    "PKM": "pokemonmini", "PLUS4": "commodoreplus4", "PRBOOM": "prboom",
    "PUAE": "amiga", "SEGACD": "segacd", "SG1000": "sg1000",
    "SGB": "gameboy", "SMS": "mastersystem", "SUPA": "snes",
    "VB": "virtualboy", "VIC": "vic20",
    # Pak Store emulators
    "3DO": "threedo", "A800": "atari8bit", "DC": "dreamcast",
    "EASYRPG": "easyrpg", "GW": "gameandwatch", "INTV": "intellivision",
    "J2ME": "j2me", "JAGUAR": "jaguar", "N64": "nintendo64",
    "NDS": "nintendods", "NEOCD": "neogeocd", "O2": "odyssey2",
    "PICO": "pico8", "PSP": "psp", "SCUMMVM": "scummvm",
    "ScummVM": "scummvm", "SGX": "supergrafx", "SMSU": "megadrive",
    "SS": "saturn", "SWAN": "playstation", "TIC": "tic80",
    # Legacy alias with no currently observed pak; retained deliberately.
    "SUPERGRAFX": "supergrafx",
}

TAG_CANDIDATES: dict[str, list[str]] = {
    # One emulator, five documented ROM folders. A bundled default would
    # misclassify four of them.
    "GPGX": ["megadrive", "mastersystem", "gamegear", "sg1000", "segacd"],
    # The Atari 800 core also runs Atari 5200 media. The pre-catalog default
    # is kept for compatibility, with the second platform offered.
    "A800": ["atari8bit", "atari5200"],
    # The Beetle WonderSwan core covers both libraries; neither is a safe
    # default, so this suffix has candidates only.
    "WSC": ["wonderswan", "wonderswancolor"],
    # C128 lost its bundled default (see CORRECTIONS). ScreenScraper has no
    # Commodore 128 platform; most C128 folders hold C64 software.
    "C128": ["commodore64"],
    # DICE emulates discrete-logic arcade hardware. MAME catalogues many of
    # those titles, but the match is per-game, so it is offered, not assumed.
    "DICE": ["mame"],
}

# Suffixes reviewed as having no suitable catalog target. Recorded here so the
# audit can tell a completed review from work not yet done. This is not a
# hidden or ignored sentinel: these folders stay visible and mappable.
NO_TARGET_REVIEWED: dict[str, str] = {
    "PORTS": "PortMaster installs user-chosen native game ports; the content "
             "is open-ended, so no single platform applies.",
    "ZQUEST": "Zelda Classic quests are user-authored content with no "
              "platform entry at either provider.",
    "MKXPZ": "mkxp-z runs RPG Maker XP/VX/VX Ace projects; neither provider "
             "catalogues them as a platform.",
}

# Deliberate, reviewed deviations from the pre-catalog tables in src/systems.c.
CORRECTIONS: dict[str, str] = {
    "COLECO": "was ScreenScraper 60, which is PlayStation 4. ColecoVision is "
              "48. Every COLECO scrape made against the old table queried the "
              "wrong platform.",
    "MSX": "was ScreenScraper 62, which is PS Vita. MSX is 113.",
    "C128": "was ScreenScraper 87, which is the Amstrad GX4000. ScreenScraper "
            "has no Commodore 128 platform, so the suffix now offers "
            "Commodore 64 as a candidate instead of carrying a wrong default.",
}


class GeneratorError(Exception):
    pass


# ── Fetching ──────────────────────────────────────────────────


def load_env() -> None:
    if not ENV_LOCAL.is_file():
        return
    for line in ENV_LOCAL.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def fetch(url: str, *, redacted: str) -> bytes:
    """Fetch a URL. `redacted` is what appears in any error message."""
    request = urllib.request.Request(url, headers={
        "User-Agent": "scrapegoat-gen-systems-catalog/1",
        "Accept": "application/json",
    })
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token and url.startswith(GITHUB_API):
        request.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        raise GeneratorError(f"{redacted}: HTTP {exc.code} {exc.reason}") from None
    except (urllib.error.URLError, OSError) as exc:
        raise GeneratorError(f"{redacted}: {type(exc).__name__}") from None


def fetch_screenscraper() -> tuple[list[dict], str]:
    dev_id = os.environ.get("SCREENSCRAPER_DEV_ID")
    dev_password = os.environ.get("SCREENSCRAPER_DEV_PASSWORD")
    if not dev_id or not dev_password:
        raise GeneratorError(
            "set SCREENSCRAPER_DEV_ID and SCREENSCRAPER_DEV_PASSWORD in "
            ".env.local or the environment")

    params = {
        "devid": dev_id, "devpassword": dev_password,
        "softname": SS_SOFT_NAME, "output": "json",
    }
    user = os.environ.get("SCREENSCRAPER_USER")
    password = os.environ.get("SCREENSCRAPER_PASSWORD")
    if user and password:
        params.update(ssid=user, sspassword=password)

    url = f"{SS_API_BASE}/systemesListe.php?" + urllib.parse.urlencode(params)
    raw = fetch(url, redacted=f"{SS_API_BASE}/systemesListe.php")
    digest = hashlib.sha256(raw).hexdigest()
    try:
        systems = json.loads(raw)["response"]["systemes"]
    except (ValueError, KeyError, TypeError) as exc:
        raise GeneratorError(f"unexpected systemesListe response: {exc}") from None
    if not isinstance(systems, list) or not systems:
        raise GeneratorError("systemesListe returned no systems")
    return systems, digest


def fetch_libretro_cht() -> tuple[list[str], str]:
    head = json.loads(fetch(f"{GITHUB_API}/repos/{LIBRETRO_REPO}/commits/master",
                            redacted=f"{LIBRETRO_REPO} master"))
    commit = head["sha"]
    listing = json.loads(fetch(
        f"{GITHUB_API}/repos/{LIBRETRO_REPO}/contents/cht?ref={commit}",
        redacted=f"{LIBRETRO_REPO} cht/"))
    dirs = sorted(item["name"] for item in listing if item.get("type") == "dir")
    if not dirs:
        raise GeneratorError("the libretro cht/ inventory came back empty")
    return dirs, commit


# ── Building ──────────────────────────────────────────────────


SLUG_STRIP = re.compile(r"[^a-z0-9]+")


def propose_slug(name: str) -> str:
    folded = unicodedata.normalize("NFKD", name)
    folded = "".join(c for c in folded if not unicodedata.combining(c))
    slug = SLUG_STRIP.sub("", folded.lower())
    return slug or "platform"


def platform_names(system: dict) -> tuple[str, list[str]]:
    noms = system.get("noms") or {}
    primary = (noms.get("nom_eu") or noms.get("nom_us")
               or (noms.get("noms_commun") or "").split(",")[0])
    primary = (primary or "").strip()
    aliases: list[str] = []
    for key in ("nom_eu", "nom_us", "nom_recalbox", "nom_retropie",
                "nom_launchbox", "nom_hyperspin", "noms_commun"):
        for value in str(noms.get(key) or "").split(","):
            value = value.strip()
            if value and value != primary and value not in aliases:
                aliases.append(value)
    return primary, aliases


def build_catalog(systems: list[dict], cht_dirs: list[str],
                  ss_digest: str, cht_commit: str) -> dict:
    by_id = {}
    for system in systems:
        try:
            ss_id = int(system.get("id"))
        except (TypeError, ValueError):
            continue
        if ss_id > 0:
            by_id[ss_id] = system

    missing = sorted(set(IDENTITY) - set(by_id))
    if missing:
        raise GeneratorError(
            "reviewed ScreenScraper IDs are absent from the imported list: "
            f"{missing}. Upstream removals are reported, never silently dropped")

    available = set(cht_dirs)
    unknown_dirs = {pid: directory for pid, directory in LIBRETRO_BINDINGS.items()
                    if directory not in available}
    if unknown_dirs:
        raise GeneratorError(
            "reviewed cheat directories are absent from the imported "
            f"inventory: {unknown_dirs}")

    slugs: dict[int, str] = dict((k, v[0]) for k, v in IDENTITY.items())
    used = {}
    for ss_id, slug in slugs.items():
        if slug in used:
            raise GeneratorError(
                f"reviewed slug '{slug}' is claimed by ScreenScraper IDs "
                f"{used[slug]} and {ss_id}")
        used[slug] = ss_id

    platforms = []
    disambiguated: list[tuple[str, str, int]] = []
    for ss_id in sorted(by_id):
        system = by_id[ss_id]
        name, aliases = platform_names(system)
        if ss_id in IDENTITY:
            slug, name = IDENTITY[ss_id][0], IDENTITY[ss_id][1]
            upstream, _ = platform_names(system)
            if upstream and upstream != name and upstream not in aliases:
                aliases.insert(0, upstream)
        else:
            if not name:
                continue
            slug = propose_slug(name)
            if slug in used:
                # Deterministic and permanent, but a review item: a reviewer
                # can promote a readable slug into IDENTITY, and existing
                # overrides keep resolving either way because the ss_id ->
                # slug association is what is preserved.
                slug = f"{slug}-{ss_id}"
                disambiguated.append((slug, name, ss_id))
            if slug in used:
                raise GeneratorError(
                    f"slug '{slug}' for ScreenScraper {ss_id} still collides "
                    f"with {used[slug]}; assign a reviewed slug in IDENTITY")
            used[slug] = ss_id

        entry = {"id": slug, "name": name, "ss_id": ss_id}
        if aliases:
            entry["aliases"] = aliases[:12]
        directory = LIBRETRO_BINDINGS.get(slug)
        if directory:
            entry["libretro_dir"] = directory
        unavailable = VERIFIED_UNAVAILABLE.get(slug)
        if unavailable:
            entry["verified_unavailable"] = unavailable
        platforms.append(entry)

    known = {entry["id"] for entry in platforms}
    for tag, pid in TAG_DEFAULTS.items():
        if pid not in known:
            raise GeneratorError(f"suffix default {tag} -> {pid} has no platform")
    for tag, ids in TAG_CANDIDATES.items():
        for pid in ids:
            if pid not in known:
                raise GeneratorError(f"suffix candidate {tag} -> {pid} has no platform")

    bound = set(LIBRETRO_BINDINGS.values())
    return {
        "schema": 1,
        "generated": {
            "generator": "scripts/gen_systems_catalog.py",
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "screenscraper_systems": len(by_id),
            "screenscraper_sha256": ss_digest,
            "libretro_database_commit": cht_commit,
            "libretro_cht_directories": len(cht_dirs),
            "libretro_cht_unbound": sorted(set(cht_dirs) - bound),
            "auto_disambiguated_slugs": [
                {"id": slug, "name": name, "ss_id": ss_id}
                for slug, name, ss_id in disambiguated
            ],
            "no_target_reviewed": NO_TARGET_REVIEWED,
            "corrections": CORRECTIONS,
        },
        "platforms": platforms,
        "tags": dict(sorted(TAG_DEFAULTS.items())),
        "tag_candidates": {k: list(v) for k, v in sorted(TAG_CANDIDATES.items())},
    }


# ── Baseline comparison ───────────────────────────────────────


def compare_with_baseline(catalog: dict) -> list[str]:
    """Report every pre-catalog association the new catalog does not reproduce."""
    if not BASELINE_PATH.is_file():
        return [f"baseline fixture {BASELINE_PATH.name} is missing"]
    baseline = json.loads(BASELINE_PATH.read_text(encoding="utf-8"))
    platforms = {p["id"]: p for p in catalog["platforms"]}
    tags = catalog["tags"]

    differences: list[str] = []
    for tag, ss_id in baseline["screenscraper"].items():
        platform = platforms.get(tags.get(tag, ""))
        current = platform.get("ss_id") if platform else None
        if current != ss_id:
            note = CORRECTIONS.get(tag)
            differences.append(
                f"{tag}: ScreenScraper {ss_id} -> {current}"
                + (f" (reviewed correction: {note})" if note
                   else " (UNREVIEWED regression)"))
    for tag, directory in baseline["libretro"].items():
        platform = platforms.get(tags.get(tag, ""))
        current = platform.get("libretro_dir") if platform else None
        if current != directory:
            note = CORRECTIONS.get(tag)
            differences.append(
                f"{tag}: cheats {directory!r} -> {current!r}"
                + (f" (reviewed correction: {note})" if note
                   else " (UNREVIEWED regression)"))
    return differences


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", help="write the candidate catalog here")
    parser.add_argument("--publish", action="store_true",
                        help=f"write {CATALOG_PATH.relative_to(REPO_ROOT)} itself")
    args = parser.parse_args(argv)

    if not args.output and not args.publish:
        parser.error("pass --output for a candidate, or --publish to replace "
                     "the bundled catalog")

    load_env()
    try:
        systems, ss_digest = fetch_screenscraper()
        cht_dirs, cht_commit = fetch_libretro_cht()
        catalog = build_catalog(systems, cht_dirs, ss_digest, cht_commit)
    except GeneratorError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    differences = compare_with_baseline(catalog)
    unreviewed = [d for d in differences if "UNREVIEWED" in d]
    for difference in differences:
        print(f"baseline: {difference}", file=sys.stderr)
    if unreviewed:
        print(f"error: {len(unreviewed)} pre-catalog association(s) changed "
              "without a reviewed correction", file=sys.stderr)
        return 1

    body = json.dumps(catalog, indent=2, ensure_ascii=False) + "\n"
    destination = Path(args.output) if args.output else CATALOG_PATH
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(body, encoding="utf-8")

    auto = catalog["generated"]["auto_disambiguated_slugs"]
    if auto:
        print(f"note: {len(auto)} platform id(s) were disambiguated "
              "automatically; promote readable slugs into IDENTITY if any of "
              "them ever needs one: "
              + ", ".join(a["id"] for a in auto), file=sys.stderr)

    print(f"wrote {destination} "
          f"({len(catalog['platforms'])} platforms, "
          f"{len(catalog['tags'])} suffix defaults, "
          f"{len(catalog['tag_candidates'])} candidate sets, "
          f"{sum(1 for p in catalog['platforms'] if p.get('libretro_dir'))} "
          "cheat bindings)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

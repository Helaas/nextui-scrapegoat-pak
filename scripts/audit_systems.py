#!/usr/bin/env python3
"""Inventory and coverage audit for NextUI ROM-folder suffixes.

Discovers every emulator suffix that a NextUI install can produce -- from the
bundled skeleton and from the Pak Store -- verifies what each suffix actually
means, and compares that against the shipped ScrapeGoat catalog.

Two modes:

  full (default)    Requires resources/systems.json. Reports provider coverage.
  --inventory-only  Never reads the catalog. Certifies discovery only; its
                    output is labelled "coverage not evaluated" and can never
                    stand in for the committed coverage report.

Standard library only. Network access is required for the storefront and for
release resolution; responses are cached under .cache/audit_systems so repeat
runs stay cheap and reviewable.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CATALOG_PATH = REPO_ROOT / "resources" / "systems.json"
COVERAGE_REPORT = REPO_ROOT / "SYSTEMS.md"
INVENTORY_REPORT = REPO_ROOT / "SYSTEMS-INVENTORY.md"
CACHE_DIR = REPO_ROOT / ".cache" / "audit_systems"

STOREFRONT_URL = (
    "https://raw.githubusercontent.com/LoveRetro/nextui-pak-store/"
    "refs/heads/gh-pages/storefront.json"
)
GITHUB_API = "https://api.github.com"
USER_AGENT = "scrapegoat-audit-systems/1"
HTTP_TIMEOUT = 30

# NextUI skeleton layouts. EXTRAS has no paks/ directory, so one glob cannot
# cover both.
NEXTUI_LAYOUTS = (
    ("base", "skeleton/SYSTEM/*/paks/Emus/*.pak", 2),
    ("extras", "skeleton/EXTRAS/Emus/*/*.pak", 3),
)

# ── Reviewed manual evidence ──────────────────────────────────
#
# Some releases cannot be resolved from committed layout, packaging rules or
# the store's install rule alone. Each entry below records one human review
# bound to a single storefront version and source commit; a version or commit
# mismatch forces re-review instead of silently passing.

PAK_EVIDENCE: dict[str, dict] = {
    "yK3mJ7zP1n": {  # INTV
        "storefront_version": "v0.0.0",
        "source_commit": "7535b60599b3",
        "tags": ["INTV"],
        "devices": ["tg5040"],
        "reasoning": (
            "The v0.0.0 release attaches INTV.pak.zip and the storefront names "
            "the pak INTV, so the Pak Store installs Emus/<device>/INTV.pak. "
            "The pak.json committed at the tag carries the display string "
            "'Intellivision (INTV)', which was corrected to 'INTV' on the "
            "default branch the storefront is built from; it is a stale display "
            "name, not a second installed directory. "
            "https://github.com/K2Retro2nd/minui-intv-pak/releases/tag/v0.0.0"),
    },
    "zK8wB1jL4g": {  # ScummVM
        "storefront_version": "0.3.0",
        "source_commit": "f60d7bad2ce1",
        "tags": ["SCUMMVM", "ScummVM"],
        "devices": ["tg5040"],
        "reasoning": (
            "Two installed directory names are reachable for the same release. "
            "The Pak Store installs Emus/<device>/<storefront name>.pak, and the "
            "storefront name is 'ScummVM'. The release asset is SCUMMVM.pak.zip "
            "and the pak's own README documents the ROM folder as "
            "'/Roms/ScummVM (SCUMMVM)/', which only resolves against a "
            "SCUMMVM.pak directory -- what a manual install of that asset "
            "produces. Both suffixes therefore occur in the wild and both are "
            "recorded. NextUI matches a ROM folder suffix against the emulator "
            "directory name, so the two are distinct suffixes, not spellings. "
            "https://github.com/laesetuc/minui-scummvm/blob/f60d7bad2ce1/README.md"),
    },
}

# ── Reviewed suffix meaning ───────────────────────────────────
#
# A suffix identifies an emulator association, not necessarily one scraping
# platform. "scope" is singular, multi or unknown. Nothing here is inferred
# from the suffix spelling: NextUI suffixes are read from the core their
# launch.sh runs, store suffixes from the release-specific pak description.

_NEXTUI = "NextUI skeleton pak launch.sh"
_STORE = "Pak Store entry description at the audited storefront snapshot"


def _nextui(core: str, *systems: str, scope: str = "singular",
            note: str = "") -> dict:
    return {
        "scope": scope,
        "systems": list(systems),
        "evidence": f"{_NEXTUI} (EMU_EXE={core})",
        "reasoning": note or (
            f"NextUI ships one suffix per system; this pak runs the {core} "
            f"core for {', '.join(systems)}."),
    }


def _store(*systems: str, scope: str = "singular", note: str = "") -> dict:
    return {
        "scope": scope,
        "systems": list(systems),
        "evidence": _STORE,
        "reasoning": note,
    }


TARGET_MEANING: dict[str, dict] = {
    # NextUI base systems
    "FC": _nextui("fceumm", "Nintendo Entertainment System / Famicom"),
    "GB": _nextui("gambatte", "Game Boy"),
    "GBA": _nextui("gpsp", "Game Boy Advance"),
    "GBC": _nextui("gambatte", "Game Boy Color"),
    "MD": _nextui("picodrive", "Mega Drive / Genesis"),
    "PS": _nextui("pcsx_rearmed", "PlayStation"),
    "SFC": _nextui("snes9x", "Super Famicom / SNES"),

    # NextUI EXTRAS
    "32X": _nextui("picodrive", "Sega 32X"),
    "A2600": _nextui("stella2014", "Atari 2600"),
    "A5200": _nextui("a5200", "Atari 5200"),
    "A7800": _nextui("prosystem", "Atari 7800"),
    "C128": _nextui("vice_x128", "Commodore 128"),
    "C64": _nextui("vice_x64", "Commodore 64"),
    "COLECO": _nextui("gearcoleco", "ColecoVision"),
    "CPC": _nextui("cap32", "Amstrad CPC"),
    "FBN": _nextui(
        "fbneo", "Arcade (FinalBurn Neo)",
        note="The FinalBurn Neo core covers many arcade boards, but the "
             "providers treat FBNeo arcade games as one platform."),
    "FDS": _nextui("fceumm", "Famicom Disk System"),
    "GG": _nextui("picodrive", "Game Gear"),
    "LYNX": _nextui("handy", "Atari Lynx"),
    "MGBA": _nextui("mgba", "Game Boy Advance"),
    "MSX": _nextui(
        "bluemsx", "MSX",
        note="blueMSX also runs ColecoVision and SG-1000 media, but NextUI "
             "ships separate COLECO and SG1000 suffixes for those, so this "
             "suffix means the MSX family."),
    "NGP": _nextui("race", "Neo Geo Pocket"),
    "NGPC": _nextui("race", "Neo Geo Pocket Color"),
    "P8": _nextui("fake08", "PICO-8"),
    "PCE": _nextui(
        "mednafen_pce_fast", "PC Engine / TurboGrafx-16",
        note="The core also runs CD-ROM^2 titles; the providers file those "
             "under the same PC Engine platform."),
    "PET": _nextui("vice_xpet", "Commodore PET"),
    "PKM": _nextui("pokemini", "Pokémon Mini"),
    "PLUS4": _nextui("vice_xplus4", "Commodore Plus/4"),
    "PRBOOM": _nextui(
        "prboom", "Doom engine games (id Software IWAD/PWAD)",
        note="PrBoom plays Doom-engine WADs, not a hardware platform. The "
             "scraping target is the Doom game set, not a generic PC ID; the "
             "provider decision is a separate review."),
    "PUAE": _nextui("puae2021", "Commodore Amiga"),
    "SEGACD": _nextui("picodrive", "Mega-CD / Sega CD"),
    "SG1000": _nextui("picodrive", "SG-1000"),
    "SGB": _nextui(
        "mgba", "Game Boy (Super Game Boy)",
        note="Super Game Boy plays Game Boy cartridges with SGB enhancements; "
             "the providers catalogue the games as Game Boy."),
    "SMS": _nextui("picodrive", "Master System"),
    "SUPA": _nextui("mednafen_supafaust", "Super Famicom / SNES"),
    "VB": _nextui("mednafen_vb", "Virtual Boy"),
    "VIC": _nextui("vice_xvic", "VIC-20"),

    # Pak Store emulators
    "3DO": _store("3DO Interactive Multiplayer",
                  note="Opera core; '3DO addon for minarch, utilizing the "
                       "Opera core'."),
    "A800": _store(
        "Atari 8-bit computers (400/800/XL/XE)", "Atari 5200",
        scope="multi",
        note="The pak states the core 'emulates Atari 8-bit computers (400, "
             "800, XL, XE) and the 5200 console'. One suffix therefore spans "
             "two scraping platforms and needs folder selection, even though "
             "the pre-catalog table mapped A800 to Atari 800 alone."),
    "DC": _store("Dreamcast", note="Flycast standalone."),
    "DICE": _store(
        "Arcade (discrete logic, pre-CPU)",
        note="DICE emulates discrete integrated-circuit arcade hardware, not a "
             "CPU-based board. Whether either provider catalogues these games "
             "is a separate decision; do not assign a generic Arcade ID."),
    "EASYRPG": _store("RPG Maker 2000/2003 games"),
    "GPGX": _store(
        "Mega Drive / Genesis", "Master System", "Game Gear", "SG-1000",
        "Mega-CD / Sega CD", scope="multi",
        note="The pak describes the Genesis Plus GX core for 'Sega Genesis/"
             "Mega Drive, Master System, Game Gear, SG-1000, and Sega/Mega CD', "
             "and its v1.0.0 README documents one folder per system sharing the "
             "GPGX suffix. A single bundled default would misclassify four of "
             "the five folders. "
             "https://github.com/Helaas/nextui-gppx-pak/blob/v1.0.0/README.md#roms"),
    "GW": _store(
        "Game & Watch (MADrigal simulators)",
        note="Not cartridge dumps: the gw core runs MADrigal handheld "
             "simulations, so provider catalogues must be checked before any "
             "availability claim."),
    "INTV": _store(
        "Intellivision",
        note="FreeINTV core; 'Play classic Intellivision games with the "
             "FreeINTV core'."),
    "J2ME": _store(
        "Java ME (J2ME) mobile games",
        note="Provider coverage for J2ME must be verified independently for "
             "each provider."),
    "JAGUAR": _store("Atari Jaguar", note="Virtual Jaguar core."),
    "MKXPZ": _store(
        "RPG Maker XP/VX/VX Ace games",
        note="mkxp-z is a runtime for RPG Maker XP/VX/VX Ace projects rather "
             "than a console; relevant targets and provider limits are a "
             "separate review."),
    "N64": _store("Nintendo 64", note="mupen64plus standalone."),
    "NDS": _store("Nintendo DS", note="DraStic standalone."),
    "NEOCD": _store("Neo Geo CD", note="NeoCD libretro core."),
    "O2": _store("Magnavox Odyssey² / Philips Videopac",
                 note="O2EM core; the pak names both regional brands."),
    "PICO": _store("PICO-8", note="Alias of the NextUI P8 suffix, from a "
                                  "separate Pak Store entry."),
    "PORTS": _store(
        scope="open-ended",
        note="PortMaster installs native game ports chosen by the user. The "
             "content is open-ended, so no platform can be assigned to the "
             "suffix. Open-ended content is not evidence that none of the "
             "games are scrapeable, and PORTS must not be hidden by default."),
    "PSP": _store("PlayStation Portable", note="PPSSPP standalone."),
    "SCUMMVM": _store(
        "ScummVM point-and-click adventures",
        note="Suffix produced by a manual install of the SCUMMVM.pak.zip asset, "
             "and the folder name the pak's README documents."),
    "ScummVM": _store(
        "ScummVM point-and-click adventures",
        note="Suffix produced by a Pak Store install, which names the directory "
             "from the storefront entry ('ScummVM'). Same games as SCUMMVM, "
             "different directory name."),
    "SGX": _store("PC Engine SuperGrafx",
                  note="Beetle PC Engine SuperGrafx core. The pre-catalog "
                       "SUPERGRAFX tag stays as an alias."),
    "SMSU": _store(
        "Mega Drive / Genesis (MSU-MD)",
        note="A Genesis Plus GX (PUNCHiUM) build with MD-MSU support. Despite "
             "the spelling this is not Master System. How the providers "
             "represent MSU-MD modified games still needs verification."),
    "SS": _store("Sega Saturn", note="Yaba Sanshiro standalone."),
    "SWAN": _store(
        "PlayStation",
        note="SwanStation is a PlayStation core: the pak describes 'A PSX "
             "addon for minarch, utilizing the Swanstation core'. Despite the "
             "spelling this is not WonderSwan."),
    "TIC": _store("TIC-80"),
    "WSC": _store(
        "WonderSwan", "WonderSwan Color", scope="multi",
        note="The pak describes the Beetle WonderSwan core as emulating "
             "'Bandai WonderSwan / WonderSwan Color', so one suffix spans both "
             "monochrome and colour libraries."),
    "ZQUEST": _store(
        "Zelda Classic quests (v2.10-compatible)",
        note="User-authored quests for the Zelda Classic engine. Provider "
             "limits are a separate review; absence of a catalogue entry must "
             "be recorded as a decision, not assumed."),
}


class AuditError(Exception):
    pass


# ── Problem tracking ──────────────────────────────────────────

INCOMPLETE = "incomplete"     # source acquisition failed -> exit 2
INVALID = "invalid"           # missing/invalid catalog -> exit 2
UNRESOLVED = "unresolved"     # identity/meaning/provider undecided -> exit 1
GAP = "gap"                   # decision missing where target is known -> exit 1
STALE = "stale"               # manual evidence no longer matches -> exit 2
NOTE = "note"                 # informational only


@dataclass
class Problems:
    items: list[tuple[str, str, str]] = field(default_factory=list)

    def add(self, kind: str, subject: str, detail: str) -> None:
        self.items.append((kind, subject, detail))

    def of(self, *kinds: str) -> list[tuple[str, str, str]]:
        return [p for p in self.items if p[0] in kinds]

    def count(self, *kinds: str) -> int:
        return len(self.of(*kinds))


# ── HTTP with an on-disk cache ────────────────────────────────


def github_token() -> str | None:
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        return token.strip() or None
    try:
        out = subprocess.run(
            ["gh", "auth", "token"],
            capture_output=True, text=True, timeout=10, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout.strip() or None if out.returncode == 0 else None


@dataclass
class Fetcher:
    cache_dir: Path
    refresh: bool = False
    token: str | None = None
    calls: int = 0
    cache_hits: int = 0
    rate_limited: bool = False

    def _cache_file(self, url: str) -> Path:
        return self.cache_dir / (hashlib.sha256(url.encode()).hexdigest() + ".json")

    def get(self, url: str, accept: str = "application/json") -> dict:
        """Return {ok, status, body, sha256, fetched_at, from_cache, error}."""
        cache_file = self._cache_file(url)
        if not self.refresh and cache_file.is_file():
            try:
                cached = json.loads(cache_file.read_text(encoding="utf-8"))
                self.cache_hits += 1
                cached["from_cache"] = True
                return cached
            except (OSError, ValueError):
                pass

        headers = {"User-Agent": USER_AGENT, "Accept": accept}
        if self.token and url.startswith(GITHUB_API):
            headers["Authorization"] = f"Bearer {self.token}"
        request = urllib.request.Request(url, headers=headers)
        self.calls += 1
        result: dict = {
            "url": url,
            "fetched_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "from_cache": False,
        }
        try:
            with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT) as response:
                raw = response.read()
                result.update(
                    ok=True, status=response.status,
                    body=raw.decode("utf-8", "replace"),
                    sha256=hashlib.sha256(raw).hexdigest(),
                )
        except urllib.error.HTTPError as exc:
            raw = exc.read() or b""
            if exc.code in (403, 429) and b"rate limit" in raw.lower():
                self.rate_limited = True
            result.update(
                ok=False, status=exc.code, body="", sha256="",
                error=f"HTTP {exc.code} {exc.reason}",
            )
        except (urllib.error.URLError, OSError, ValueError) as exc:
            # Never cache transport failures: they are not evidence.
            result.update(ok=False, status=0, body="", sha256="",
                          error=f"{type(exc).__name__}: {exc}")
            return result

        try:
            self.cache_dir.mkdir(parents=True, exist_ok=True)
            cache_file.write_text(json.dumps(result), encoding="utf-8")
        except OSError:
            pass
        return result

    def get_json(self, url: str) -> tuple[dict | list | None, dict]:
        meta = self.get(url)
        if not meta.get("ok"):
            return None, meta
        try:
            return json.loads(meta["body"]), meta
        except ValueError as exc:
            meta["ok"] = False
            meta["error"] = f"invalid JSON: {exc}"
            return None, meta


# ── NextUI skeleton scan ──────────────────────────────────────


@dataclass
class PakSighting:
    tag: str
    origin: str          # "nextui:base", "nextui:extras", "store:<id>"
    device: str
    path: str
    has_launch: bool
    core: str | None = None   # libretro core named by the pak's launch.sh


EMU_EXE_RE = re.compile(r"^[ \t]*EMU_EXE[ \t]*=[ \t]*([A-Za-z0-9_.-]+)", re.M)


def launch_core(launch: Path) -> str | None:
    """The libretro core a NextUI emulator pak launches, if it names one.

    This is what a suffix actually runs. It is evidence about the emulator
    association, not on its own proof of which scraping platform a folder holds.
    """
    try:
        found = EMU_EXE_RE.search(launch.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        return None
    return found.group(1) if found else None


def git_revision(repo: Path) -> dict:
    info: dict = {"path": str(repo)}
    def run(*args: str) -> str | None:
        try:
            out = subprocess.run(["git", "-C", str(repo), *args],
                                 capture_output=True, text=True,
                                 timeout=20, check=False)
        except (OSError, subprocess.SubprocessError):
            return None
        return out.stdout.strip() if out.returncode == 0 else None

    info["commit"] = run("rev-parse", "HEAD")
    status = run("status", "--porcelain")
    info["dirty"] = None if status is None else bool(status)
    info["describe"] = run("describe", "--tags", "--always", "--dirty")
    return info


def scan_nextui(repo: Path, problems: Problems) -> tuple[list[PakSighting], dict]:
    meta = {"repo": str(repo), "layouts": {}}
    sightings: list[PakSighting] = []

    if not repo.is_dir():
        problems.add(INCOMPLETE, "NextUI",
                     f"repository not found at {repo}; pass --nextui-repo")
        return sightings, meta
    meta["revision"] = git_revision(repo)
    if meta["revision"].get("commit") is None:
        problems.add(INCOMPLETE, "NextUI",
                     f"{repo} is not a readable git repository; "
                     "the scan cannot be bound to a revision")
    elif meta["revision"].get("dirty"):
        problems.add(NOTE, "NextUI",
                     "working tree is dirty; the scan does not describe a "
                     "committed revision")

    for name, pattern, device_index in NEXTUI_LAYOUTS:
        found = sorted(repo.glob(pattern))
        meta["layouts"][name] = {"pattern": pattern, "paks": len(found)}
        if not found:
            problems.add(INCOMPLETE, "NextUI",
                         f"layout '{name}' ({pattern}) matched no paks; "
                         "the skeleton layout may have changed")
            continue
        for pak in found:
            if not pak.is_dir():
                problems.add(INCOMPLETE, "NextUI",
                             f"{pak.relative_to(repo)} is not a directory")
                continue
            tag = pak.name[: -len(".pak")]
            parts = pak.relative_to(repo).parts
            device = parts[device_index] if len(parts) > device_index else "?"
            launch = pak / "launch.sh"
            has_launch = launch.is_file()
            core = launch_core(launch) if has_launch else None
            if not has_launch:
                problems.add(NOTE, f"NextUI {tag}",
                             f"{pak.relative_to(repo)} has no launch.sh; "
                             "reported as an incomplete pak layout, not a "
                             "working emulator")
            sightings.append(PakSighting(
                tag=tag, origin=f"nextui:{name}", device=device,
                path=str(pak.relative_to(repo)), has_launch=has_launch,
                core=core,
            ))
    return sightings, meta


# ── Pak Store acquisition ─────────────────────────────────────


@dataclass
class StoreEntry:
    store_id: str
    name: str
    version: str
    release_filename: str | None
    devices: list[str]
    disabled: bool
    experimental: bool
    repo_url: str | None
    # resolution
    tag_name: str | None = None
    commit: str | None = None
    tags: list[str] = field(default_factory=list)
    tag_devices: dict[str, list[str]] = field(default_factory=dict)
    evidence: str = "unresolved"
    evidence_detail: str = ""
    truncated: bool = False
    release_assets: list[str] = field(default_factory=list)
    asset_candidate: str | None = None
    pak_json_name: str | None = None
    deferred: list[str] = field(default_factory=list)

    @property
    def is_pakz(self) -> bool:
        return bool(self.release_filename
                    and self.release_filename.lower().endswith(".pakz"))


def fetch_storefront(fetcher: Fetcher, problems: Problems) -> tuple[list[StoreEntry], dict]:
    data, meta = fetcher.get_json(STOREFRONT_URL)
    source = {
        "url": STOREFRONT_URL,
        "fetched_at": meta.get("fetched_at"),
        "sha256": meta.get("sha256"),
        "from_cache": meta.get("from_cache"),
    }
    if data is None:
        problems.add(INCOMPLETE, "Pak Store",
                     f"could not read the storefront: {meta.get('error')}")
        return [], source
    if not isinstance(data, dict):
        problems.add(INCOMPLETE, "Pak Store", "storefront is not a JSON object")
        return [], source

    entries: list[StoreEntry] = []
    counts = {}
    for array in ("paks", "experimental_paks"):
        items = data.get(array)
        if not isinstance(items, list):
            problems.add(INCOMPLETE, "Pak Store",
                         f"storefront has no '{array}' array")
            continue
        counts[array] = len(items)
        for item in items:
            if not isinstance(item, dict) or item.get("type") != "EMU":
                continue
            entries.append(StoreEntry(
                store_id=str(item.get("id") or "?"),
                name=str(item.get("name") or "?"),
                version=str(item.get("version") or ""),
                release_filename=item.get("release_filename"),
                devices=[str(p) for p in (item.get("platforms") or [])],
                disabled=bool(item.get("disabled")),
                experimental=array == "experimental_paks" or bool(item.get("experimental")),
                repo_url=item.get("repo_url"),
            ))
    source["counts"] = counts
    source["emu_entries"] = len(entries)
    entries.sort(key=lambda e: (e.name.upper(), e.store_id))
    return entries, source


GITHUB_REPO_RE = re.compile(r"^https?://github\.com/([^/]+)/([^/#?]+?)(?:\.git)?/?$")
RAW_GITHUB = "https://raw.githubusercontent.com"

# Device directory names seen in NextUI pak layouts. Used only to label a
# suffix sighting, never to decide what a suffix means.
KNOWN_DEVICES = {
    "desktop", "h700", "my355", "rg35xxplus", "tg3040", "tg5040", "tg5050",
}

# Paths that never install an emulator suffix.
EXCLUDED_SEGMENTS = {"Tools", "tools", "test", "tests", "build", "dist"}

# Packaging rules read from a pak's own Makefile at the resolved commit.
PAK_NAME_RE = re.compile(r"^[ \t]*PAK_NAME[ \t]*[:?+]?=[ \t]*(.+?)[ \t]*$", re.M)
JQ_PAK_JSON_NAME_RE = re.compile(r"jq\s+-r\s+\.name\s+pak\.json")
PAK_DIR_DEVICE_RE = re.compile(r"Emus/([A-Za-z0-9_]+)")

# How the Pak Store installs a non-.pakz emulator release. Read from the
# store's own installer, not inferred from asset names: UnzipPakArchive()
# extracts into filepath.Join(GetEmulatorRoot(), pak.Name+".pak"), and
# GetEmulatorRoot() is Emus/<device>. pak.Name is the storefront `name` field.
STORE_INSTALL_RULE = {
    "repo": "LoveRetro/nextui-pak-store",
    "file": "utils/functions.go",
    "reviewed_commit": "d234b8730616b61e47783207ffd42707a160fac6",
    "citation": (
        "LoveRetro/nextui-pak-store utils/functions.go@d234b873 "
        "UnzipPakArchive"
    ),
}


def resolve_release(entry: StoreEntry, fetcher: Fetcher, problems: Problems) -> None:
    """Establish which ROM-folder suffixes an advertised release installs."""
    subject = f"{entry.name} ({entry.store_id})"
    entry.asset_candidate = candidate_tag(entry)

    if not entry.repo_url:
        problems.add(UNRESOLVED, subject, "storefront entry has no repo_url")
        entry.evidence_detail = "no repository URL in the storefront"
    else:
        match = GITHUB_REPO_RE.match(entry.repo_url.strip())
        if not match:
            problems.add(UNRESOLVED, subject,
                         f"repo_url {entry.repo_url} is not a GitHub repository; "
                         "resolve the advertised release by hand")
            entry.evidence_detail = "non-GitHub repository"
        else:
            _resolve_github(entry, match.group(1), match.group(2), fetcher, problems)

    evidence = PAK_EVIDENCE.get(entry.store_id)
    if evidence:
        _apply_manual_evidence(entry, evidence, problems)

    if entry.evidence != "unresolved":
        entry.deferred.clear()
    for detail in entry.deferred:
        problems.add(UNRESOLVED, subject, detail)

    if entry.evidence == "unresolved":
        hint = (f"; the release asset suggests '{entry.asset_candidate}' and the "
                f"storefront names the pak '{entry.name}', both candidates rather "
                "than proof" if entry.asset_candidate else "")
        problems.add(UNRESOLVED, subject,
                     f"installed suffixes are unconfirmed{hint}. Add a reviewed "
                     "PAK_EVIDENCE entry bound to this storefront version and "
                     "source commit")


def candidate_tag(entry: StoreEntry) -> str | None:
    """A candidate suffix from the release asset name. Never proof on its own."""
    name = entry.release_filename
    if not name:
        return None
    for suffix in (".pak.zip", ".pakz", ".pak", ".zip"):
        if name.endswith(suffix):
            name = name[: -len(suffix)]
            break
    name = name.replace(".", " ").strip()
    return name or None


def _resolve_github(entry: StoreEntry, owner: str, repo: str,
                    fetcher: Fetcher, problems: Problems) -> None:
    subject = f"{entry.name} ({entry.store_id})"
    version = entry.version.strip()
    if not version:
        problems.add(UNRESOLVED, subject, "storefront advertises no version")
        entry.evidence_detail = "no advertised version"
        return

    candidates = [version, version[1:] if version.startswith("v") else "v" + version]

    release = None
    for tag in candidates:
        data, meta = fetcher.get_json(
            f"{GITHUB_API}/repos/{owner}/{repo}/releases/tags/"
            f"{urllib.parse.quote(tag, safe='')}")
        if isinstance(data, dict):
            release = data
            entry.tag_name = data.get("tag_name") or tag
            break
        if meta.get("status") in (0, 401, 403, 429) or fetcher.rate_limited:
            problems.add(INCOMPLETE, subject,
                         f"could not reach GitHub for {owner}/{repo}: "
                         f"{meta.get('error')}")
            entry.evidence_detail = "release lookup failed"
            return
    if release is None:
        problems.add(UNRESOLVED, subject,
                     f"no release tagged {' or '.join(candidates)} in "
                     f"{owner}/{repo}; the advertised version needs manual "
                     "confirmation. Do not substitute the default branch or "
                     "the newest release")
        entry.evidence_detail = "advertised version has no matching release"
        return

    assets = [a.get("name") for a in (release.get("assets") or [])
              if isinstance(a, dict)]
    entry.release_assets = [str(a) for a in assets if a]
    if entry.release_filename and entry.release_filename not in entry.release_assets:
        problems.add(UNRESOLVED, subject,
                     f"the {entry.tag_name} release does not carry the advertised "
                     f"asset {entry.release_filename!r}; it has "
                     f"{entry.release_assets or 'no assets'}")

    commit, meta = fetcher.get_json(
        f"{GITHUB_API}/repos/{owner}/{repo}/commits/"
        f"{urllib.parse.quote(entry.tag_name or '', safe='')}")
    if not isinstance(commit, dict) or not commit.get("sha"):
        problems.add(INCOMPLETE, subject,
                     f"could not resolve {entry.tag_name} to a commit in "
                     f"{owner}/{repo}: {meta.get('error')}")
        entry.evidence_detail = "tag does not resolve to a commit"
        return
    entry.commit = commit["sha"]

    tree, meta = fetcher.get_json(
        f"{GITHUB_API}/repos/{owner}/{repo}/git/trees/{entry.commit}?recursive=1")
    if not isinstance(tree, dict):
        problems.add(INCOMPLETE, subject,
                     f"could not read the source tree at {entry.commit[:12]}: "
                     f"{meta.get('error')}")
        entry.evidence_detail = "source tree unreadable"
        return
    entry.truncated = bool(tree.get("truncated"))
    if entry.truncated:
        problems.add(INCOMPLETE, subject,
                     f"the tree listing for {owner}/{repo}@{entry.commit[:12]} "
                     "is truncated; installed suffixes cannot be enumerated "
                     "from it")
        entry.evidence_detail = "truncated source tree"
        return

    paths = [str(node.get("path") or "") for node in (tree.get("tree") or [])]
    slug = f"{owner}/{repo}@{entry.commit[:12]} ({entry.tag_name})"
    entry.pak_json_name = _read_pak_json_name(entry, owner, repo, paths, fetcher)

    # Tier A: pak directories committed at the resolved commit.
    committed = pak_dirs_in_tree(paths)
    if committed:
        entry.tags = sorted(committed)
        entry.tag_devices = {t: sorted(d) for t, d in sorted(committed.items())}
        entry.evidence = "source-tree"
        entry.evidence_detail = (
            f"{slug} commits {len(entry.tags)} emulator pak director"
            f"{'y' if len(entry.tags) == 1 else 'ies'}")
        return

    if entry.is_pakz:
        # Tier B: a .pakz extracts at the SD root, so only the archive layout
        # decides. With no committed layout, the packaging rule is the evidence.
        _resolve_from_packaging_rule(entry, owner, repo, paths, slug,
                                     fetcher, problems)
        return

    # Tier C: a plain .pak.zip is installed by the Pak Store into
    # Emus/<device>/<storefront name>.pak, so the storefront name is the
    # installed directory name.
    _resolve_from_store_install_rule(entry, slug, problems)


def pak_dirs_in_tree(paths: list[str]) -> dict[str, set[str]]:
    """Suffix -> devices for every committed .pak directory that is an emulator.

    A .pak directory containing another .pak directory is a container, not a
    suffix: some paks name their device directories `<device>.pak`.
    """
    pak_paths = {p for p in paths if p.endswith(".pak")}
    containers = {p for p in pak_paths
                  if any(other != p and other.startswith(p + "/")
                         for other in pak_paths)}

    found: dict[str, set[str]] = {}
    for path in sorted(pak_paths - containers):
        parts = path.split("/")
        if EXCLUDED_SEGMENTS.intersection(parts[:-1]):
            continue
        if "Emus" not in parts[:-1]:
            continue
        tag = parts[-1][: -len(".pak")]
        devices = {part[:-4] if part.endswith(".pak") else part
                   for part in parts[:-1]}
        devices &= KNOWN_DEVICES
        found.setdefault(tag, set()).update(devices or {"(any)"})
    return found


def _read_pak_json_name(entry: StoreEntry, owner: str, repo: str,
                        paths: list[str], fetcher: Fetcher) -> str | None:
    if "pak.json" not in paths or not entry.commit:
        return None
    data, _ = fetcher.get_json(
        f"{RAW_GITHUB}/{owner}/{repo}/{entry.commit}/pak.json")
    if not isinstance(data, dict):
        return None
    name = data.get("name")
    return name.strip() if isinstance(name, str) and name.strip() else None


def _resolve_from_packaging_rule(entry: StoreEntry, owner: str, repo: str,
                                 paths: list[str], slug: str,
                                 fetcher: Fetcher, problems: Problems) -> None:
    subject = f"{entry.name} ({entry.store_id})"
    if "Makefile" not in paths:
        entry.evidence_detail = (
            f"{slug} commits neither a pak directory nor a Makefile; the "
            f"{entry.release_filename} layout needs manual evidence")
        return

    body = fetcher.get(f"{RAW_GITHUB}/{owner}/{repo}/{entry.commit}/Makefile",
                       accept="text/plain")
    if not body.get("ok"):
        problems.add(INCOMPLETE, subject,
                     f"could not read the Makefile at {slug}: {body.get('error')}")
        entry.evidence_detail = "packaging rule unreadable"
        return

    makefile = body["body"]
    name_match = PAK_NAME_RE.search(makefile)
    if not name_match:
        entry.evidence_detail = (
            f"{slug} has no PAK_NAME packaging rule; the "
            f"{entry.release_filename} layout needs manual evidence")
        return

    value = name_match.group(1).strip()
    if JQ_PAK_JSON_NAME_RE.search(value):
        if not entry.pak_json_name:
            entry.evidence_detail = (
                f"{slug} derives PAK_NAME from pak.json, which could not be read")
            return
        tag = entry.pak_json_name
        derivation = f"PAK_NAME is read from pak.json (name '{tag}')"
    elif "$(" in value or "`" in value:
        entry.evidence_detail = (
            f"{slug} computes PAK_NAME as {value!r}; resolve it by hand")
        return
    else:
        tag = value.strip('"')
        derivation = f"PAK_NAME := {tag}"

    devices = sorted(set(PAK_DIR_DEVICE_RE.findall(makefile)) & KNOWN_DEVICES)
    entry.tags = [tag]
    entry.tag_devices = {tag: devices or sorted(entry.devices)}
    entry.evidence = "packaging-rule"
    entry.evidence_detail = (
        f"{slug} builds {entry.release_filename} with {derivation}, so the "
        f"archive installs `{tag}.pak`. This is release-specific packaging "
        "evidence read from the build rules, not a committed pak directory, "
        "and the archive itself was not downloaded")


def _resolve_from_store_install_rule(entry: StoreEntry, slug: str,
                                     problems: Problems) -> None:
    tag = entry.name.strip()
    if not tag:
        entry.evidence_detail = "the storefront entry has no name"
        return

    conflicts = []
    if entry.asset_candidate and entry.asset_candidate != tag:
        conflicts.append(
            f"the release asset {entry.release_filename!r} implies "
            f"'{entry.asset_candidate}'")
    if entry.pak_json_name and entry.pak_json_name != tag:
        conflicts.append(
            f"pak.json at the resolved commit names the pak "
            f"'{entry.pak_json_name}'")
    if conflicts:
        entry.deferred.append(
            f"the Pak Store installs `{tag}.pak`, but "
            + "; and ".join(conflicts)
            + ". A ROM folder suffix must match the installed directory "
              "exactly, so confirm the intended suffix against the pak's own "
              "documentation before mapping it")
        entry.evidence_detail = (
            f"install rule gives '{tag}', conflicting candidates "
            + ", ".join(sorted({c for c in (entry.asset_candidate,
                                            entry.pak_json_name) if c and c != tag})))
        return

    entry.tags = [tag]
    entry.tag_devices = {tag: sorted(entry.devices)}
    entry.evidence = "store-install-rule"
    entry.evidence_detail = (
        f"the Pak Store extracts a non-.pakz EMU release into "
        f"`Emus/<device>/<storefront name>.pak`, so {entry.release_filename} "
        f"installs `{tag}.pak` ({STORE_INSTALL_RULE['citation']}); corroborated "
        f"by {slug}")


def _apply_manual_evidence(entry: StoreEntry, evidence: dict,
                           problems: Problems) -> None:
    subject = f"{entry.name} ({entry.store_id})"
    reviewed_version = str(evidence.get("storefront_version") or "")
    reviewed_commit = str(evidence.get("source_commit") or "")

    if reviewed_version != entry.version:
        problems.add(STALE, subject,
                     f"reviewed evidence covers storefront version "
                     f"{reviewed_version or '(none)'}, the storefront now "
                     f"advertises {entry.version}; re-review before relying on it")
        return
    if entry.commit and reviewed_commit and not entry.commit.startswith(reviewed_commit):
        problems.add(STALE, subject,
                     f"reviewed evidence covers commit {reviewed_commit}, the "
                     f"advertised release resolves to {entry.commit[:12]}")
        return

    reviewed_tags = [str(t) for t in (evidence.get("tags") or [])]
    if not reviewed_tags:
        problems.add(UNRESOLVED, subject,
                     "manual evidence records no installed suffixes")
        return
    if entry.tags and sorted(entry.tags) != sorted(reviewed_tags):
        problems.add(STALE, subject,
                     f"automatic evidence shows {sorted(entry.tags)} but the "
                     f"reviewed entry records {sorted(reviewed_tags)}; reconcile "
                     "them before trusting either")
        return

    entry.tags = sorted(reviewed_tags)
    entry.tag_devices = {
        tag: [str(d) for d in (evidence.get("devices") or entry.devices)]
        for tag in entry.tags
    }
    entry.evidence = "reviewed"
    entry.evidence_detail = str(evidence.get("reasoning") or "manually reviewed")


# ── Catalog ───────────────────────────────────────────────────


@dataclass
class Catalog:
    platforms: dict[str, dict]
    tags: dict[str, str]
    tag_candidates: dict[str, list[str]]
    path: Path


def load_catalog(problems: Problems) -> Catalog | None:
    if not CATALOG_PATH.is_file():
        problems.add(INVALID, "catalog",
                     f"{CATALOG_PATH.relative_to(REPO_ROOT)} does not exist. "
                     "A missing catalog makes coverage unevaluated, not empty. "
                     "Use --inventory-only for discovery before it is seeded")
        return None
    try:
        data = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        problems.add(INVALID, "catalog", f"{CATALOG_PATH.name} is unreadable: {exc}")
        return None
    if not isinstance(data, dict) or data.get("schema") != 1:
        problems.add(INVALID, "catalog", "unsupported or missing schema")
        return None

    platforms: dict[str, dict] = {}
    for platform in data.get("platforms") or []:
        if not isinstance(platform, dict):
            problems.add(INVALID, "catalog", "platform entry is not an object")
            return None
        pid = platform.get("id")
        if not isinstance(pid, str) or not pid or pid in platforms:
            problems.add(INVALID, "catalog", f"invalid or duplicate platform id {pid!r}")
            return None
        platforms[pid] = platform

    tags = {k: v for k, v in (data.get("tags") or {}).items()}
    candidates = {k: list(v) for k, v in (data.get("tag_candidates") or {}).items()}
    for tag, pid in tags.items():
        if pid not in platforms:
            problems.add(INVALID, "catalog",
                         f"tag default {tag} -> {pid} references an unknown platform")
            return None
    for tag, ids in candidates.items():
        for pid in ids:
            if pid not in platforms:
                problems.add(INVALID, "catalog",
                             f"tag candidate {tag} -> {pid} references an "
                             "unknown platform")
                return None
    return Catalog(platforms, tags, candidates, CATALOG_PATH)


# ── Coverage rows ─────────────────────────────────────────────


PROVIDER_SUPPORTED = "supported"
PROVIDER_UNAVAILABLE = "verified unavailable"
PROVIDER_UNVERIFIED = "not verified"


@dataclass
class Row:
    tag: str
    sources: list[str]
    devices: list[str]
    cores: list[str]
    evidence: str
    meaning: str
    default_platform: str
    candidates: list[str]
    screenscraper: str
    cheats: str
    status: str


def provider_state(platform: dict | None, key: str) -> str:
    if platform is None:
        return PROVIDER_UNVERIFIED
    if platform.get(key) not in (None, "", -1):
        return PROVIDER_SUPPORTED
    verified = platform.get("verified_unavailable") or []
    return PROVIDER_UNAVAILABLE if key in verified else PROVIDER_UNVERIFIED


def build_rows(sightings: list[PakSighting], entries: list[StoreEntry],
               catalog: Catalog | None, problems: Problems) -> list[Row]:
    by_tag: dict[str, dict] = {}

    def slot(tag: str) -> dict:
        return by_tag.setdefault(tag, {"sources": set(), "devices": set(),
                                       "evidence": set(), "cores": set()})

    for sighting in sightings:
        item = slot(sighting.tag)
        item["sources"].add(sighting.origin)
        item["devices"].add(sighting.device)
        item["evidence"].add("nextui skeleton")
        if sighting.core:
            item["cores"].add(sighting.core)

    for entry in entries:
        label = f"store:{entry.name}@{entry.version}"
        if entry.disabled:
            label += " (disabled)"
        if entry.experimental:
            label += " (experimental)"
        if not entry.tags:
            continue
        for tag in entry.tags:
            item = slot(tag)
            item["sources"].add(label)
            item["devices"].update(entry.tag_devices.get(tag) or entry.devices)
            item["evidence"].add(entry.evidence)

    rows: list[Row] = []
    for tag in sorted(by_tag):
        item = by_tag[tag]
        meaning = TARGET_MEANING.get(tag)
        if meaning:
            scope = meaning.get("scope", "unknown")
            systems = ", ".join(meaning.get("systems") or []) or "-"
            meaning_text = f"{scope}: {systems}" if systems != "-" else scope
            if scope == "unknown":
                problems.add(UNRESOLVED, tag,
                             "the systems this suffix targets are not resolved")
            elif scope == "open-ended":
                problems.add(NOTE, tag,
                             "reviewed as open-ended content with no single "
                             "platform: a documented limit, not an unresolved "
                             "decision. "
                             + str(meaning.get("reasoning") or ""))
        else:
            meaning_text = "not verified"
            problems.add(UNRESOLVED, tag,
                         "no reviewed record of what systems this suffix targets")

        default_id = catalog.tags.get(tag) if catalog else None
        candidate_ids = catalog.tag_candidates.get(tag, []) if catalog else []
        platform = catalog.platforms.get(default_id) if (catalog and default_id) else None

        if catalog:
            if not default_id and not candidate_ids:
                problems.add(GAP, tag,
                             "the catalog offers neither a default platform nor "
                             "reviewed candidates")
            screenscraper = provider_state(platform, "ss_id")
            cheats = provider_state(platform, "libretro_dir")
            if platform is None and candidate_ids:
                screenscraper = cheats = PROVIDER_UNVERIFIED
        else:
            screenscraper = cheats = "coverage not evaluated"

        if not catalog:
            status = "inventory only"
        elif default_id:
            status = "default mapping"
        elif candidate_ids:
            status = "folder selection required"
        else:
            status = "unmapped"

        rows.append(Row(
            tag=tag,
            sources=sorted(item["sources"]),
            devices=sorted(d for d in item["devices"] if d),
            cores=sorted(item["cores"]),
            evidence=", ".join(sorted(item["evidence"])),
            meaning=meaning_text,
            default_platform=default_id or "-",
            candidates=candidate_ids,
            screenscraper=screenscraper,
            cheats=cheats,
            status=status,
        ))

    if catalog:
        observed = set(by_tag)
        for tag in sorted(set(catalog.tags) | set(catalog.tag_candidates)):
            if tag not in observed:
                problems.add(NOTE, tag,
                             "catalog alias with no currently observed pak; "
                             "retained, informational only")
    return rows


# ── Report ────────────────────────────────────────────────────


def escape(text: str) -> str:
    return text.replace("|", "\\|")


def render_report(rows: list[Row], problems: Problems, nextui_meta: dict,
                  store_meta: dict, sightings: list[PakSighting],
                  entries: list[StoreEntry], catalog: Catalog | None,
                  inventory_only: bool, exit_code: int) -> str:
    lines: list[str] = []
    title = "System Suffix Inventory" if inventory_only else "System Suffix Coverage"
    lines.append(f"# {title}")
    lines.append("")

    if exit_code == 2:
        banner = "INCOMPLETE"
    elif exit_code == 1:
        banner = "GAPS REMAIN"
    else:
        banner = "COMPLETE"
    lines.append(f"**Status: {banner}**")
    if inventory_only:
        lines.append("")
        lines.append(
            "This run used `--inventory-only`: **coverage not evaluated**. It "
            "certifies discovery only and cannot replace the committed coverage "
            "report.")
    lines.append("")

    # Sources
    lines.append("## Sources")
    lines.append("")
    revision = nextui_meta.get("revision") or {}
    lines.append(f"- NextUI repository: `{nextui_meta.get('repo')}`")
    lines.append(f"  - commit: `{revision.get('commit') or 'unresolved'}`"
                 f" ({revision.get('describe') or 'no description'})")
    lines.append(f"  - working tree dirty: {revision.get('dirty')}")
    for name, info in (nextui_meta.get("layouts") or {}).items():
        lines.append(f"  - layout `{name}`: `{info['pattern']}` -> {info['paks']} pak(s)")
    lines.append(f"- Pak Store: `{store_meta.get('url')}`")
    lines.append(f"  - fetched: {store_meta.get('fetched_at')}"
                 f"{' (cached)' if store_meta.get('from_cache') else ''}")
    lines.append(f"  - sha256: `{store_meta.get('sha256') or 'n/a'}`")
    counts = store_meta.get("counts") or {}
    lines.append(f"  - entries: "
                 + ", ".join(f"{k}={v}" for k, v in sorted(counts.items()))
                 + f", EMU={store_meta.get('emu_entries', 0)}")
    if catalog:
        lines.append(f"- Catalog: `{catalog.path.relative_to(REPO_ROOT)}` "
                     f"({len(catalog.platforms)} platforms, {len(catalog.tags)} "
                     f"tag defaults, {len(catalog.tag_candidates)} candidate sets)")
    else:
        lines.append("- Catalog: not read"
                     + ("" if inventory_only else " (missing or invalid)"))
    lines.append("")

    # Suffix table
    lines.append("## Suffixes")
    lines.append("")
    lines.append("| Suffix | Source | Devices | Core | Evidence | Target meaning | "
                 "Default | Candidates | ScreenScraper | Cheats | Status |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for row in rows:
        lines.append("| " + " | ".join(escape(cell) for cell in [
            f"`{row.tag}`",
            "<br>".join(row.sources) or "-",
            ", ".join(row.devices) or "-",
            ", ".join(f"`{c}`" for c in row.cores) or "-",
            row.evidence or "-",
            row.meaning,
            row.default_platform,
            ", ".join(row.candidates) or "-",
            row.screenscraper,
            row.cheats,
            row.status,
        ]) + " |")
    lines.append("")

    # Store releases
    lines.append("## Pak Store emulator releases")
    lines.append("")
    lines.append("| Pak | Store ID | Version | Release asset | Resolved commit | "
                 "Installed suffixes | Evidence |")
    lines.append("|---|---|---|---|---|---|---|")
    for entry in sorted(entries, key=lambda e: e.name.upper()):
        flags = []
        if entry.disabled:
            flags.append("disabled")
        if entry.experimental:
            flags.append("experimental")
        name = entry.name + (f" ({', '.join(flags)})" if flags else "")
        lines.append("| " + " | ".join(escape(cell) for cell in [
            name,
            f"`{entry.store_id}`",
            entry.version or "-",
            f"`{entry.release_filename}`" if entry.release_filename else "-",
            f"`{entry.commit[:12]}`" if entry.commit else "unresolved",
            ", ".join(f"`{t}`" for t in entry.tags) or "unresolved",
            f"{entry.evidence}: {entry.evidence_detail}" if entry.evidence_detail
            else entry.evidence,
        ]) + " |")
    lines.append("")

    # Reviewed target meaning
    lines.append("## Reviewed target meaning")
    lines.append("")
    lines.append("What each suffix targets, and the evidence it rests on. "
                 "Nothing here is inferred from a suffix's spelling.")
    lines.append("")
    for row in rows:
        meaning = TARGET_MEANING.get(row.tag)
        if not meaning:
            lines.append(f"- **`{row.tag}`** — not verified.")
            continue
        systems = ", ".join(meaning.get("systems") or []) or "no single platform"
        lines.append(f"- **`{row.tag}`** — {meaning.get('scope')}: {systems}")
        lines.append(f"  - evidence: {meaning.get('evidence')}")
        reasoning = str(meaning.get("reasoning") or "").strip()
        if reasoning:
            lines.append(f"  - {reasoning}")
    lines.append("")
    if PAK_EVIDENCE:
        lines.append("### Reviewed packaging evidence")
        lines.append("")
        for store_id, evidence in sorted(PAK_EVIDENCE.items()):
            entry = next((e for e in entries if e.store_id == store_id), None)
            label = entry.name if entry else store_id
            lines.append(f"- **{label}** (`{store_id}`), storefront version "
                         f"`{evidence.get('storefront_version')}`, source commit "
                         f"`{evidence.get('source_commit')}` — installs "
                         + ", ".join(f"`{t}`" for t in evidence.get("tags") or [])
                         + f". {evidence.get('reasoning')}")
        lines.append("")

    # Findings
    sections = [
        ("Incomplete source acquisition", (INCOMPLETE,)),
        ("Invalid catalog", (INVALID,)),
        ("Stale manual evidence", (STALE,)),
        ("Unresolved identity, target meaning or provider decisions", (UNRESOLVED,)),
        ("Missing default or candidate decisions", (GAP,)),
        ("Informational", (NOTE,)),
    ]
    lines.append("## Findings")
    lines.append("")
    for heading, kinds in sections:
        found = problems.of(*kinds)
        if not found:
            continue
        lines.append(f"### {heading} ({len(found)})")
        lines.append("")
        for _, subject, detail in sorted(found, key=lambda p: (p[1], p[2])):
            lines.append(f"- **{subject}** — {detail}")
        lines.append("")

    # Summary
    lines.append("## Summary")
    lines.append("")
    nextui_tags = {sighting.tag for sighting in sightings}
    store_tags = {tag for entry in entries for tag in entry.tags}
    lines.append(f"- Distinct suffixes discovered: {len(rows)}")
    lines.append(f"  - from the NextUI skeleton: {len(nextui_tags)}")
    lines.append(f"  - from Pak Store emulator releases: {len(store_tags)} "
                 f"({len(store_tags - nextui_tags)} not in the skeleton)")
    lines.append(f"- Store releases with confirmed installed suffixes: "
                 f"{sum(1 for e in entries if e.tags)} of {len(entries)}")
    lines.append(f"- Suffixes needing folder selection (multi-system): "
                 f"{sum(1 for r in rows if r.meaning.startswith('multi'))}")
    lines.append(f"- Suffixes reviewed as open-ended content: "
                 f"{sum(1 for r in rows if r.meaning.startswith('open-ended'))}")
    lines.append(f"- Unambiguous defaults: "
                 f"{sum(1 for r in rows if r.status == 'default mapping')}")
    lines.append(f"- Requiring folder selection: "
                 f"{sum(1 for r in rows if r.status == 'folder selection required')}")
    lines.append(f"- ScreenScraper supported: "
                 f"{sum(1 for r in rows if r.screenscraper == PROVIDER_SUPPORTED)}")
    lines.append(f"- Cheat databases supported: "
                 f"{sum(1 for r in rows if r.cheats == PROVIDER_SUPPORTED)}")
    lines.append(f"- Incomplete sources: {problems.count(INCOMPLETE, INVALID, STALE)}")
    lines.append(f"- Unresolved decisions: {problems.count(UNRESOLVED)}")
    lines.append(f"- Missing mapping decisions: {problems.count(GAP)}")
    lines.append("")
    lines.append("Recognising a suffix name is not coverage, and user overrides "
                 "on a device cannot close a catalog gap.")
    lines.append("")
    lines.append(f"Generated by `scripts/audit_systems.py` at "
                 f"{time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}; "
                 f"exit code {exit_code}.")
    lines.append("")
    return "\n".join(lines)


# ── Entry point ───────────────────────────────────────────────


def compute_exit(problems: Problems) -> int:
    if problems.count(INCOMPLETE, INVALID, STALE):
        return 2
    if problems.count(UNRESOLVED, GAP):
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--nextui-repo", default=os.environ.get("NEXTUI_REPO", "../NextUI"),
                        help="path to a NextUI checkout (default: ../NextUI)")
    parser.add_argument("--inventory-only", action="store_true",
                        help="discovery only; never reads the catalog")
    parser.add_argument("--write-report", action="store_true",
                        help="write the Markdown report")
    parser.add_argument("--report-path", default=None,
                        help="report destination (default: SYSTEMS.md, or "
                             "SYSTEMS-INVENTORY.md with --inventory-only)")
    parser.add_argument("--cache-dir", default=str(CACHE_DIR),
                        help="HTTP cache directory")
    parser.add_argument("--refresh", action="store_true",
                        help="ignore cached HTTP responses")
    parser.add_argument("--offline", action="store_true",
                        help="use only cached HTTP responses; uncached lookups "
                             "are reported as incomplete")
    args = parser.parse_args(argv)

    report_path = Path(args.report_path) if args.report_path else (
        INVENTORY_REPORT if args.inventory_only else COVERAGE_REPORT)
    if args.inventory_only and report_path.resolve() == COVERAGE_REPORT.resolve():
        parser.error("--inventory-only cannot write the coverage report "
                     f"({COVERAGE_REPORT.name}); it certifies discovery only")

    problems = Problems()
    fetcher = Fetcher(cache_dir=Path(args.cache_dir), refresh=args.refresh,
                      token=github_token())
    if args.offline:
        fetcher.get = _offline_get(fetcher)  # type: ignore[method-assign]

    nextui_repo = Path(args.nextui_repo).expanduser()
    if not nextui_repo.is_absolute():
        nextui_repo = (Path.cwd() / nextui_repo).resolve()
    sightings, nextui_meta = scan_nextui(nextui_repo, problems)

    entries, store_meta = fetch_storefront(fetcher, problems)
    for entry in entries:
        resolve_release(entry, fetcher, problems)

    catalog = None if args.inventory_only else load_catalog(problems)
    rows = build_rows(sightings, entries, catalog, problems)

    exit_code = compute_exit(problems)
    report = render_report(rows, problems, nextui_meta, store_meta, sightings,
                           entries, catalog, args.inventory_only, exit_code)

    if args.write_report:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(report, encoding="utf-8")
        print(f"wrote {report_path}")
    else:
        sys.stdout.write(report)

    print(f"\nsuffixes={len(rows)} incomplete={problems.count(INCOMPLETE, INVALID, STALE)} "
          f"unresolved={problems.count(UNRESOLVED)} gaps={problems.count(GAP)} "
          f"http_calls={fetcher.calls} cache_hits={fetcher.cache_hits} "
          f"exit={exit_code}", file=sys.stderr)
    return exit_code


def _offline_get(fetcher: Fetcher):
    original = fetcher.get

    def get(url: str, accept: str = "application/json") -> dict:
        cache_file = fetcher._cache_file(url)
        if cache_file.is_file():
            return original(url, accept)
        return {"url": url, "ok": False, "status": 0, "body": "", "sha256": "",
                "error": "offline: no cached response", "from_cache": False,
                "fetched_at": None}

    return get


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Checks for scripts/audit_systems.py and scripts/gen_systems_catalog.py.

Everything here runs offline against fixtures: no provider is contacted and no
credentials are needed.

    python3 -m unittest discover -s tests -p 'test_*.py'
"""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))


import audit_systems as audit
import gen_systems_catalog as gen
import setup_mock_sdcard as mock_sd


def make_pak(root: Path, relative: str, launch: str | None = "EMU_EXE=testcore\n"):
    pak = root / relative
    pak.mkdir(parents=True, exist_ok=True)
    if launch is not None:
        (pak / "launch.sh").write_text(launch, encoding="utf-8")
    return pak


class NextUIScanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def scan(self):
        problems = audit.Problems()
        sightings, meta = audit.scan_nextui(self.root, problems)
        return sightings, meta, problems

    def test_covers_both_layouts(self):
        make_pak(self.root, "skeleton/SYSTEM/tg5040/paks/Emus/GB.pak",
                 "EMU_EXE=gambatte\n")
        make_pak(self.root, "skeleton/SYSTEM/tg5050/paks/Emus/GB.pak",
                 "EMU_EXE=gambatte\n")
        make_pak(self.root, "skeleton/EXTRAS/Emus/tg5040/GG.pak",
                 "EMU_EXE=picodrive\n")

        sightings, meta, problems = self.scan()
        tags = sorted({s.tag for s in sightings})
        self.assertEqual(tags, ["GB", "GG"])
        self.assertEqual(meta["layouts"]["base"]["paks"], 2)
        self.assertEqual(meta["layouts"]["extras"]["paks"], 1)
        # The fixture is not a git checkout, so the only expected complaint is
        # that the scan cannot be bound to a revision.
        layout_problems = [detail for _, _, detail in problems.of(audit.INCOMPLETE)
                           if "layout" in detail]
        self.assertEqual(layout_problems, [])

    def test_device_repetition_is_not_a_second_suffix(self):
        make_pak(self.root, "skeleton/SYSTEM/tg5040/paks/Emus/GB.pak")
        make_pak(self.root, "skeleton/SYSTEM/tg5050/paks/Emus/GB.pak")
        make_pak(self.root, "skeleton/EXTRAS/Emus/tg5040/GG.pak")

        sightings, _, _ = self.scan()
        devices = sorted({s.device for s in sightings if s.tag == "GB"})
        self.assertEqual(devices, ["tg5040", "tg5050"])
        self.assertEqual(len([s for s in sightings if s.tag == "GB"]), 2)

    def test_missing_layout_is_incomplete(self):
        make_pak(self.root, "skeleton/SYSTEM/tg5040/paks/Emus/GB.pak")
        _, _, problems = self.scan()
        self.assertTrue(any("extras" in detail
                            for _, _, detail in problems.of(audit.INCOMPLETE)))

    def test_missing_repository_is_incomplete(self):
        problems = audit.Problems()
        audit.scan_nextui(self.root / "absent", problems)
        self.assertEqual(problems.count(audit.INCOMPLETE), 1)

    def test_pak_without_launch_is_reported(self):
        make_pak(self.root, "skeleton/SYSTEM/tg5040/paks/Emus/GB.pak", launch=None)
        make_pak(self.root, "skeleton/EXTRAS/Emus/tg5040/GG.pak")
        sightings, _, problems = self.scan()
        broken = [s for s in sightings if s.tag == "GB"][0]
        self.assertFalse(broken.has_launch)
        self.assertTrue(any("launch.sh" in detail
                            for _, _, detail in problems.of(audit.INCOMPLETE)))

    def test_core_is_read_from_launch_script(self):
        make_pak(self.root, "skeleton/SYSTEM/tg5040/paks/Emus/MD.pak",
                 "#!/bin/sh\nEMU_EXE=picodrive\nCORES_PATH=.\n")
        make_pak(self.root, "skeleton/EXTRAS/Emus/tg5040/GG.pak")
        sightings, _, _ = self.scan()
        core = [s.core for s in sightings if s.tag == "MD"][0]
        self.assertEqual(core, "picodrive")


class PakTreeTests(unittest.TestCase):
    def test_device_directories_collapse_to_one_suffix(self):
        found = audit.pak_dirs_in_tree([
            "skeleton/Emus/tg5040/O2.pak",
            "skeleton/Emus/tg5040/O2.pak/launch.sh",
            "skeleton/Emus/my355/O2.pak",
        ])
        self.assertEqual(sorted(found), ["O2"])
        self.assertEqual(found["O2"], {"tg5040", "my355"})

    def test_container_pak_directory_is_not_a_suffix(self):
        found = audit.pak_dirs_in_tree([
            "skeleton/Emus/h700.pak",
            "skeleton/Emus/h700.pak/O2.pak",
            "skeleton/Emus/h700.pak/O2.pak/launch.sh",
        ])
        self.assertEqual(sorted(found), ["O2"])
        self.assertEqual(found["O2"], {"h700"})

    def test_multi_suffix_bundle(self):
        found = audit.pak_dirs_in_tree([
            "skeleton/Emus/tg5040/AAA.pak",
            "skeleton/Emus/tg5040/BBB.pak",
        ])
        self.assertEqual(sorted(found), ["AAA", "BBB"])

    def test_tool_paks_are_excluded(self):
        found = audit.pak_dirs_in_tree([
            "skeleton/Tools/tg5040/Helper.pak",
            "skeleton/Emus/tg5040/AAA.pak",
        ])
        self.assertEqual(sorted(found), ["AAA"])

    def test_pak_outside_emus_is_not_a_suffix(self):
        self.assertEqual(audit.pak_dirs_in_tree(["dist/Something.pak"]), {})


class ManualEvidenceTests(unittest.TestCase):
    def entry(self, **overrides):
        base = dict(store_id="abc", name="Thing", version="v1.0.0",
                    release_filename="THING.pak.zip", devices=["tg5040"],
                    disabled=False, experimental=False,
                    repo_url="https://github.com/o/r")
        base.update(overrides)
        entry = audit.StoreEntry(**base)
        entry.commit = "abc123" + "0" * 34
        return entry

    def test_version_bump_invalidates_evidence(self):
        entry = self.entry(version="v1.1.0")
        problems = audit.Problems()
        audit._apply_manual_evidence(entry, {
            "storefront_version": "v1.0.0", "source_commit": "abc123",
            "tags": ["THING"], "reasoning": "reviewed",
        }, problems)
        self.assertEqual(problems.count(audit.STALE), 1)
        self.assertEqual(entry.evidence, "unresolved")

    def test_commit_mismatch_invalidates_evidence(self):
        entry = self.entry()
        entry.commit = "deadbeefcafe"
        problems = audit.Problems()
        audit._apply_manual_evidence(entry, {
            "storefront_version": "v1.0.0", "source_commit": "abc123",
            "tags": ["THING"],
        }, problems)
        self.assertEqual(problems.count(audit.STALE), 1)

    def test_contradicting_automatic_evidence_is_stale(self):
        entry = self.entry()
        entry.tags = ["OTHER"]
        entry.evidence = "store-install-rule"
        problems = audit.Problems()
        audit._apply_manual_evidence(entry, {
            "storefront_version": "v1.0.0", "source_commit": "abc123",
            "tags": ["THING"],
        }, problems)
        self.assertEqual(problems.count(audit.STALE), 1)
        self.assertEqual(entry.tags, ["OTHER"])

    def test_matching_evidence_resolves_the_entry(self):
        entry = self.entry()
        problems = audit.Problems()
        audit._apply_manual_evidence(entry, {
            "storefront_version": "v1.0.0", "source_commit": "abc123",
            "tags": ["THING"], "reasoning": "reviewed by hand",
        }, problems)
        self.assertEqual(entry.evidence, "reviewed")
        self.assertEqual(entry.tags, ["THING"])
        self.assertEqual(problems.count(audit.STALE, audit.UNRESOLVED), 0)

    def test_partial_source_candidates_can_be_completed_by_bound_review(self):
        entry = self.entry()
        entry.tags = ["THING"]
        problems = audit.Problems()
        audit._apply_manual_evidence(entry, {
            "storefront_version": "v1.0.0", "source_commit": "abc123",
            "tags": ["THING", "EXTRA"], "reasoning": "release adds EXTRA at build time",
        }, problems)
        self.assertEqual(entry.tags, ["EXTRA", "THING"])
        self.assertEqual(entry.evidence, "reviewed")

    def test_unbound_manual_evidence_cannot_pass(self):
        entry = self.entry()
        problems = audit.Problems()
        audit._apply_manual_evidence(entry, {
            "storefront_version": "v1.0.0", "tags": ["THING"],
        }, problems)
        self.assertEqual(entry.evidence, "unresolved")
        self.assertEqual(audit.compute_exit(problems), 1)

    def test_reviewed_but_broken_store_installation_still_fails(self):
        entry = self.entry()
        problems = audit.Problems()
        audit._apply_manual_evidence(entry, {
            "storefront_version": "v1.0.0", "source_commit": "abc123",
            "tags": ["THING"], "reasoning": "inspected released archive",
            "installation_issue": "SD-root layout is advertised as a flat pak zip",
        }, problems)
        self.assertEqual(entry.evidence, "reviewed")
        self.assertEqual(problems.count(audit.GAP), 1)
        self.assertEqual(audit.compute_exit(problems), 1)


class ReleasePackagingTests(unittest.TestCase):
    def test_partial_tree_never_confirms_the_published_bundle(self):
        class FakeFetcher:
            rate_limited = False

            def get_json(self, url):
                if "/releases/tags/" in url:
                    return {"tag_name": "v1", "assets": [{"name": "GPGX.pakz"}]}, {"ok": True}
                if "/commits/" in url:
                    return {"sha": "a" * 40}, {"ok": True}
                if "/git/trees/" in url:
                    return {"truncated": False, "tree": [
                        {"path": "skeleton/Emus/tg5040/GPGX.pak"},
                        {"path": "Makefile"},
                    ]}, {"ok": True}
                raise AssertionError(url)

        entry = audit.StoreEntry("fixture", "GPGX", "v1", "GPGX.pakz",
                                 ["tg5040"], False, False, "https://github.com/o/r")
        problems = audit.Problems()
        audit.resolve_release(entry, FakeFetcher(), problems)
        self.assertEqual(entry.tags, ["GPGX"])
        self.assertEqual(entry.evidence, "unresolved")
        self.assertEqual(audit.compute_exit(problems), 1)

    def test_name_variable_alone_does_not_confirm_generated_layout(self):
        class FakeFetcher:
            def get(self, *args, **kwargs):
                return {"ok": True, "body": "PAK_NAME := GPGX\n"}

        entry = audit.StoreEntry("fixture", "GPGX", "v1", "GPGX.pakz",
                                 ["tg5040"], False, False, "https://github.com/o/r")
        entry.commit = "a" * 40
        audit._resolve_from_packaging_rule(entry, "o", "r", ["Makefile"],
                                           "o/r@v1", FakeFetcher(), audit.Problems())
        self.assertEqual(entry.tags, ["GPGX"])
        self.assertEqual(entry.evidence, "unresolved")


class FetcherTests(unittest.TestCase):
    def test_offline_uncached_lookup_is_not_ok(self):
        with tempfile.TemporaryDirectory() as tmp:
            fetcher = audit.Fetcher(cache_dir=Path(tmp))
            offline = audit._offline_get(fetcher)
            result = offline("https://example.invalid/thing.json")
            self.assertFalse(result["ok"])
            self.assertIn("offline", result["error"])

    def test_rate_limited_response_is_flagged(self):
        with tempfile.TemporaryDirectory() as tmp:
            fetcher = audit.Fetcher(cache_dir=Path(tmp))
            self.assertFalse(fetcher.rate_limited)


class TruncatedTreeTests(unittest.TestCase):
    def test_truncated_tree_is_incomplete(self):
        entry = audit.StoreEntry(
            store_id="abc", name="Thing", version="v1.0.0",
            release_filename="THING.pak.zip", devices=[], disabled=False,
            experimental=False, repo_url="https://github.com/o/r")
        problems = audit.Problems()

        class FakeFetcher:
            rate_limited = False

            def get_json(self, url):
                if "releases/tags" in url:
                    return {"tag_name": "v1.0.0",
                            "assets": [{"name": "THING.pak.zip"}]}, {"ok": True}
                if "/commits/" in url:
                    return {"sha": "a" * 40}, {"ok": True}
                if "/git/trees/" in url:
                    return {"truncated": True, "tree": []}, {"ok": True}
                return None, {"ok": False, "error": "unexpected"}

        audit._resolve_github(entry, "o", "r", FakeFetcher(), problems)
        self.assertTrue(entry.truncated)
        self.assertEqual(entry.tags, [])
        self.assertTrue(any("truncated" in detail
                            for _, _, detail in problems.of(audit.INCOMPLETE)))


class CatalogTests(unittest.TestCase):
    def test_missing_catalog_is_invalid_not_empty(self):
        problems = audit.Problems()
        original = audit.CATALOG_PATH
        try:
            audit.CATALOG_PATH = REPO_ROOT / "resources" / "absent.json"
            self.assertIsNone(audit.load_catalog(problems))
        finally:
            audit.CATALOG_PATH = original
        self.assertEqual(problems.count(audit.INVALID), 1)
        self.assertEqual(audit.compute_exit(problems), 2)

    def test_dangling_reference_is_invalid(self):
        problems = audit.Problems()
        original = audit.CATALOG_PATH
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "systems.json"
            path.write_text(json.dumps({
                "schema": 1,
                "platforms": [{"id": "a", "name": "A"}],
                "tags": {"X": "missing"},
            }), encoding="utf-8")
            try:
                audit.CATALOG_PATH = path
                self.assertIsNone(audit.load_catalog(problems))
            finally:
                audit.CATALOG_PATH = original
        self.assertEqual(problems.count(audit.INVALID), 1)

    def test_shipped_catalog_loads(self):
        problems = audit.Problems()
        catalog = audit.load_catalog(problems)
        self.assertIsNotNone(catalog, [p[2] for p in problems.items])
        self.assertGreater(len(catalog.platforms), 100)
        self.assertIn("GPGX", catalog.tag_candidates)
        self.assertNotIn("GPGX", catalog.tags)

    def test_invalid_field_types_and_duplicates_fail_catalog_validation(self):
        base = {"schema": 1, "platforms": [{"id": "x", "name": "X"}], "tags": {"X": "x"}}
        cases = []
        for field, value in (("ss_id", 1e100), ("libretro_dir", ".."), ("aliases", [42])):
            value_case = copy.deepcopy(base)
            value_case["platforms"][0][field] = value
            cases.append(json.dumps(value_case))
        cases.extend([
            json.dumps({**base, "schema": 1.5}), json.dumps({**base, "tags": []}),
            json.dumps(base) + " trailing garbage",
            '{"schema":1,"platforms":[{"id":"x","name":"X","ss_id":1,"ss_id":2}]}',
        ])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "catalog.json"
            for body in cases:
                with self.subTest(body=body):
                    path.write_text(body)
                    with self.assertRaises(gen.GeneratorError):
                        gen.read_catalog(path)


class ProviderCoverageTests(unittest.TestCase):
    def rows(self, tag, modify=None):
        problems = audit.Problems()
        catalog = audit.load_catalog(problems)
        if modify:
            modify(catalog)
        entry = audit.StoreEntry("fixture", tag, "v1", tag + ".pakz", [], False, False, "")
        entry.tags = [tag]
        return audit.build_rows([], [entry], catalog, problems), problems

    def test_unverified_default_provider_fails_the_gate(self):
        def unreviewed(catalog):
            catalog.platforms["commodore64"].pop("verified_unavailable", None)
        rows, problems = self.rows("C64", unreviewed)
        self.assertEqual(rows[0].cheats, audit.PROVIDER_UNVERIFIED)
        self.assertEqual(audit.compute_exit(problems), 1)

    def test_all_candidate_providers_are_evaluated(self):
        rows, problems = self.rows("GPGX")
        self.assertEqual(rows[0].screenscraper, audit.PROVIDER_SUPPORTED)
        self.assertEqual(rows[0].cheats, audit.PROVIDER_SUPPORTED)
        self.assertEqual(audit.compute_exit(problems), 0)

        def unreviewed(catalog):
            catalog.platforms["gamegear"].pop("libretro_dir")
        _, problems = self.rows("GPGX", unreviewed)
        self.assertEqual(audit.compute_exit(problems), 1)
        self.assertTrue(any("gamegear" in detail for _, _, detail in problems.of(audit.UNRESOLVED)))

    def test_verified_provider_limitation_is_not_a_gap(self):
        rows, problems = self.rows("GW")
        self.assertEqual(rows[0].cheats, audit.PROVIDER_UNAVAILABLE)
        self.assertEqual(audit.compute_exit(problems), 0)


class ExitCodeTests(unittest.TestCase):
    def test_clean_run_exits_zero(self):
        self.assertEqual(audit.compute_exit(audit.Problems()), 0)

    def test_gaps_exit_one(self):
        problems = audit.Problems()
        problems.add(audit.GAP, "X", "no decision")
        self.assertEqual(audit.compute_exit(problems), 1)

    def test_incomplete_sources_exit_two(self):
        problems = audit.Problems()
        problems.add(audit.GAP, "X", "no decision")
        problems.add(audit.INCOMPLETE, "Y", "fetch failed")
        self.assertEqual(audit.compute_exit(problems), 2)

    def test_stale_evidence_exits_two(self):
        problems = audit.Problems()
        problems.add(audit.STALE, "Y", "version bump")
        self.assertEqual(audit.compute_exit(problems), 2)


class ReportTests(unittest.TestCase):
    def render(self, exit_code, inventory_only=False, catalog=None):
        return audit.render_report(
            rows=[], problems=audit.Problems(), nextui_meta={"layouts": {}},
            store_meta={}, sightings=[], entries=[], catalog=catalog,
            inventory_only=inventory_only, exit_code=exit_code)

    def test_incomplete_run_is_labelled(self):
        self.assertIn("**Status: INCOMPLETE**", self.render(2))

    def test_gaps_are_labelled(self):
        self.assertIn("**Status: GAPS REMAIN**", self.render(1))

    def test_inventory_only_says_coverage_not_evaluated(self):
        report = self.render(0, inventory_only=True)
        self.assertIn("coverage not evaluated", report)
        self.assertIn("cannot replace the committed coverage report", report)

    def test_inventory_only_cannot_write_the_coverage_report(self):
        with self.assertRaises(SystemExit):
            audit.main(["--inventory-only", "--write-report",
                        "--report-path", str(audit.COVERAGE_REPORT)])


class CatalogGeneratorTests(unittest.TestCase):
    """Identity must survive regeneration, including an upstream rename."""

    def systems(self, megadrive_name="Megadrive"):
        return [
            {"id": 1, "noms": {"nom_eu": megadrive_name,
                               "noms_commun": "Sega Genesis,Genesis"}},
            {"id": 2, "noms": {"nom_eu": "Master System"}},
            {"id": 999, "noms": {"nom_eu": "Brand New Machine"}},
        ]

    def cht(self):
        return ["Sega - Mega Drive - Genesis", "Sega - Master System - Mark III"]

    def build(self, systems):
        original_identity = dict(gen.IDENTITY)
        original_bindings = dict(gen.LIBRETRO_BINDINGS)
        original_defaults = dict(gen.TAG_DEFAULTS)
        original_candidates = dict(gen.TAG_CANDIDATES)
        original_unavailable = dict(gen.VERIFIED_UNAVAILABLE)
        try:
            gen.IDENTITY = {1: ("megadrive", "Mega Drive / Genesis"),
                            2: ("mastersystem", "Master System")}
            gen.LIBRETRO_BINDINGS = {
                "megadrive": "Sega - Mega Drive - Genesis",
                "mastersystem": "Sega - Master System - Mark III",
            }
            gen.TAG_DEFAULTS = {"MD": "megadrive", "SMS": "mastersystem"}
            gen.TAG_CANDIDATES = {"GPGX": ["megadrive", "mastersystem"]}
            gen.VERIFIED_UNAVAILABLE = {}
            return gen.build_catalog(systems, self.cht(), "sha", "commit")
        finally:
            gen.IDENTITY = original_identity
            gen.LIBRETRO_BINDINGS = original_bindings
            gen.TAG_DEFAULTS = original_defaults
            gen.TAG_CANDIDATES = original_candidates
            gen.VERIFIED_UNAVAILABLE = original_unavailable

    def test_upstream_rename_keeps_the_local_id(self):
        first = self.build(self.systems())
        renamed = self.build(self.systems(megadrive_name="Sega Mega Drive II"))
        by_ss = {p["ss_id"]: p for p in renamed["platforms"]}
        self.assertEqual(by_ss[1]["id"], "megadrive")
        self.assertIn("Sega Mega Drive II", by_ss[1]["aliases"])
        self.assertEqual([p["id"] for p in first["platforms"]],
                         [p["id"] for p in renamed["platforms"]])

    def test_decisions_are_preserved(self):
        catalog = self.build(self.systems())
        self.assertEqual(catalog["tags"]["MD"], "megadrive")
        self.assertEqual(catalog["tag_candidates"]["GPGX"],
                         ["megadrive", "mastersystem"])
        self.assertNotIn("GPGX", catalog["tags"])

    def test_new_platform_gets_a_proposed_slug(self):
        catalog = self.build(self.systems())
        by_ss = {p["ss_id"]: p for p in catalog["platforms"]}
        self.assertEqual(by_ss[999]["id"], "brandnewmachine")

    def test_missing_reviewed_platform_is_reported_not_dropped(self):
        systems = [s for s in self.systems() if s["id"] != 2]
        with self.assertRaises(gen.GeneratorError):
            self.build(systems)

    def test_missing_cheat_directory_is_fatal(self):
        original = dict(gen.LIBRETRO_BINDINGS)
        try:
            with self.assertRaises(gen.GeneratorError):
                gen.build_catalog(self.systems(), ["Only - This One"],
                                  "sha", "commit")
        finally:
            gen.LIBRETRO_BINDINGS = original

    def test_shipped_catalog_matches_the_baseline_except_corrections(self):
        differences = gen.compare_with_baseline(
            json.loads((REPO_ROOT / "resources" / "systems.json")
                       .read_text(encoding="utf-8")))
        unreviewed = [d for d in differences if "UNREVIEWED" in d]
        self.assertEqual(unreviewed, [])
        self.assertEqual(len(differences), len(gen.CORRECTIONS))

    def test_every_published_identity_and_decision_survives_regeneration(self):
        existing = gen.read_catalog(REPO_ROOT / "resources" / "systems.json")
        existing["platforms"].append({"id": "cheat-only", "name": "Cheat only", "libretro_dir": "Fixture"})
        existing["platforms"][0]["aliases"] = ["Reviewed local synonym"]
        existing["tags"]["LOCAL"] = "cheat-only"
        existing["tag_candidates"]["LOCAL_MULTI"] = ["cheat-only", "megadrive"]
        upstream = [{"id": p["ss_id"], "noms": {"nom_eu": p["name"] + " Renamed"}}
                    for p in existing["platforms"] if p.get("ss_id")]
        dirs = [p["libretro_dir"] for p in existing["platforms"] if p.get("libretro_dir")]
        first = gen.build_catalog(upstream, dirs, "sha", "commit", existing)
        again = gen.build_catalog(upstream, dirs, "sha", "commit", first)
        self.assertEqual(first["platforms"], again["platforms"])
        self.assertEqual(first["tags"], existing["tags"])
        self.assertEqual(first["tag_candidates"], existing["tag_candidates"])
        by_id = {p["id"]: p for p in first["platforms"]}
        for original in existing["platforms"]:
            current = by_id[original["id"]]
            self.assertEqual(current.get("ss_id"), original.get("ss_id"))
            self.assertEqual(current.get("libretro_dir"), original.get("libretro_dir"))
            self.assertEqual(current.get("verified_unavailable"), original.get("verified_unavailable"))
            self.assertTrue(set(original.get("aliases", [])).issubset(current.get("aliases", [])))
        with self.assertRaises(gen.GeneratorError):
            gen.build_catalog(upstream[1:], dirs, "sha", "commit", existing)

    def test_new_slug_collision_requires_review(self):
        existing = {"platforms": [{"id": "machine", "name": "Original", "ss_id": 1}],
                    "tags": {}, "tag_candidates": {}}
        upstream = [{"id": 1, "noms": {"nom_eu": "Original"}},
                    {"id": 2, "noms": {"nom_eu": "Machine"}}]
        with self.assertRaises(gen.GeneratorError):
            gen.build_catalog(upstream, [], "sha", "commit", existing)


class MockSDTests(unittest.TestCase):
    def test_five_nonempty_distinct_examples_preserve_existing_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            mock_sd.setup(root)
            paths = list((root / "Roms").glob("* (GPGX)/*"))
            self.assertEqual(len(paths), 5)
            self.assertEqual(len({p.name for p in paths}), 5)
            self.assertTrue(all(p.stat().st_size for p in paths))
            paths[0].write_bytes(b"existing ROM data")
            mock_sd.setup(root)
            self.assertEqual(paths[0].read_bytes(), b"existing ROM data")


if __name__ == "__main__":
    unittest.main()

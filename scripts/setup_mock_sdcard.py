#!/usr/bin/env python3
"""Add non-playable ROM placeholders for testing the GPGX mapping picker."""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "mock_sdcard"
EXAMPLES = {
    "Mega Drive": "md",
    "Master System": "sms",
    "Game Gear": "gg",
    "SG-1000": "sg",
    "Mega CD": "chd",
}


def setup(root: Path = ROOT) -> None:
    for name, extension in EXAMPLES.items():
        folder = root / "Roms" / f"{name} (GPGX)"
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / f"GPGX {name} Example.{extension}"
        try:
            with path.open("xb") as output:
                output.write(b"ScrapeGoat UI test placeholder; not a playable ROM.\n")
        except FileExistsError:
            pass


if __name__ == "__main__":
    setup()
    print(f"GPGX mapping examples ready in {ROOT / 'Roms'}")

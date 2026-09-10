# System Suffix Coverage

**Status: GAPS REMAIN**

## Sources

- NextUI repository: `/Volumes/Storage/GitHub/NextUI`
  - commit: `f524d3b44d1de9625fd88f4f0c14b3e6b2f3dab5` (v6.13.2-2-gf524d3b4)
  - working tree dirty: False
  - layout `base`: `skeleton/SYSTEM/*/paks/Emus/*.pak` -> 21 pak(s)
  - layout `extras`: `skeleton/EXTRAS/Emus/*/*.pak` -> 60 pak(s)
- Pak Store: `https://raw.githubusercontent.com/LoveRetro/nextui-pak-store/refs/heads/gh-pages/storefront.json`
  - fetched: 2026-09-09T21:02:31Z (cached)
  - sha256: `5643806aa8ca7d8d3ee5c67613789b3816a4bcd1e841ca70b26b2802e368136b`
  - entries: experimental_paks=4, paks=117, EMU=26
- Catalog: `resources/systems.json` (250 platforms, 58 tag defaults, 5 candidate sets)

## Suffixes

| Suffix | Source | Devices | Core | Evidence | Target meaning | Default | Candidates | ScreenScraper | Cheats | Status |
|---|---|---|---|---|---|---|---|---|---|---|
| `32X` | nextui:extras | tg5040, tg5050 | `picodrive` | nextui skeleton | singular: Sega 32X | sega32x | - | supported | supported | default mapping |
| `3DO` | store:3DO@v1.1.0 | tg5040, tg5050 | - | store-install-rule | singular: 3DO Interactive Multiplayer | threedo | - | supported | verified unavailable | default mapping |
| `A2600` | nextui:extras | tg5040, tg5050 | `stella2014` | nextui skeleton | singular: Atari 2600 | atari2600 | - | supported | supported | default mapping |
| `A5200` | nextui:extras | tg5040, tg5050 | `a5200` | nextui skeleton | singular: Atari 5200 | atari5200 | - | supported | supported | default mapping |
| `A7800` | nextui:extras | tg5040, tg5050 | `prosystem` | nextui skeleton | singular: Atari 7800 | atari7800 | - | supported | supported | default mapping |
| `A800` | store:A800@v0.4.2 | tg5040 | - | store-install-rule | multi: Atari 8-bit computers (400/800/XL/XE), Atari 5200 | atari8bit | atari8bit, atari5200 | supported | supported | default mapping |
| `C128` | nextui:extras | tg5040, tg5050 | `vice_x128` | nextui skeleton | singular: Commodore 128 | - | commodore64 | supported | verified unavailable | folder selection required |
| `C64` | nextui:extras | tg5040, tg5050 | `vice_x64` | nextui skeleton | singular: Commodore 64 | commodore64 | - | supported | verified unavailable | default mapping |
| `COLECO` | nextui:extras | tg5040, tg5050 | `gearcoleco` | nextui skeleton | singular: ColecoVision | colecovision | - | supported | supported | default mapping |
| `CPC` | nextui:extras | tg5040, tg5050 | `cap32` | nextui skeleton | singular: Amstrad CPC | amstradcpc | - | supported | verified unavailable | default mapping |
| `DC` | store:DC@0.5.0 | tg5040 | - | store-install-rule | singular: Dreamcast | dreamcast | - | supported | supported | default mapping |
| `DICE` | store:DICE@v1.0.0 | h700, my355, tg5040 | - | store-install-rule | singular: Arcade (discrete logic, pre-CPU) | - | mame | supported | supported | folder selection required |
| `EASYRPG` | store:EASYRPG@v1.2.1 | tg5040 | - | store-install-rule | singular: RPG Maker 2000/2003 games | easyrpg | - | supported | verified unavailable | default mapping |
| `FBN` | nextui:extras | tg5040, tg5050 | `fbneo` | nextui skeleton | singular: Arcade (FinalBurn Neo) | mame | - | supported | supported | default mapping |
| `FC` | nextui:base | desktop, tg5040, tg5050 | `fceumm` | nextui skeleton | singular: Nintendo Entertainment System / Famicom | nes | - | supported | supported | default mapping |
| `FDS` | nextui:extras | tg5040, tg5050 | `fceumm` | nextui skeleton | singular: Famicom Disk System | famicomdisksystem | - | supported | supported | default mapping |
| `GB` | nextui:base | desktop, tg5040, tg5050 | `gambatte` | nextui skeleton | singular: Game Boy | gameboy | - | supported | supported | default mapping |
| `GBA` | nextui:base | desktop, tg5040, tg5050 | `gpsp` | nextui skeleton | singular: Game Boy Advance | gameboyadvance | - | supported | supported | default mapping |
| `GBC` | nextui:base | desktop, tg5040, tg5050 | `gambatte` | nextui skeleton | singular: Game Boy Color | gameboycolor | - | supported | supported | default mapping |
| `GG` | nextui:extras | tg5040, tg5050 | `picodrive` | nextui skeleton | singular: Game Gear | gamegear | - | supported | supported | default mapping |
| `GPGX` | store:GPGX@v1.0.0 | my355, tg5040, tg5050 | - | reviewed | multi: Mega Drive / Genesis, Master System, Game Gear, SG-1000, Mega-CD / Sega CD | - | megadrive, mastersystem, gamegear, sg1000, segacd | supported | supported | folder selection required |
| `GW` | store:GW@v1.0.0 | tg5040 | - | reviewed | singular: Game & Watch (MADrigal simulators) | gameandwatch | - | supported | verified unavailable | default mapping |
| `INTV` | store:INTV@v0.0.0 | tg5040 | - | reviewed | singular: Intellivision | intellivision | - | supported | supported | default mapping |
| `J2ME` | store:J2ME@v1.6.1 | h700, my355, tg5040, tg5050 | - | store-install-rule | singular: Java ME (J2ME) mobile games | j2me | - | supported | verified unavailable | default mapping |
| `JAGUAR` | store:JAGUAR@v1.1.0 | tg5040, tg5050 | - | store-install-rule | singular: Atari Jaguar | jaguar | - | supported | supported | default mapping |
| `LYNX` | nextui:extras | tg5040, tg5050 | `handy` | nextui skeleton | singular: Atari Lynx | lynx | - | supported | supported | default mapping |
| `MD` | nextui:base | desktop, tg5040, tg5050 | `picodrive` | nextui skeleton | singular: Mega Drive / Genesis | megadrive | - | supported | supported | default mapping |
| `MGBA` | nextui:extras | tg5040, tg5050 | `mgba` | nextui skeleton | singular: Game Boy Advance | gameboyadvance | - | supported | supported | default mapping |
| `MKXPZ` | store:MKXPZ@v0.6.7 (experimental) | tg5040, tg5050 | - | store-install-rule | singular: RPG Maker XP/VX/VX Ace games | - | - | not verified | not verified | no target (reviewed) |
| `MSX` | nextui:extras | tg5040, tg5050 | `bluemsx` | nextui skeleton | singular: MSX | msx | - | supported | supported | default mapping |
| `N64` | store:N64@0.6.3 | tg5040, tg5050 | - | store-install-rule | singular: Nintendo 64 | nintendo64 | - | supported | supported | default mapping |
| `NDS` | store:NDS@0.12.0 | tg5040, tg5050 | - | store-install-rule | singular: Nintendo DS | nintendods | - | supported | supported | default mapping |
| `NEOCD` | store:NEOCD@v1.0.0 | h700, my355, tg5040 | - | reviewed | singular: Neo Geo CD | neogeocd | - | supported | verified unavailable | default mapping |
| `NGP` | nextui:extras | tg5040, tg5050 | `race` | nextui skeleton | singular: Neo Geo Pocket | neogeopocket | - | supported | verified unavailable | default mapping |
| `NGPC` | nextui:extras | tg5040, tg5050 | `race` | nextui skeleton | singular: Neo Geo Pocket Color | neogeopocketcolor | - | supported | verified unavailable | default mapping |
| `O2` | store:O2@v1.0.0 | h700, my355, tg5040 | - | reviewed | singular: Magnavox Odyssey² / Philips Videopac | odyssey2 | - | supported | verified unavailable | default mapping |
| `P8` | nextui:extras | tg5040, tg5050 | `fake08` | nextui skeleton | singular: PICO-8 | pico8 | - | supported | verified unavailable | default mapping |
| `PCE` | nextui:extras | tg5040, tg5050 | `mednafen_pce_fast` | nextui skeleton | singular: PC Engine / TurboGrafx-16 | pcengine | - | supported | supported | default mapping |
| `PET` | nextui:extras | tg5040, tg5050 | `vice_xpet` | nextui skeleton | singular: Commodore PET | commodorepet | - | supported | verified unavailable | default mapping |
| `PICO` | store:PICO@0.7.0 | rg35xxplus, tg5040, tg5050 | - | store-install-rule | singular: PICO-8 | pico8 | - | supported | verified unavailable | default mapping |
| `PKM` | nextui:extras | tg5040, tg5050 | `pokemini` | nextui skeleton | singular: Pokémon Mini | pokemonmini | - | supported | verified unavailable | default mapping |
| `PLUS4` | nextui:extras | tg5040, tg5050 | `vice_xplus4` | nextui skeleton | singular: Commodore Plus/4 | commodoreplus4 | - | supported | verified unavailable | default mapping |
| `PORTS` | store:PORTS@2.14.0 | tg5040 | - | reviewed | open-ended | - | - | not verified | not verified | no target (reviewed) |
| `PRBOOM` | nextui:extras | tg5040, tg5050 | `prboom` | nextui skeleton | singular: Doom engine games (id Software IWAD/PWAD) | prboom | - | supported | supported | default mapping |
| `PS` | nextui:base | desktop, tg5040, tg5050 | `pcsx_rearmed` | nextui skeleton | singular: PlayStation | playstation | - | supported | supported | default mapping |
| `PSP` | store:PSP@6.2.0 | tg5040, tg5050 | - | store-install-rule | singular: PlayStation Portable | psp | - | supported | supported | default mapping |
| `PUAE` | nextui:extras | tg5040, tg5050 | `puae2021` | nextui skeleton | singular: Commodore Amiga | amiga | - | supported | verified unavailable | default mapping |
| `SCUMMVM` | store:ScummVM@0.3.0 | tg5040 | - | reviewed | singular: ScummVM point-and-click adventures | scummvm | - | supported | verified unavailable | default mapping |
| `SEGACD` | nextui:extras | tg5040, tg5050 | `picodrive` | nextui skeleton | singular: Mega-CD / Sega CD | segacd | - | supported | supported | default mapping |
| `SFC` | nextui:base | desktop, tg5040, tg5050 | `snes9x` | nextui skeleton | singular: Super Famicom / SNES | snes | - | supported | supported | default mapping |
| `SG1000` | nextui:extras | tg5040, tg5050 | `picodrive` | nextui skeleton | singular: SG-1000 | sg1000 | - | supported | supported | default mapping |
| `SGB` | nextui:extras | tg5040, tg5050 | `mgba` | nextui skeleton | singular: Game Boy (Super Game Boy) | gameboy | - | supported | supported | default mapping |
| `SGX` | store:SGX@v1.0.0 | h700, my355, tg5040 | - | reviewed | singular: PC Engine SuperGrafx | supergrafx | - | supported | supported | default mapping |
| `SMS` | nextui:extras | tg5040, tg5050 | `picodrive` | nextui skeleton | singular: Master System | mastersystem | - | supported | supported | default mapping |
| `SMSU` | store:SMSU@v1.1.1 | tg5040 | - | store-install-rule | singular: Mega Drive / Genesis (MSU-MD) | megadrive | - | supported | supported | default mapping |
| `SS` | store:SS@1.9.1 | tg5040, tg5050 | - | store-install-rule | singular: Sega Saturn | saturn | - | supported | supported | default mapping |
| `SUPA` | nextui:extras | tg5040, tg5050 | `mednafen_supafaust` | nextui skeleton | singular: Super Famicom / SNES | snes | - | supported | supported | default mapping |
| `SWAN` | store:SWAN@v1.0.0 | h700 | - | store-install-rule | singular: PlayStation | playstation | - | supported | supported | default mapping |
| `ScummVM` | store:ScummVM@0.3.0 | tg5040 | - | reviewed | singular: ScummVM point-and-click adventures | scummvm | - | supported | verified unavailable | default mapping |
| `TIC` | store:TIC@0.3.0 | tg5040 | - | store-install-rule | singular: TIC-80 | tic80 | - | supported | supported | default mapping |
| `VB` | nextui:extras | tg5040, tg5050 | `mednafen_vb` | nextui skeleton | singular: Virtual Boy | virtualboy | - | supported | verified unavailable | default mapping |
| `VIC` | nextui:extras | tg5040, tg5050 | `vice_xvic` | nextui skeleton | singular: VIC-20 | vic20 | - | supported | verified unavailable | default mapping |
| `WSC` | store:WSC@v1.0.0 | tg5040, tg5050 | - | store-install-rule | multi: WonderSwan, WonderSwan Color | - | wonderswan, wonderswancolor | supported | verified unavailable | folder selection required |
| `ZQUEST` | store:ZQUEST@v1.0.0 | tg5040 | - | reviewed | singular: Zelda Classic quests (v2.10-compatible) | - | - | not verified | not verified | no target (reviewed) |

## Pak Store emulator releases

| Pak | Store ID | Version | Release asset | Resolved commit | Installed suffixes | Evidence |
|---|---|---|---|---|---|---|
| 3DO | `hB5yN2xR6w` | v1.1.0 | `3DO.pak.zip` | `ec85f863090a` | `3DO` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so 3DO.pak.zip installs `3DO.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by dominickvaccaro/nextui-opera-3do@ec85f863090a (v1.1.0) |
| A800 | `QXheBZoEcC` | v0.4.2 | `A800.zip` | `84eddf59d27e` | `A800` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so A800.zip installs `A800.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by KrutzOtrem/nextui-a800@84eddf59d27e (v0.4.2) |
| DC | `pK2yD5qS7g` | 0.5.0 | `DC.pak.zip` | `8f034a526c4a` | `DC` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so DC.pak.zip installs `DC.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by josegonzalez/minui-dreamcast-pak@8f034a526c4a (0.5.0) |
| DICE | `72p7yQjQQu` | v1.0.0 | `DICE.pak.zip` | `02ae22beba10` | `DICE` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so DICE.pak.zip installs `DICE.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by tsolfan/dice-libretro-nextui@02ae22beba10 (v1.0.0) |
| EASYRPG | `MS6nE23a90` | v1.2.1 | `EASYRPG.zip` | `b50b4cac19df` | `EASYRPG` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so EASYRPG.zip installs `EASYRPG.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by KrutzOtrem/nextui-easyrpg@b50b4cac19df (v1.2.1) |
| GPGX | `9duidHBqAk` | v1.0.0 | `GPGX.pakz` | `b2ef7fdcb19e` | `GPGX` | reviewed: Reviewed the complete assemble_pak and package recipes at the release commit: launch.sh and the core enter GPGX.pak, then a clean staging directory receives only Emus/tg5040/GPGX.pak, Emus/tg5050/GPGX.pak and Emus/my355/GPGX.pak before zip creates GPGX.pakz. Device repetitions are one suffix. Source packaging evidence; the asset was not downloaded. https://github.com/Helaas/nextui-gppx-pak/blob/b2ef7fdcb19eb94e3ff3dc4a03d6cfc959aaf7a6/Makefile |
| GW | `2J5vkdSppU` | v1.0.0 | `GW.pakz` | `a32e8926009c` | `GW` | reviewed: Inspected the advertised release archive on 2026-09-10 (SHA-256 f162102b5347e6fec96260f1f8b5c9e7ccf63ede1dd7d923be0cddc72d554331). Every emulator launch.sh is under Emus/<device>/GW.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/pawndev/Game-Watch-NextUI/releases/download/v1.0.0/GW.pakz |
| INTV | `yK3mJ7zP1n` | v0.0.0 | `INTV.pak.zip` | `7535b60599b3` | `INTV` | reviewed: The v0.0.0 release attaches INTV.pak.zip and the storefront names the pak INTV, so the Pak Store installs Emus/<device>/INTV.pak. The pak.json committed at the tag carries the display string 'Intellivision (INTV)', which was corrected to 'INTV' on the default branch the storefront is built from; it is a stale display name, not a second installed directory. https://github.com/K2Retro2nd/minui-intv-pak/releases/tag/v0.0.0 |
| J2ME | `VDhSsgndqM` | v1.6.1 | `J2ME.pak.zip` | `53410a57d9c4` | `J2ME` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so J2ME.pak.zip installs `J2ME.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by reesque/J2ME-Pak@53410a57d9c4 (v1.6.1) |
| JAGUAR | `cM8pD1hK4q` | v1.1.0 | `JAGUAR.pak.zip` | `f196565165d8` | `JAGUAR` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so JAGUAR.pak.zip installs `JAGUAR.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by dominickvaccaro/nextui-virtual-jaguar@f196565165d8 (v1.1.0) |
| MKXPZ (experimental) | `77QFF0uYRu` | v0.6.7 | `MKXPZ.pak.zip` | `9255cd4dae6b` | `MKXPZ` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so MKXPZ.pak.zip installs `MKXPZ.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by KrutzOtrem/nextui-mkxpz@9255cd4dae6b (v0.6.7) |
| N64 | `jT8nH4zM1r` | 0.6.3 | `N64.pak.zip` | `ecbbdbac54f0` | `N64` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so N64.pak.zip installs `N64.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by josegonzalez/minui-n64-pak@ecbbdbac54f0 (0.6.3) |
| NDS | `fW6pX3kB9v` | 0.12.0 | `NDS.pak.zip` | `310a46c1a15e` | `NDS` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so NDS.pak.zip installs `NDS.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by josegonzalez/minui-nintendo-ds-pak@310a46c1a15e (0.12.0) |
| NEOCD | `23hbrUZWLY` | v1.0.0 | `NEOCD.pak.zip` | `00cfd674fd6b` | `NEOCD` | reviewed: Inspected the advertised release archive on 2026-09-10 (SHA-256 776b7814f7480e0f7951c9b3e13d96babed59921fcf37cd5ca6798c261b6ddc9). Every emulator launch.sh is under Emus/<device>/NEOCD.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/tsolfan/neocd_libretro-nextui/releases/download/v1.0.0/NEOCD.pak.zip |
| O2 | `FxBRHBO4mo` | v1.0.0 | `O2.pak.zip` | `c957aab65b98` | `O2` | reviewed: Inspected the advertised release archive on 2026-09-10 (SHA-256 1b15f6b7e79cdce6f66eef6663d08bec556c44ae010b94137346a06de9d4666f). Every emulator launch.sh is under Emus/<device>/O2.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/tsolfan/libretro-o2em-nextui/releases/download/v1.0.0/O2.pak.zip |
| PICO | `bR1mQ7yC2t` | 0.7.0 | `PICO.pak.zip` | `688e64c35f47` | `PICO` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so PICO.pak.zip installs `PICO.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by josegonzalez/minui-pico-8-pak@688e64c35f47 (0.7.0) |
| PORTS | `gQ7hT4dK9c` | 2.14.0 | `PORTS.pakz` | `a0f7d82ae6e5` | `PORTS` | reviewed: Reviewed release-pakz: it clears /tmp/pakz-build, archives HEAD into Emus/tg5040/PORTS.pak, copies generated dependencies inside that pak, adds Roms/Ports (PORTS), and zips the entire staging directory. PAK_NAME comes from the release pak.json and PAK_DIR is Emus/tg5040. No second emulator suffix is assembled. Source packaging evidence; the asset was not downloaded. https://github.com/ben16w/minui-portmaster/blob/a0f7d82ae6e571a2093fd816476aa513e3529bfd/Makefile |
| PSP | `mL1wS5bV8n` | 6.2.0 | `PSP.pak.zip` | `0c65e1c1a009` | `PSP` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so PSP.pak.zip installs `PSP.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by ben16w/minui-psp@0c65e1c1a009 (6.2.0) |
| ScummVM | `zK8wB1jL4g` | 0.3.0 | `SCUMMVM.pak.zip` | `f60d7bad2ce1` | `SCUMMVM`, `ScummVM` | reviewed: Two installed directory names are reachable for the same release. The Pak Store installs Emus/<device>/<storefront name>.pak, and the storefront name is 'ScummVM'. The release asset is SCUMMVM.pak.zip and the pak's own README documents the ROM folder as '/Roms/ScummVM (SCUMMVM)/', which only resolves against a SCUMMVM.pak directory -- what a manual install of that asset produces. Both suffixes therefore occur in the wild and both are recorded. NextUI matches a ROM folder suffix against the emulator directory name, so the two are distinct suffixes, not spellings. https://github.com/laesetuc/minui-scummvm/blob/f60d7bad2ce1/README.md |
| SGX | `8SdCQx7vgn` | v1.0.0 | `SGX.pak.zip` | `db518a2c28e8` | `SGX` | reviewed: Inspected the advertised release archive on 2026-09-10 (SHA-256 13b9512829cce26e601ca640cfa9e39aebc1971728727e7757b1ba1ae5a2d8d2). Every emulator launch.sh is under Emus/<device>/SGX.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/tsolfan/beetle-supergrafx-libretro-nextui/releases/download/v1.0.0/SGX.pak.zip |
| SMSU | `tY5xB7qL3r` | v1.1.1 | `SMSU.pak.zip` | `6edbd2082b96` | `SMSU` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so SMSU.pak.zip installs `SMSU.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by SkyZ0ro/Trimui-Brick-GMD@6edbd2082b96 (v1.1.1) |
| SS | `bGeMnFrpN6` | 1.9.1 | `SS.pak.zip` | `749c60300015` | `SS` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so SS.pak.zip installs `SS.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by SouthwestDivineFist/SS-pak@749c60300015 (1.9.1) |
| SWAN | `9zWXgEipEd` | v1.0.0 | `SWAN.pak.zip` | `12abab79ba9b` | `SWAN` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so SWAN.pak.zip installs `SWAN.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by discus7l/nextui-swanstation-pak@12abab79ba9b (v1.0.0) |
| TIC | `rH6cY9zM2g` | 0.3.0 | `TIC.pak.zip` | `0fb83c6e69d1` | `TIC` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so TIC.pak.zip installs `TIC.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by amayer5125/nextui-tic-80-pak@0fb83c6e69d1 (0.3.0) |
| WSC | `UTRIL9C3F1` | v1.0.0 | `WSC.pak.zip` | `80766a22450f` | `WSC` | store-install-rule: the Pak Store extracts a non-.pakz EMU release into `Emus/<device>/<storefront name>.pak`, so WSC.pak.zip installs `WSC.pak` (LoveRetro/nextui-pak-store utils/functions.go@d234b873 UnzipPakArchive); corroborated by alecrem/Trimui-Brick-WSC@80766a22450f (v1.0.0) |
| ZQUEST | `qM2cH6zW1r` | v1.0.0 | `ZQUEST.pakz` | `7d7519aad66f` | `ZQUEST` | reviewed: Inspected the advertised release archive on 2026-09-10 (SHA-256 11ed82bea3d41f60ae48cf56fcf051a87de04f972c935c9a2e7ef5feab07e468). Every emulator launch.sh is under Emus/<device>/ZQUEST.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/cobaltgit/Zelda-Classic-MinUI/releases/download/v1.0.0/ZQUEST.pakz |

## Corrections to the pre-catalog tables

Associations the static tables in `src/systems.c` got wrong. Each was found by resolving the shipped ScreenScraper ID against the imported platform list.

- **`C128`** — was ScreenScraper 87, which is the Amstrad GX4000. ScreenScraper has no Commodore 128 platform, so the suffix now offers Commodore 64 as a candidate instead of carrying a wrong default.
- **`COLECO`** — was ScreenScraper 60, which is PlayStation 4. ColecoVision is 48. Every COLECO scrape made against the old table queried the wrong platform.
- **`MSX`** — was ScreenScraper 62, which is PS Vita. MSX is 113.

## Provider review evidence

- **commodore64 / libretro_dir** — Reviewed all 45 cht/ directories at the pinned libretro snapshot on 2026-09-10: no Commodore 64 cheat database is present. https://github.com/libretro/libretro-database/tree/ff28a5e5bca21f7ae2001602d2e0585cf66c9b5c/cht

## Reviewed target meaning

What each suffix targets, and the evidence it rests on. Nothing here is inferred from a suffix's spelling.

- **`32X`** — singular: Sega 32X
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=picodrive)
  - NextUI ships one suffix per system; this pak runs the picodrive core for Sega 32X.
- **`3DO`** — singular: 3DO Interactive Multiplayer
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Opera core; '3DO addon for minarch, utilizing the Opera core'.
- **`A2600`** — singular: Atari 2600
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=stella2014)
  - NextUI ships one suffix per system; this pak runs the stella2014 core for Atari 2600.
- **`A5200`** — singular: Atari 5200
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=a5200)
  - NextUI ships one suffix per system; this pak runs the a5200 core for Atari 5200.
- **`A7800`** — singular: Atari 7800
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=prosystem)
  - NextUI ships one suffix per system; this pak runs the prosystem core for Atari 7800.
- **`A800`** — multi: Atari 8-bit computers (400/800/XL/XE), Atari 5200
  - evidence: Pak Store entry description at the audited storefront snapshot
  - The pak states the core 'emulates Atari 8-bit computers (400, 800, XL, XE) and the 5200 console'. One suffix therefore spans two scraping platforms and needs folder selection, even though the pre-catalog table mapped A800 to Atari 800 alone.
- **`C128`** — singular: Commodore 128
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=vice_x128)
  - NextUI ships one suffix per system; this pak runs the vice_x128 core for Commodore 128.
- **`C64`** — singular: Commodore 64
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=vice_x64)
  - NextUI ships one suffix per system; this pak runs the vice_x64 core for Commodore 64.
- **`COLECO`** — singular: ColecoVision
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=gearcoleco)
  - NextUI ships one suffix per system; this pak runs the gearcoleco core for ColecoVision.
- **`CPC`** — singular: Amstrad CPC
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=cap32)
  - NextUI ships one suffix per system; this pak runs the cap32 core for Amstrad CPC.
- **`DC`** — singular: Dreamcast
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Flycast standalone.
- **`DICE`** — singular: Arcade (discrete logic, pre-CPU)
  - evidence: Pak Store entry description at the audited storefront snapshot
  - DICE emulates discrete integrated-circuit arcade hardware, not a CPU-based board. Whether either provider catalogues these games is a separate decision; do not assign a generic Arcade ID.
- **`EASYRPG`** — singular: RPG Maker 2000/2003 games
  - evidence: Pak Store entry description at the audited storefront snapshot
- **`FBN`** — singular: Arcade (FinalBurn Neo)
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=fbneo)
  - The FinalBurn Neo core covers many arcade boards, but the providers treat FBNeo arcade games as one platform.
- **`FC`** — singular: Nintendo Entertainment System / Famicom
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=fceumm)
  - NextUI ships one suffix per system; this pak runs the fceumm core for Nintendo Entertainment System / Famicom.
- **`FDS`** — singular: Famicom Disk System
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=fceumm)
  - NextUI ships one suffix per system; this pak runs the fceumm core for Famicom Disk System.
- **`GB`** — singular: Game Boy
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=gambatte)
  - NextUI ships one suffix per system; this pak runs the gambatte core for Game Boy.
- **`GBA`** — singular: Game Boy Advance
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=gpsp)
  - NextUI ships one suffix per system; this pak runs the gpsp core for Game Boy Advance.
- **`GBC`** — singular: Game Boy Color
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=gambatte)
  - NextUI ships one suffix per system; this pak runs the gambatte core for Game Boy Color.
- **`GG`** — singular: Game Gear
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=picodrive)
  - NextUI ships one suffix per system; this pak runs the picodrive core for Game Gear.
- **`GPGX`** — multi: Mega Drive / Genesis, Master System, Game Gear, SG-1000, Mega-CD / Sega CD
  - evidence: Pak Store entry description at the audited storefront snapshot
  - The pak describes the Genesis Plus GX core for 'Sega Genesis/Mega Drive, Master System, Game Gear, SG-1000, and Sega/Mega CD', and its v1.0.0 README documents one folder per system sharing the GPGX suffix. A single bundled default would misclassify four of the five folders. https://github.com/Helaas/nextui-gppx-pak/blob/v1.0.0/README.md#roms
- **`GW`** — singular: Game & Watch (MADrigal simulators)
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Not cartridge dumps: the gw core runs MADrigal handheld simulations, so provider catalogues must be checked before any availability claim.
- **`INTV`** — singular: Intellivision
  - evidence: Pak Store entry description at the audited storefront snapshot
  - FreeINTV core; 'Play classic Intellivision games with the FreeINTV core'.
- **`J2ME`** — singular: Java ME (J2ME) mobile games
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Provider coverage for J2ME must be verified independently for each provider.
- **`JAGUAR`** — singular: Atari Jaguar
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Virtual Jaguar core.
- **`LYNX`** — singular: Atari Lynx
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=handy)
  - NextUI ships one suffix per system; this pak runs the handy core for Atari Lynx.
- **`MD`** — singular: Mega Drive / Genesis
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=picodrive)
  - NextUI ships one suffix per system; this pak runs the picodrive core for Mega Drive / Genesis.
- **`MGBA`** — singular: Game Boy Advance
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=mgba)
  - NextUI ships one suffix per system; this pak runs the mgba core for Game Boy Advance.
- **`MKXPZ`** — singular: RPG Maker XP/VX/VX Ace games
  - evidence: Pak Store entry description at the audited storefront snapshot
  - mkxp-z is a runtime for RPG Maker XP/VX/VX Ace projects rather than a console; relevant targets and provider limits are a separate review.
- **`MSX`** — singular: MSX
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=bluemsx)
  - blueMSX also runs ColecoVision and SG-1000 media, but NextUI ships separate COLECO and SG1000 suffixes for those, so this suffix means the MSX family.
- **`N64`** — singular: Nintendo 64
  - evidence: Pak Store entry description at the audited storefront snapshot
  - mupen64plus standalone.
- **`NDS`** — singular: Nintendo DS
  - evidence: Pak Store entry description at the audited storefront snapshot
  - DraStic standalone.
- **`NEOCD`** — singular: Neo Geo CD
  - evidence: Pak Store entry description at the audited storefront snapshot
  - NeoCD libretro core.
- **`NGP`** — singular: Neo Geo Pocket
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=race)
  - NextUI ships one suffix per system; this pak runs the race core for Neo Geo Pocket.
- **`NGPC`** — singular: Neo Geo Pocket Color
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=race)
  - NextUI ships one suffix per system; this pak runs the race core for Neo Geo Pocket Color.
- **`O2`** — singular: Magnavox Odyssey² / Philips Videopac
  - evidence: Pak Store entry description at the audited storefront snapshot
  - O2EM core; the pak names both regional brands.
- **`P8`** — singular: PICO-8
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=fake08)
  - NextUI ships one suffix per system; this pak runs the fake08 core for PICO-8.
- **`PCE`** — singular: PC Engine / TurboGrafx-16
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=mednafen_pce_fast)
  - The core also runs CD-ROM^2 titles; the providers file those under the same PC Engine platform.
- **`PET`** — singular: Commodore PET
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=vice_xpet)
  - NextUI ships one suffix per system; this pak runs the vice_xpet core for Commodore PET.
- **`PICO`** — singular: PICO-8
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Alias of the NextUI P8 suffix, from a separate Pak Store entry.
- **`PKM`** — singular: Pokémon Mini
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=pokemini)
  - NextUI ships one suffix per system; this pak runs the pokemini core for Pokémon Mini.
- **`PLUS4`** — singular: Commodore Plus/4
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=vice_xplus4)
  - NextUI ships one suffix per system; this pak runs the vice_xplus4 core for Commodore Plus/4.
- **`PORTS`** — open-ended: no single platform
  - evidence: Pak Store entry description at the audited storefront snapshot
  - PortMaster installs native game ports chosen by the user. The content is open-ended, so no platform can be assigned to the suffix. Open-ended content is not evidence that none of the games are scrapeable, and PORTS must not be hidden by default.
- **`PRBOOM`** — singular: Doom engine games (id Software IWAD/PWAD)
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=prboom)
  - PrBoom plays Doom-engine WADs, not a hardware platform. The scraping target is the Doom game set, not a generic PC ID; the provider decision is a separate review.
- **`PS`** — singular: PlayStation
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=pcsx_rearmed)
  - NextUI ships one suffix per system; this pak runs the pcsx_rearmed core for PlayStation.
- **`PSP`** — singular: PlayStation Portable
  - evidence: Pak Store entry description at the audited storefront snapshot
  - PPSSPP standalone.
- **`PUAE`** — singular: Commodore Amiga
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=puae2021)
  - NextUI ships one suffix per system; this pak runs the puae2021 core for Commodore Amiga.
- **`SCUMMVM`** — singular: ScummVM point-and-click adventures
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Suffix produced by a manual install of the SCUMMVM.pak.zip asset, and the folder name the pak's README documents.
- **`SEGACD`** — singular: Mega-CD / Sega CD
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=picodrive)
  - NextUI ships one suffix per system; this pak runs the picodrive core for Mega-CD / Sega CD.
- **`SFC`** — singular: Super Famicom / SNES
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=snes9x)
  - NextUI ships one suffix per system; this pak runs the snes9x core for Super Famicom / SNES.
- **`SG1000`** — singular: SG-1000
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=picodrive)
  - NextUI ships one suffix per system; this pak runs the picodrive core for SG-1000.
- **`SGB`** — singular: Game Boy (Super Game Boy)
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=mgba)
  - Super Game Boy plays Game Boy cartridges with SGB enhancements; the providers catalogue the games as Game Boy.
- **`SGX`** — singular: PC Engine SuperGrafx
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Beetle PC Engine SuperGrafx core. The pre-catalog SUPERGRAFX tag stays as an alias.
- **`SMS`** — singular: Master System
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=picodrive)
  - NextUI ships one suffix per system; this pak runs the picodrive core for Master System.
- **`SMSU`** — singular: Mega Drive / Genesis (MSU-MD)
  - evidence: Pak Store entry description at the audited storefront snapshot
  - A Genesis Plus GX (PUNCHiUM) build with MD-MSU support. Despite the spelling this is not Master System. How the providers represent MSU-MD modified games still needs verification.
- **`SS`** — singular: Sega Saturn
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Yaba Sanshiro standalone.
- **`SUPA`** — singular: Super Famicom / SNES
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=mednafen_supafaust)
  - NextUI ships one suffix per system; this pak runs the mednafen_supafaust core for Super Famicom / SNES.
- **`SWAN`** — singular: PlayStation
  - evidence: Pak Store entry description at the audited storefront snapshot
  - SwanStation is a PlayStation core: the pak describes 'A PSX addon for minarch, utilizing the Swanstation core'. Despite the spelling this is not WonderSwan.
- **`ScummVM`** — singular: ScummVM point-and-click adventures
  - evidence: Pak Store entry description at the audited storefront snapshot
  - Suffix produced by a Pak Store install, which names the directory from the storefront entry ('ScummVM'). Same games as SCUMMVM, different directory name.
- **`TIC`** — singular: TIC-80
  - evidence: Pak Store entry description at the audited storefront snapshot
- **`VB`** — singular: Virtual Boy
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=mednafen_vb)
  - NextUI ships one suffix per system; this pak runs the mednafen_vb core for Virtual Boy.
- **`VIC`** — singular: VIC-20
  - evidence: NextUI skeleton pak launch.sh (EMU_EXE=vice_xvic)
  - NextUI ships one suffix per system; this pak runs the vice_xvic core for VIC-20.
- **`WSC`** — multi: WonderSwan, WonderSwan Color
  - evidence: Pak Store entry description at the audited storefront snapshot
  - The pak describes the Beetle WonderSwan core as emulating 'Bandai WonderSwan / WonderSwan Color', so one suffix spans both monochrome and colour libraries.
- **`ZQUEST`** — singular: Zelda Classic quests (v2.10-compatible)
  - evidence: Pak Store entry description at the audited storefront snapshot
  - User-authored quests for the Zelda Classic engine. Provider limits are a separate review; absence of a catalogue entry must be recorded as a decision, not assumed.

### Reviewed packaging evidence

- **NEOCD** (`23hbrUZWLY`), storefront version `v1.0.0`, source commit `00cfd674fd6bdf836c534f90ca9d31650e4c7eee` — installs `NEOCD`. Inspected the advertised release archive on 2026-09-10 (SHA-256 776b7814f7480e0f7951c9b3e13d96babed59921fcf37cd5ca6798c261b6ddc9). Every emulator launch.sh is under Emus/<device>/NEOCD.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/tsolfan/neocd_libretro-nextui/releases/download/v1.0.0/NEOCD.pak.zip
- **GW** (`2J5vkdSppU`), storefront version `v1.0.0`, source commit `a32e8926009ce35ab2c7113908d74a3c1dbbc018` — installs `GW`. Inspected the advertised release archive on 2026-09-10 (SHA-256 f162102b5347e6fec96260f1f8b5c9e7ccf63ede1dd7d923be0cddc72d554331). Every emulator launch.sh is under Emus/<device>/GW.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/pawndev/Game-Watch-NextUI/releases/download/v1.0.0/GW.pakz
- **SGX** (`8SdCQx7vgn`), storefront version `v1.0.0`, source commit `db518a2c28e821f351dc585340c9b42e5e40b162` — installs `SGX`. Inspected the advertised release archive on 2026-09-10 (SHA-256 13b9512829cce26e601ca640cfa9e39aebc1971728727e7757b1ba1ae5a2d8d2). Every emulator launch.sh is under Emus/<device>/SGX.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/tsolfan/beetle-supergrafx-libretro-nextui/releases/download/v1.0.0/SGX.pak.zip
- **GPGX** (`9duidHBqAk`), storefront version `v1.0.0`, source commit `b2ef7fdcb19eb94e3ff3dc4a03d6cfc959aaf7a6` — installs `GPGX`. Reviewed the complete assemble_pak and package recipes at the release commit: launch.sh and the core enter GPGX.pak, then a clean staging directory receives only Emus/tg5040/GPGX.pak, Emus/tg5050/GPGX.pak and Emus/my355/GPGX.pak before zip creates GPGX.pakz. Device repetitions are one suffix. Source packaging evidence; the asset was not downloaded. https://github.com/Helaas/nextui-gppx-pak/blob/b2ef7fdcb19eb94e3ff3dc4a03d6cfc959aaf7a6/Makefile
- **O2** (`FxBRHBO4mo`), storefront version `v1.0.0`, source commit `c957aab65b983c2ece61234ad3f41eb6281188c8` — installs `O2`. Inspected the advertised release archive on 2026-09-10 (SHA-256 1b15f6b7e79cdce6f66eef6663d08bec556c44ae010b94137346a06de9d4666f). Every emulator launch.sh is under Emus/<device>/O2.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/tsolfan/libretro-o2em-nextui/releases/download/v1.0.0/O2.pak.zip
- **PORTS** (`gQ7hT4dK9c`), storefront version `2.14.0`, source commit `a0f7d82ae6e571a2093fd816476aa513e3529bfd` — installs `PORTS`. Reviewed release-pakz: it clears /tmp/pakz-build, archives HEAD into Emus/tg5040/PORTS.pak, copies generated dependencies inside that pak, adds Roms/Ports (PORTS), and zips the entire staging directory. PAK_NAME comes from the release pak.json and PAK_DIR is Emus/tg5040. No second emulator suffix is assembled. Source packaging evidence; the asset was not downloaded. https://github.com/ben16w/minui-portmaster/blob/a0f7d82ae6e571a2093fd816476aa513e3529bfd/Makefile
- **ZQUEST** (`qM2cH6zW1r`), storefront version `v1.0.0`, source commit `7d7519aad66f53a2013bfccaa42b897b8b0d4f46` — installs `ZQUEST`. Inspected the advertised release archive on 2026-09-10 (SHA-256 11ed82bea3d41f60ae48cf56fcf051a87de04f972c935c9a2e7ef5feab07e468). Every emulator launch.sh is under Emus/<device>/ZQUEST.pak for the listed devices; no other emulator pak is present. This is an SD-root layout. https://github.com/cobaltgit/Zelda-Classic-MinUI/releases/download/v1.0.0/ZQUEST.pakz
- **INTV** (`yK3mJ7zP1n`), storefront version `v0.0.0`, source commit `7535b60599b3` — installs `INTV`. The v0.0.0 release attaches INTV.pak.zip and the storefront names the pak INTV, so the Pak Store installs Emus/<device>/INTV.pak. The pak.json committed at the tag carries the display string 'Intellivision (INTV)', which was corrected to 'INTV' on the default branch the storefront is built from; it is a stale display name, not a second installed directory. https://github.com/K2Retro2nd/minui-intv-pak/releases/tag/v0.0.0
- **ScummVM** (`zK8wB1jL4g`), storefront version `0.3.0`, source commit `f60d7bad2ce1` — installs `SCUMMVM`, `ScummVM`. Two installed directory names are reachable for the same release. The Pak Store installs Emus/<device>/<storefront name>.pak, and the storefront name is 'ScummVM'. The release asset is SCUMMVM.pak.zip and the pak's own README documents the ROM folder as '/Roms/ScummVM (SCUMMVM)/', which only resolves against a SCUMMVM.pak directory -- what a manual install of that asset produces. Both suffixes therefore occur in the wild and both are recorded. NextUI matches a ROM folder suffix against the emulator directory name, so the two are distinct suffixes, not spellings. https://github.com/laesetuc/minui-scummvm/blob/f60d7bad2ce1/README.md

## Findings

### Mapping or installation gaps (3)

- **NEOCD (23hbrUZWLY)** — The verified NEOCD.pak.zip contains an SD-root Emus/<device>/NEOCD.pak layout, but its .pak.zip storefront filename selects extraction below Emus/<device>/NEOCD.pak. The pinned Pak Store extractor preserves the nested paths, leaving no launch.sh at the expected location. Manual extraction at the SD root works; the advertised store package needs an upstream filename/layout correction. https://github.com/LoveRetro/nextui-pak-store/blob/d234b873/utils/functions.go#L220
- **O2 (FxBRHBO4mo)** — The verified O2.pak.zip contains an SD-root Emus/<device>/O2.pak layout, but its .pak.zip storefront filename selects extraction below Emus/<device>/O2.pak. The pinned Pak Store extractor preserves the nested paths, leaving no launch.sh at the expected location. Manual extraction at the SD root works; the advertised store package needs an upstream filename/layout correction. https://github.com/LoveRetro/nextui-pak-store/blob/d234b873/utils/functions.go#L220
- **SGX (8SdCQx7vgn)** — The verified SGX.pak.zip contains an SD-root Emus/<device>/SGX.pak layout, but its .pak.zip storefront filename selects extraction below Emus/<device>/SGX.pak. The pinned Pak Store extractor preserves the nested paths, leaving no launch.sh at the expected location. Manual extraction at the SD root works; the advertised store package needs an upstream filename/layout correction. https://github.com/LoveRetro/nextui-pak-store/blob/d234b873/utils/functions.go#L220

### Informational (5)

- **MKXPZ** — reviewed as having no suitable catalog target: mkxp-z runs RPG Maker XP/VX/VX Ace projects; neither provider catalogues them as a platform. The folder stays visible and mappable
- **PORTS** — reviewed as having no suitable catalog target: PortMaster installs user-chosen native game ports; the content is open-ended, so no single platform applies. The folder stays visible and mappable
- **PORTS** — reviewed as open-ended content with no single platform: a documented limit, not an unresolved decision. PortMaster installs native game ports chosen by the user. The content is open-ended, so no platform can be assigned to the suffix. Open-ended content is not evidence that none of the games are scrapeable, and PORTS must not be hidden by default.
- **SUPERGRAFX** — catalog alias with no currently observed pak; retained, informational only
- **ZQUEST** — reviewed as having no suitable catalog target: Zelda Classic quests are user-authored content with no platform entry at either provider. The folder stays visible and mappable

## Summary

- Distinct suffixes discovered: 64
  - from the NextUI skeleton: 37
  - from Pak Store emulator releases: 27 (27 not in the skeleton)
- Store releases with confirmed installed suffixes: 26 of 26
  - Source/asset candidates remain listed above as unresolved until release packaging is reviewed.
- Suffixes needing folder selection (multi-system): 3
- Suffixes reviewed as open-ended content: 1
- Unambiguous defaults: 57
- Requiring folder selection: 4
- ScreenScraper supported: 61
- Cheat databases supported: 39
- Incomplete sources: 0
- Unresolved decisions: 0
- Mapping/installation gaps: 3

Recognising a suffix name is not coverage, and user overrides on a device cannot close a catalog gap.

Generated by `scripts/audit_systems.py` at 2026-09-10T19:01:48Z; exit code 1.

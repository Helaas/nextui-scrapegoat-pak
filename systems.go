package main

// systems.go — NextUI system tag mappings for ScreenScraper.fr and Libretro.
//
// Source of truth for system tags: NextUI project (workspace/all/minarch/ra_consoles.h
// and Emus directories). All 32 supported systems are listed here.

// SSPlatformIDs maps NextUI system tags to ScreenScraper.fr platform IDs.
// Systems without a meaningful ScreenScraper equivalent (P8, PRBOOM) are omitted.
// MGBA and SUPA share IDs with GBA and SFC respectively (same platform, different core).
var SSPlatformIDs = map[string]int{
	// Base systems
	"FC":  3,  // Famicom / NES
	"GB":  9,  // Game Boy
	"GBA": 12, // Game Boy Advance
	"GBC": 10, // Game Boy Color
	"MD":  1,  // Mega Drive / Genesis
	"PS":  57, // PlayStation
	"SFC": 4,  // Super Famicom / SNES
	// Extra systems
	"32X":    19,  // Sega 32X
	"A2600":  26,  // Atari 2600
	"A5200":  40,  // Atari 5200
	"A7800":  41,  // Atari 7800
	"C64":    66,  // Commodore 64
	"C128":   87,  // Commodore 128
	"COLECO": 60,  // ColecoVision
	"CPC":    65,  // Amstrad CPC
	"FBN":    75,  // FinalBurn Neo / Arcade
	"FDS":    106, // Famicom Disk System
	"GG":     21,  // Game Gear
	"LYNX":   28,  // Atari Lynx
	"MGBA":   12,  // Game Boy Advance via mGBA (same platform as GBA)
	"MSX":    62,  // MSX
	"NGP":    25,  // Neo Geo Pocket
	"NGPC":   82,  // Neo Geo Pocket Color
	"PCE":    31,  // PC Engine / TurboGrafx-16
	"PKM":    211, // Pokémon Mini
	"PUAE":   64,  // Amiga
	"SEGACD": 20,  // Sega CD / Mega-CD
	"SG1000": 109, // SG-1000
	"SGB":    9,   // Super Game Boy (treat as Game Boy)
	"SMS":    2,   // Master System
	"SUPA":   4,   // Super Famicom via Supafaust (same platform as SFC)
	"VB":     11,  // Virtual Boy
	"VIC":    73,  // VIC-20
}

// LibretroSystemDirs maps NextUI system tags to libretro-database/cht/ directory names
// on GitHub (github.com/libretro/libretro-database/tree/master/cht/).
// Systems without cheat databases in libretro (P8, PRBOOM, PET, PLUS4, PKM, VIC, C128) are omitted.
var LibretroSystemDirs = map[string]string{
	// Base systems
	"FC":  "Nintendo - Nintendo Entertainment System",
	"GB":  "Nintendo - Game Boy",
	"GBA": "Nintendo - Game Boy Advance",
	"GBC": "Nintendo - Game Boy Color",
	"MD":  "Sega - Mega Drive - Genesis",
	"PS":  "Sony - PlayStation",
	"SFC": "Nintendo - Super Nintendo Entertainment System",
	// Extra systems
	"32X":    "Sega - 32X",
	"A2600":  "Atari - 2600",
	"A5200":  "Atari - 5200",
	"A7800":  "Atari - 7800",
	"COLECO": "Coleco - ColecoVision",
	"FBN":    "FBNeo - Arcade Games",
	"FDS":    "Nintendo - Family Computer Disk System",
	"GG":     "Sega - Game Gear",
	"LYNX":   "Atari - Lynx",
	"MGBA":   "Nintendo - Game Boy Advance",
	"MSX":    "Microsoft - MSX - MSX2 - MSX2P - MSX Turbo R",
	"PCE":    "NEC - PC Engine - TurboGrafx 16",
	"SEGACD": "Sega - Mega-CD - Sega CD",
	"SGB":    "Nintendo - Game Boy",
	"SMS":    "Sega - Master System - Mark III",
	"SUPA":   "Nintendo - Super Nintendo Entertainment System",
	// Removed: C64, CPC, NGP, NGPC, PUAE, SG1000, VB — no cht/ directories in libretro-database
}

// SSDisplayNames maps NextUI system tags to human-readable names for UI display.
var SSDisplayNames = map[string]string{
	"FC":     "Famicom / NES",
	"GB":     "Game Boy",
	"GBA":    "Game Boy Advance",
	"GBC":    "Game Boy Color",
	"MD":     "Mega Drive",
	"PS":     "PlayStation",
	"SFC":    "Super Famicom",
	"32X":    "Sega 32X",
	"A2600":  "Atari 2600",
	"A5200":  "Atari 5200",
	"A7800":  "Atari 7800",
	"C64":    "Commodore 64",
	"C128":   "Commodore 128",
	"COLECO": "ColecoVision",
	"CPC":    "Amstrad CPC",
	"FBN":    "Arcade (FBNeo)",
	"FDS":    "Famicom Disk System",
	"GG":     "Game Gear",
	"LYNX":   "Atari Lynx",
	"MGBA":   "Game Boy Advance (mGBA)",
	"MSX":    "MSX",
	"NGP":    "Neo Geo Pocket",
	"NGPC":   "Neo Geo Pocket Color",
	"P8":     "PICO-8",
	"PCE":    "PC Engine",
	"PET":    "Commodore PET",
	"PKM":    "Pokémon Mini",
	"PLUS4":  "Commodore Plus/4",
	"PRBOOM": "Doom (PrBoom)",
	"PUAE":   "Amiga",
	"SEGACD": "Sega CD",
	"SG1000": "SG-1000",
	"SGB":    "Super Game Boy",
	"SMS":    "Master System",
	"SUPA":   "Super Famicom (Supafaust)",
	"VB":     "Virtual Boy",
	"VIC":    "VIC-20",
}

// systemDisplayName returns the human-readable name for a tag, falling back to the tag itself.
func systemDisplayName(tag string) string {
	if name, ok := SSDisplayNames[tag]; ok {
		return name
	}
	return tag
}

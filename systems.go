package main

// systems.go — NextUI system tag mappings for ScreenScraper.fr and Libretro.
//
// Source of truth for system tags: NextUI core tags plus community pak tags
// from the NextUI Pak Store.

// SSPlatformIDs maps NextUI system tags to ScreenScraper.fr platform IDs.
// MGBA and SUPA share IDs with GBA and SFC respectively (same platform, different core).
// P8/PICO both map to the PICO-8 platform ID for compatibility with both folder tags.
// Systems without a meaningful ScreenScraper equivalent (e.g. PRBOOM, PET, PLUS4) are omitted.
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
	"32X":        19,  // Sega 32X
	"A2600":      26,  // Atari 2600
	"A5200":      40,  // Atari 5200
	"A7800":      41,  // Atari 7800
	"A800":       43,  // Atari 8-bit / Atari 800
	"C64":        66,  // Commodore 64
	"C128":       87,  // Commodore 128
	"DC":         23,  // Dreamcast
	"EASYRPG":    231, // RPG Maker 2000/2003
	"INTV":       115, // Intellivision
	"JAGUAR":     27,  // Atari Jaguar
	"N64":        14,  // Nintendo 64
	"NDS":        15,  // Nintendo DS
	"COLECO":     60,  // ColecoVision
	"CPC":        65,  // Amstrad CPC
	"3DO":        29,  // 3DO
	"FBN":        75,  // FinalBurn Neo / Arcade
	"FDS":        106, // Famicom Disk System
	"GG":         21,  // Game Gear
	"LYNX":       28,  // Atari Lynx
	"MGBA":       12,  // Game Boy Advance via mGBA (same platform as GBA)
	"MSX":        62,  // MSX
	"NGP":        25,  // Neo Geo Pocket
	"NGPC":       82,  // Neo Geo Pocket Color
	"P8":         234, // PICO-8 (legacy tag)
	"PCE":        31,  // PC Engine / TurboGrafx-16
	"PICO":       234, // PICO-8 (community tag)
	"PKM":        211, // Pokémon Mini
	"PSP":        61,  // PlayStation Portable
	"PUAE":       64,  // Amiga
	"SCUMMVM":    123, // ScummVM
	"SEGACD":     20,  // Sega CD / Mega-CD
	"SG1000":     109, // SG-1000
	"SGB":        9,   // Super Game Boy (treat as Game Boy)
	"SMS":        2,   // Master System
	"SUPERGRAFX": 105, // PC Engine SuperGrafx
	"SUPA":       4,   // Super Famicom via Supafaust (same platform as SFC)
	"TIC":        222, // TIC-80
	"VB":         11,  // Virtual Boy
	"VIC":        73,  // VIC-20
}

// LibretroSystemDirs maps NextUI system tags to libretro-database/cht/ directory names
// on GitHub (github.com/libretro/libretro-database/tree/master/cht/).
// Systems without cheat databases in libretro (e.g. 3DO, SG1000, P8/PICO, PRBOOM, PET, PLUS4, PKM, VIC, C128) are omitted.
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
	"32X":        "Sega - 32X",
	"A2600":      "Atari - 2600",
	"A5200":      "Atari - 5200",
	"A7800":      "Atari - 7800",
	"A800":       "Atari - 8-bit Family",
	"COLECO":     "Coleco - ColecoVision",
	"DC":         "Sega - Dreamcast",
	"FBN":        "FBNeo - Arcade Games",
	"FDS":        "Nintendo - Family Computer Disk System",
	"GG":         "Sega - Game Gear",
	"INTV":       "Mattel - Intellivision",
	"JAGUAR":     "Atari - Jaguar",
	"LYNX":       "Atari - Lynx",
	"MGBA":       "Nintendo - Game Boy Advance",
	"MSX":        "Microsoft - MSX - MSX2 - MSX2P - MSX Turbo R",
	"N64":        "Nintendo - Nintendo 64",
	"NDS":        "Nintendo - Nintendo DS",
	"PCE":        "NEC - PC Engine - TurboGrafx 16",
	"PSP":        "Sony - PlayStation Portable",
	"SEGACD":     "Sega - Mega-CD - Sega CD",
	"SGB":        "Nintendo - Game Boy",
	"SMS":        "Sega - Master System - Mark III",
	"SUPERGRAFX": "NEC - PC Engine SuperGrafx",
	"SUPA":       "Nintendo - Super Nintendo Entertainment System",
	"TIC":        "TIC-80",
	// Removed: 3DO, C64, CPC, NGP, NGPC, P8/PICO, PUAE, SG1000, VB — no cht/ directories in libretro-database
}

// SSDisplayNames maps NextUI system tags to human-readable names for UI display.
var SSDisplayNames = map[string]string{
	"FC":         "Famicom / NES",
	"GB":         "Game Boy",
	"GBA":        "Game Boy Advance",
	"GBC":        "Game Boy Color",
	"MD":         "Mega Drive",
	"PS":         "PlayStation",
	"SFC":        "Super Famicom",
	"32X":        "Sega 32X",
	"3DO":        "3DO",
	"A2600":      "Atari 2600",
	"A5200":      "Atari 5200",
	"A7800":      "Atari 7800",
	"A800":       "Atari 800",
	"C64":        "Commodore 64",
	"C128":       "Commodore 128",
	"COLECO":     "ColecoVision",
	"CPC":        "Amstrad CPC",
	"DC":         "Dreamcast",
	"EASYRPG":    "RPG Maker 2000/2003",
	"FBN":        "Arcade (FBNeo)",
	"FDS":        "Famicom Disk System",
	"GG":         "Game Gear",
	"INTV":       "Intellivision",
	"JAGUAR":     "Atari Jaguar",
	"LYNX":       "Atari Lynx",
	"MGBA":       "Game Boy Advance (mGBA)",
	"MSX":        "MSX",
	"N64":        "Nintendo 64",
	"NDS":        "Nintendo DS",
	"NGP":        "Neo Geo Pocket",
	"NGPC":       "Neo Geo Pocket Color",
	"P8":         "PICO-8",
	"PCE":        "PC Engine",
	"PICO":       "PICO-8",
	"PET":        "Commodore PET",
	"PKM":        "Pokémon Mini",
	"PLUS4":      "Commodore Plus/4",
	"PRBOOM":     "Doom (PrBoom)",
	"PUAE":       "Amiga",
	"PSP":        "PlayStation Portable",
	"SCUMMVM":    "ScummVM",
	"SEGACD":     "Sega CD",
	"SG1000":     "SG-1000",
	"SGB":        "Super Game Boy",
	"SMS":        "Master System",
	"SUPERGRAFX": "PC Engine SuperGrafx",
	"SUPA":       "Super Famicom (Supafaust)",
	"TIC":        "TIC-80",
	"VB":         "Virtual Boy",
	"VIC":        "VIC-20",
}

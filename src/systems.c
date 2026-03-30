#include "systems.h"
#include <string.h>

/* ── ScreenScraper platform IDs ───────────────────────────────
 * Maps NextUI system tags to screenscraper.fr platform IDs.
 * MGBA/SUPA share IDs with GBA/SFC (same platform, different core).
 * P8/PICO both map to PICO-8 for compatibility with both folder tags. */

typedef struct { const char *tag; int id; } ss_platform_entry;

static const ss_platform_entry ss_platforms[] = {
    /* Base systems */
    {"FC",   3},   /* Famicom / NES */
    {"GB",   9},   /* Game Boy */
    {"GBA",  12},  /* Game Boy Advance */
    {"GBC",  10},  /* Game Boy Color */
    {"MD",   1},   /* Mega Drive / Genesis */
    {"PS",   57},  /* PlayStation */
    {"SFC",  4},   /* Super Famicom / SNES */
    /* Extra systems */
    {"32X",        19},
    {"3DO",        29},
    {"A2600",      26},
    {"A5200",      40},
    {"A7800",      41},
    {"A800",       43},
    {"C64",        66},
    {"C128",       87},
    {"COLECO",     60},
    {"CPC",        65},
    {"DC",         23},
    {"EASYRPG",    231},
    {"FBN",        75},
    {"FDS",        106},
    {"GG",         21},
    {"INTV",       115},
    {"JAGUAR",     27},
    {"LYNX",       28},
    {"MGBA",       12},
    {"MSX",        62},
    {"N64",        14},
    {"NDS",        15},
    {"NGP",        25},
    {"NGPC",       82},
    {"P8",         234},
    {"PCE",        31},
    {"PICO",       234},
    {"PKM",        211},
    {"PSP",        61},
    {"PUAE",       64},
    {"SCUMMVM",    123},
    {"SEGACD",     20},
    {"SG1000",     109},
    {"SGB",        9},
    {"SMS",        2},
    {"SS",         22},
    {"SUPERGRAFX", 105},
    {"SUPA",       4},
    {"TIC",        222},
    {"VB",         11},
    {"VIC",        73},
};

int ss_platform_id(const char *tag) {
    for (size_t i = 0; i < sizeof(ss_platforms) / sizeof(ss_platforms[0]); i++) {
        if (strcmp(ss_platforms[i].tag, tag) == 0)
            return ss_platforms[i].id;
    }
    return -1;
}

/* ── Libretro cheat directory names ───────────────────────────
 * Maps NextUI system tags to libretro-database/cht/ directory names.
 * Systems without cheat databases in libretro are omitted. */

typedef struct { const char *tag; const char *dir; } libretro_entry;

static const libretro_entry libretro_dirs[] = {
    /* Base systems */
    {"FC",   "Nintendo - Nintendo Entertainment System"},
    {"GB",   "Nintendo - Game Boy"},
    {"GBA",  "Nintendo - Game Boy Advance"},
    {"GBC",  "Nintendo - Game Boy Color"},
    {"MD",   "Sega - Mega Drive - Genesis"},
    {"PS",   "Sony - PlayStation"},
    {"SFC",  "Nintendo - Super Nintendo Entertainment System"},
    /* Extra systems */
    {"32X",        "Sega - 32X"},
    {"A2600",      "Atari - 2600"},
    {"A5200",      "Atari - 5200"},
    {"A7800",      "Atari - 7800"},
    {"A800",       "Atari - 8-bit Family"},
    {"COLECO",     "Coleco - ColecoVision"},
    {"DC",         "Sega - Dreamcast"},
    {"FBN",        "FBNeo - Arcade Games"},
    {"FDS",        "Nintendo - Family Computer Disk System"},
    {"GG",         "Sega - Game Gear"},
    {"INTV",       "Mattel - Intellivision"},
    {"JAGUAR",     "Atari - Jaguar"},
    {"LYNX",       "Atari - Lynx"},
    {"MGBA",       "Nintendo - Game Boy Advance"},
    {"MSX",        "Microsoft - MSX - MSX2 - MSX2P - MSX Turbo R"},
    {"N64",        "Nintendo - Nintendo 64"},
    {"NDS",        "Nintendo - Nintendo DS"},
    {"PCE",        "NEC - PC Engine - TurboGrafx 16"},
    {"PSP",        "Sony - PlayStation Portable"},
    {"SEGACD",     "Sega - Mega-CD - Sega CD"},
    {"SGB",        "Nintendo - Game Boy"},
    {"SMS",        "Sega - Master System - Mark III"},
    {"SS",         "Sega - Saturn"},
    {"SUPERGRAFX", "NEC - PC Engine SuperGrafx"},
    {"SUPA",       "Nintendo - Super Nintendo Entertainment System"},
    {"TIC",        "TIC-80"},
};

const char *libretro_dir(const char *tag) {
    for (size_t i = 0; i < sizeof(libretro_dirs) / sizeof(libretro_dirs[0]); i++) {
        if (strcmp(libretro_dirs[i].tag, tag) == 0)
            return libretro_dirs[i].dir;
    }
    return NULL;
}

/* ── Display names ────────────────────────────────────────────
 * Maps NextUI system tags to human-readable names for UI display. */

typedef struct { const char *tag; const char *name; } display_entry;

static const display_entry display_names[] = {
    {"FC",         "Famicom / NES"},
    {"GB",         "Game Boy"},
    {"GBA",        "Game Boy Advance"},
    {"GBC",        "Game Boy Color"},
    {"MD",         "Mega Drive"},
    {"PS",         "PlayStation"},
    {"SFC",        "Super Famicom"},
    {"32X",        "Sega 32X"},
    {"3DO",        "3DO"},
    {"A2600",      "Atari 2600"},
    {"A5200",      "Atari 5200"},
    {"A7800",      "Atari 7800"},
    {"A800",       "Atari 800"},
    {"C64",        "Commodore 64"},
    {"C128",       "Commodore 128"},
    {"COLECO",     "ColecoVision"},
    {"CPC",        "Amstrad CPC"},
    {"DC",         "Dreamcast"},
    {"EASYRPG",    "RPG Maker 2000/2003"},
    {"FBN",        "Arcade (FBNeo)"},
    {"FDS",        "Famicom Disk System"},
    {"GG",         "Game Gear"},
    {"INTV",       "Intellivision"},
    {"JAGUAR",     "Atari Jaguar"},
    {"LYNX",       "Atari Lynx"},
    {"MGBA",       "Game Boy Advance (mGBA)"},
    {"MSX",        "MSX"},
    {"N64",        "Nintendo 64"},
    {"NDS",        "Nintendo DS"},
    {"NGP",        "Neo Geo Pocket"},
    {"NGPC",       "Neo Geo Pocket Color"},
    {"P8",         "PICO-8"},
    {"PCE",        "PC Engine"},
    {"PET",        "Commodore PET"},
    {"PICO",       "PICO-8"},
    {"PKM",        "Pok\xc3\xa9mon Mini"},
    {"PLUS4",      "Commodore Plus/4"},
    {"PRBOOM",     "Doom (PrBoom)"},
    {"PUAE",       "Amiga"},
    {"PSP",        "PlayStation Portable"},
    {"SCUMMVM",    "ScummVM"},
    {"SEGACD",     "Sega CD"},
    {"SG1000",     "SG-1000"},
    {"SGB",        "Super Game Boy"},
    {"SMS",        "Master System"},
    {"SS",         "Sega Saturn"},
    {"SUPERGRAFX", "PC Engine SuperGrafx"},
    {"SUPA",       "Super Famicom (Supafaust)"},
    {"TIC",        "TIC-80"},
    {"VB",         "Virtual Boy"},
    {"VIC",        "VIC-20"},
};

const char *ss_display_name(const char *tag) {
    for (size_t i = 0; i < sizeof(display_names) / sizeof(display_names[0]); i++) {
        if (strcmp(display_names[i].tag, tag) == 0)
            return display_names[i].name;
    }
    return tag;
}

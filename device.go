package main

import (
	"archive/zip"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"

)

// ── Device paths ─────────────────────────────────────────────

const (
	sdcardPath = "/mnt/SDCARD"
	romsPath   = sdcardPath + "/Roms"
	cheatsPath = sdcardPath + "/Cheats"
)

// shortcutMarkerFile is the hidden marker file written by nextui-shortcuts-pak
// inside every shortcut folder. Its content is the clean display name.
const shortcutMarkerFile = ".shortcut"

// ── Data types ───────────────────────────────────────────────

// ConsoleDir represents a ROM console directory.
type ConsoleDir struct {
	Name       string // e.g. "Game Boy Advance (GBA)" or "Game Boy Advance (GBA).disabled"
	Tag        string // e.g. "GBA"
	Path       string // full path to the directory
	Display    string // display name without tag suffix
	IsDisabled bool   // true when the folder ends with ".disabled"
}

// ROMFile represents a ROM file or game folder within a console directory.
type ROMFile struct {
	Name        string // filename or folder name (may include ".disabled" suffix)
	Path        string // full path
	Display     string // display name (no extension, no ".disabled" suffix)
	IsMultiDisc bool   // subdirectory containing {name}.m3u
	IsCueFolder bool   // subdirectory containing {name}.cue
	IsDisabled  bool   // true when the file/folder name ends with ".disabled"
}

// ScrapeSummary holds the results of a scrape or cheat download run.
type ScrapeSummary struct {
	Total    int
	Found    int
	NotFound int
	Errors   int
}

// ── Scanning functions ───────────────────────────────────────

// getSDCardPath returns the SD card root, adjusted for macOS development.
func getSDCardPath() string {
	if sdcard := os.Getenv("SDCARD_PATH"); sdcard != "" {
		return sdcard
	}
	if platform == PlatformMac {
		cwd, _ := os.Getwd()
		return filepath.Join(cwd, "mock_sdcard")
	}
	return sdcardPath
}

// getROMsPath returns the path to the Roms directory.
func getROMsPath() string {
	return filepath.Join(getSDCardPath(), "Roms")
}

// getCheatsPath returns the path to the Cheats directory.
func getCheatsPath() string {
	return filepath.Join(getSDCardPath(), "Cheats")
}

// getCheatRepoPath returns the path to the sparse git clone of the libretro cheat database.
func getCheatRepoPath() string {
	return filepath.Join(getSDCardPath(), ".userdata", "shared", "ScrapeGoat", "libretro-database")
}

// scanConsoleDirs returns all ROM console directories, excluding shortcut folders.
// When showHidden is false (default), directories starting with "." or ending with
// ".disabled" are skipped. When true, they are included (Mac system dotfiles without
// a tag are still excluded).
func scanConsoleDirs(showHidden bool) ([]ConsoleDir, error) {
	romsDir := getROMsPath()
	entries, err := os.ReadDir(romsDir)
	if err != nil {
		return nil, fmt.Errorf("reading roms dir: %w", err)
	}

	var consoles []ConsoleDir
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		name := e.Name()
		fullPath := filepath.Join(romsDir, name)

		// Always skip shortcut folders.
		if isShortcutFolder(fullPath) {
			continue
		}

		if !showHidden {
			if isHidden(name) {
				continue
			}
		} else {
			// With showHidden: still exclude Mac dotfiles (dot-dirs without a TAG).
			if isMacDotfile(name) || name == "map.txt" {
				continue
			}
		}

		// For .disabled folders, strip the suffix before extracting tag/display.
		isDisabled := strings.HasSuffix(name, ".disabled")
		baseName := name
		if isDisabled {
			baseName = strings.TrimSuffix(name, ".disabled")
		}

		tag := extractTag(baseName)
		if tag == "" {
			continue
		}
		consoles = append(consoles, ConsoleDir{
			Name:       name,
			Tag:        tag,
			Path:       fullPath,
			Display:    extractDisplayName(baseName),
			IsDisabled: isDisabled,
		})
	}

	sort.Slice(consoles, func(i, j int) bool {
		return strings.ToLower(consoles[i].Display) < strings.ToLower(consoles[j].Display)
	})
	log.Printf("scanConsoleDirs: showHidden=%v found %d console folders", showHidden, len(consoles))
	return consoles, nil
}

// scanROMs returns all ROM files in a console directory.
// When showHidden is false (default), files/folders starting with "." or ending
// with ".disabled" are skipped. When true, they are included; ".disabled" is
// stripped from the display name and the ROM is marked IsDisabled.
func scanROMs(consoleDir string, showHidden bool) ([]ROMFile, error) {
	entries, err := os.ReadDir(consoleDir)
	if err != nil {
		return nil, fmt.Errorf("reading rom dir: %w", err)
	}

	var roms []ROMFile
	for _, e := range entries {
		name := e.Name()

		if !showHidden {
			if isHidden(name) {
				continue
			}
		} else {
			// Always skip Mac/system artifacts and map.txt regardless of showHidden.
			if strings.HasPrefix(name, ".") || name == "map.txt" {
				continue
			}
		}

		// Strip .disabled suffix for display and m3u/cue detection; mark as disabled.
		isDisabled := strings.HasSuffix(name, ".disabled")
		baseName := name
		if isDisabled {
			baseName = strings.TrimSuffix(name, ".disabled")
		}

		if e.IsDir() {
			dirPath := filepath.Join(consoleDir, name)
			if _, err := os.Stat(filepath.Join(dirPath, baseName+".m3u")); err == nil {
				roms = append(roms, ROMFile{
					Name:        name,
					Path:        dirPath,
					Display:     baseName,
					IsMultiDisc: true,
					IsDisabled:  isDisabled,
				})
			} else if _, err := os.Stat(filepath.Join(dirPath, baseName+".cue")); err == nil {
				roms = append(roms, ROMFile{
					Name:        name,
					Path:        dirPath,
					Display:     baseName,
					IsCueFolder: true,
					IsDisabled:  isDisabled,
				})
			}
			continue
		}
		roms = append(roms, ROMFile{
			Name:       name,
			Path:       filepath.Join(consoleDir, name),
			Display:    stripExtension(baseName),
			IsDisabled: isDisabled,
		})
	}

	sort.Slice(roms, func(i, j int) bool {
		return strings.ToLower(roms[i].Display) < strings.ToLower(roms[j].Display)
	})
	log.Printf("scanROMs: dir=%s showHidden=%v roms=%d", consoleDir, showHidden, len(roms))
	return roms, nil
}

// ── Artwork helpers ──────────────────────────────────────────

// artworkExists reports whether source artwork already exists for a ROM.
// Path: Roms/<consoleDirName>/.media/<displayName>.png
func artworkExists(consoleDirName, displayName string) bool {
	path := filepath.Join(getROMsPath(), consoleDirName, ".media", displayName+".png")
	_, err := os.Stat(path)
	return err == nil
}

// artworkSrcPath returns the expected source artwork path for a ROM.
func artworkSrcPath(consoleDirName, displayName string) string {
	return filepath.Join(getROMsPath(), consoleDirName, ".media", displayName+".png")
}

// ensureMediaDir creates the .media directory under consoleDir if it doesn't exist.
func ensureMediaDir(consoleDirPath string) error {
	return os.MkdirAll(filepath.Join(consoleDirPath, ".media"), 0755)
}

// findShortcutsForROM scans all shortcut folders in Roms/ and returns the paths of
// those whose .m3u points into the given consoleDirName for a ROM with the given display name.
// It uses the .shortcut marker file display name for matching.
func findShortcutsForROM(consoleDirName, romDisplayName string) ([]string, error) {
	romsDir := getROMsPath()
	entries, err := os.ReadDir(romsDir)
	if err != nil {
		return nil, fmt.Errorf("reading roms dir: %w", err)
	}

	var matches []string
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		fullPath := filepath.Join(romsDir, e.Name())
		if !isShortcutFolder(fullPath) {
			continue
		}

		// Read the .m3u to find which console this shortcut points into.
		m3uPath := filepath.Join(fullPath, e.Name()+".m3u")
		data, err := os.ReadFile(m3uPath)
		if err != nil {
			continue
		}
		relPath := strings.TrimSpace(string(data))
		// relPath is "../Console Dir (TAG)/game.rom" — extract the console component.
		parts := strings.SplitN(relPath, "/", 3)
		if len(parts) < 2 || parts[0] != ".." {
			continue
		}
		if parts[1] != consoleDirName {
			continue
		}

		// The shortcut points into our console dir. Check the display name matches.
		shortcutDisplay := readShortcutMarker(fullPath)
		if shortcutDisplay == "" {
			// Fall back to extracting from folder name
			shortcutDisplay = extractDisplayName(e.Name())
			shortcutDisplay = strings.TrimPrefix(shortcutDisplay, "\uFEFF")
			shortcutDisplay = strings.TrimPrefix(shortcutDisplay, "\u2605 ")
		}
		if strings.EqualFold(shortcutDisplay, romDisplayName) {
			matches = append(matches, fullPath)
		}
	}
	log.Printf("findShortcutsForROM: console=%s rom=%s matches=%d", consoleDirName, romDisplayName, len(matches))
	return matches, nil
}

// ── MD5 hashing ──────────────────────────────────────────────

// computeMD5 computes the MD5 hex digest of the ROM content.
// For zip archives, it hashes the largest file inside the archive (the actual ROM).
// For folder-based ROMs (multi-disc, CUE), it hashes the primary disc image file.
func computeMD5(rom ROMFile) (hash string, size int64, err error) {
	targetPath := rom.Path

	if rom.IsMultiDisc || rom.IsCueFolder {
		primary, err := findPrimaryROMFile(rom.Path, rom.IsCueFolder)
		if err != nil || primary == "" {
			return "", 0, fmt.Errorf("no hashable file found in %s: %w", rom.Path, err)
		}
		targetPath = primary
	}

	// For zip archives, hash the inner ROM file.
	if strings.HasSuffix(strings.ToLower(targetPath), ".zip") {
		return computeMD5Zip(targetPath)
	}

	f, err := os.Open(targetPath)
	if err != nil {
		return "", 0, fmt.Errorf("open: %w", err)
	}
	defer f.Close()

	info, err := f.Stat()
	if err != nil {
		return "", 0, fmt.Errorf("stat: %w", err)
	}

	h := md5.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", 0, fmt.Errorf("hashing: %w", err)
	}
	return hex.EncodeToString(h.Sum(nil)), info.Size(), nil
}

// computeMD5Zip opens a zip archive and hashes the largest file inside it,
// which is typically the actual ROM. Returns the MD5 hex digest and uncompressed size.
func computeMD5Zip(zipPath string) (string, int64, error) {
	r, err := zip.OpenReader(zipPath)
	if err != nil {
		return "", 0, fmt.Errorf("open zip: %w", err)
	}
	defer r.Close()

	// Find the largest file in the archive (the ROM).
	var largest *zip.File
	for _, f := range r.File {
		if f.FileInfo().IsDir() {
			continue
		}
		if largest == nil || f.UncompressedSize64 > largest.UncompressedSize64 {
			largest = f
		}
	}
	if largest == nil {
		return "", 0, fmt.Errorf("zip archive is empty")
	}

	rc, err := largest.Open()
	if err != nil {
		return "", 0, fmt.Errorf("open zip entry %s: %w", largest.Name, err)
	}
	defer rc.Close()

	h := md5.New()
	if _, err := io.Copy(h, rc); err != nil {
		return "", 0, fmt.Errorf("hashing zip entry: %w", err)
	}

	log.Printf("computeMD5Zip: hashed %s inside %s (size=%d)", largest.Name, filepath.Base(zipPath), largest.UncompressedSize64)
	return hex.EncodeToString(h.Sum(nil)), int64(largest.UncompressedSize64), nil
}

// findPrimaryROMFile returns the path to the primary disc image file within a game folder.
// For CUE folders: parses the .cue to find the first data track binary.
// For multi-disc: picks the first .bin, .iso, .chd, or .img file found.
func findPrimaryROMFile(folderPath string, isCue bool) (string, error) {
	entries, err := os.ReadDir(folderPath)
	if err != nil {
		return "", err
	}

	if isCue {
		// Look for a .cue file and parse it to find the first binary file.
		for _, e := range entries {
			if strings.HasSuffix(strings.ToLower(e.Name()), ".cue") {
				cueData, err := os.ReadFile(filepath.Join(folderPath, e.Name()))
				if err != nil {
					continue
				}
				for _, line := range strings.Split(string(cueData), "\n") {
					line = strings.TrimSpace(line)
					if strings.HasPrefix(strings.ToUpper(line), "FILE ") {
						// FILE "filename.bin" BINARY
						parts := strings.Fields(line)
						if len(parts) >= 2 {
							fname := strings.Trim(parts[1], `"`)
							candidate := filepath.Join(folderPath, fname)
							if _, err := os.Stat(candidate); err == nil {
								return candidate, nil
							}
						}
					}
				}
			}
		}
	}

	// Fall back: find first disc image by extension priority.
	prio := []string{".chd", ".iso", ".bin", ".img"}
	for _, ext := range prio {
		for _, e := range entries {
			if strings.HasSuffix(strings.ToLower(e.Name()), ext) {
				return filepath.Join(folderPath, e.Name()), nil
			}
		}
	}
	return "", nil
}

// ── Visibility helpers ───────────────────────────────────────

// isHidden returns true for entries that NextUI hides by default:
// dot-prefixed files/dirs, items ending with ".disabled", and map.txt.
func isHidden(name string) bool {
	return strings.HasPrefix(name, ".") ||
		strings.HasSuffix(name, ".disabled") ||
		name == "map.txt"
}

// isMacDotfile returns true for dot-prefixed names that have no (TAG) suffix —
// i.e. Mac/system artifacts rather than user-created hidden console dirs.
func isMacDotfile(name string) bool {
	if !strings.HasPrefix(name, ".") {
		return false
	}
	return extractTag(name) == ""
}

// ── String utilities ─────────────────────────────────────────

func isShortcutFolder(folderPath string) bool {
	name := filepath.Base(folderPath)
	if strings.HasPrefix(name, "\uFEFF") || strings.HasPrefix(name, "\u2605 ") {
		return true
	}
	_, err := os.Stat(filepath.Join(folderPath, shortcutMarkerFile))
	return err == nil
}

func extractTag(name string) string {
	idx := strings.LastIndex(name, "(")
	if idx < 0 {
		return ""
	}
	end := strings.LastIndex(name, ")")
	if end <= idx {
		return ""
	}
	return strings.TrimSpace(name[idx+1 : end])
}

func extractDisplayName(name string) string {
	idx := strings.LastIndex(name, "(")
	if idx < 0 {
		return name
	}
	return strings.TrimSpace(name[:idx])
}

func stripExtension(name string) string {
	ext := filepath.Ext(name)
	if len(ext) >= 2 && len(ext) <= 5 {
		return strings.TrimSuffix(name, ext)
	}
	return name
}

func readShortcutMarker(folderPath string) string {
	data, err := os.ReadFile(filepath.Join(folderPath, shortcutMarkerFile))
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(data))
}

// ── App settings ─────────────────────────────────────────────

// MediaTypeDef maps a display name to a ScreenScraper API media type string.
type MediaTypeDef struct {
	Display string
	Value   string
}

// AllMediaTypes lists every supported ScreenScraper media type.
var AllMediaTypes = []MediaTypeDef{
	{"Screenshot (title)", "sstitle"},
	{"Screenshot (in-game)", "ss"},
	{"Fan art", "fanart"},
	{"Wheel", "wheel"},
	{"Wheel (carbon)", "wheel-carbon"},
	{"Wheel (steel)", "wheel-steel"},
	{"Box art (2D)", "box-2D"},
	{"Box art (3D)", "box-3D"},
	{"Mix Recalbox V1", "mixrbv1"},
	{"Mix Recalbox V2", "mixrbv2"},
}

// DefaultArtworkPriority is the default media type fallback order.
var DefaultArtworkPriority = []string{"box-2D", "box-3D", "mixrbv1", "ss"}

// AppSettings holds persistent user preferences.
type AppSettings struct {
	SSUsername  string   `json:"ss_username"`
	SSPassword  string   `json:"ss_password"`
	ArtworkType string   `json:"artwork_type,omitempty"` // DEPRECATED: migrated to ArtworkPrio
	ArtworkPrio []string `json:"artwork_priority"`       // priority-ordered media type fallback list
	Region      string   `json:"region,omitempty"`       // DEPRECATED: migrated to RegionPrio
	RegionPrio  []string `json:"region_priority"`        // priority-ordered region fallback list
	ShowHidden  bool     `json:"show_hidden"`            // include .disabled and dot-prefix folders/ROMs
}

// ArtworkTypes returns the priority-ordered list of ALL media types.
// The user's saved order comes first; any missing types are appended at the end.
// Handles migration from the old single ArtworkType field.
func (s AppSettings) ArtworkTypes() []string {
	var base []string
	if len(s.ArtworkPrio) > 0 {
		base = s.ArtworkPrio
	} else if s.ArtworkType != "" {
		base = []string{s.ArtworkType}
	} else {
		base = DefaultArtworkPriority
	}

	// Ensure every known media type is present.
	seen := map[string]bool{}
	var full []string
	for _, v := range base {
		if !seen[v] {
			seen[v] = true
			full = append(full, v)
		}
	}
	for _, mt := range AllMediaTypes {
		if !seen[mt.Value] {
			seen[mt.Value] = true
			full = append(full, mt.Value)
		}
	}
	return full
}

// MediaTypeDisplay returns the display name for an API media type value.
func MediaTypeDisplay(value string) string {
	for _, mt := range AllMediaTypes {
		if mt.Value == value {
			return mt.Display
		}
	}
	return value
}

// RegionDef maps a display name to a ScreenScraper API region code.
type RegionDef struct {
	Display string
	Value   string
}

// AllRegions lists every user-selectable ScreenScraper region.
// "cus" (custom/homebrew) is omitted here — it is appended automatically by
// RegionTypes() as a last-resort catch-all and is not user-orderable.
var AllRegions = []RegionDef{
	{"World", "wor"},
	{"USA", "us"},
	{"Europe", "eu"},
	{"Japan", "jp"},
	{"France", "fr"},
	{"Germany", "de"},
	{"Spain", "es"},
	{"Italy", "it"},
	{"Portugal", "pt"},
	{"ScreenScraper", "ss"},
}

// DefaultRegionPriority is the default region fallback order.
var DefaultRegionPriority = []string{"us", "eu", "jp", "wor"}

// RegionTypes returns the priority-ordered list of ALL regions.
// The user's saved order comes first; any missing regions are appended.
// "cus" is always appended last as an automatic catch-all.
func (s AppSettings) RegionTypes() []string {
	var base []string
	if len(s.RegionPrio) > 0 {
		base = s.RegionPrio
	} else if s.Region != "" {
		base = []string{s.Region}
	} else {
		base = DefaultRegionPriority
	}

	seen := map[string]bool{}
	var full []string
	for _, v := range base {
		if !seen[v] {
			seen[v] = true
			full = append(full, v)
		}
	}
	for _, r := range AllRegions {
		if !seen[r.Value] {
			seen[r.Value] = true
			full = append(full, r.Value)
		}
	}
	if !seen["cus"] {
		full = append(full, "cus")
	}
	return full
}

// RegionDisplay returns the display name for a region code.
func RegionDisplay(value string) string {
	for _, r := range AllRegions {
		if r.Value == value {
			return r.Display
		}
	}
	return value
}

func defaultSettings() AppSettings {
	return AppSettings{
		ArtworkPrio: DefaultArtworkPriority,
		RegionPrio:  DefaultRegionPriority,
	}
}

func getSettingsPath() string {
	return filepath.Join(getSDCardPath(), ".userdata", "shared", "ScrapeGoat", "settings.json")
}

func loadSettings() AppSettings {
	defaults := defaultSettings()
	data, err := os.ReadFile(getSettingsPath())
	if err != nil {
		return defaults
	}
	var s AppSettings
	if err := json.Unmarshal(data, &s); err != nil {
		log.Printf("loadSettings: parse error: %v", err)
		return defaults
	}
	// Migrate old single artwork_type → priority list.
	if len(s.ArtworkPrio) == 0 {
		if s.ArtworkType != "" {
			s.ArtworkPrio = []string{s.ArtworkType}
			s.ArtworkType = ""
		} else {
			s.ArtworkPrio = DefaultArtworkPriority
		}
	}
	// Migrate old single region string → priority list.
	if len(s.RegionPrio) == 0 {
		if s.Region != "" {
			s.RegionPrio = []string{s.Region}
		} else {
			s.RegionPrio = DefaultRegionPriority
		}
	}
	s.Region = "" // clear deprecated field after migration
	return s
}

func saveSettings(s AppSettings) error {
	path := getSettingsPath()
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return fmt.Errorf("creating settings dir: %w", err)
	}
	data, err := json.Marshal(s)
	if err != nil {
		return fmt.Errorf("marshalling settings: %w", err)
	}
	return os.WriteFile(path, data, 0644)
}

func getLogPath() string {
	return filepath.Join(getSDCardPath(), ".userdata", string(platform), "logs", "scrapegoat.log")
}

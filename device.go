package main

import (
	"archive/zip"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"image"
	"image/png"
	"io"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"

	xdraw "golang.org/x/image/draw"
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
	Name    string // e.g. "Game Boy Advance (GBA)"
	Tag     string // e.g. "GBA"
	Path    string // full path to the directory
	Display string // display name without tag suffix
}

// ROMFile represents a ROM file or game folder within a console directory.
type ROMFile struct {
	Name        string // filename or folder name
	Path        string // full path
	Display     string // display name (no extension for files; folder name for multi-disc/cue)
	IsMultiDisc bool   // subdirectory containing {name}.m3u
	IsCueFolder bool   // subdirectory containing {name}.cue
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
func scanConsoleDirs() ([]ConsoleDir, error) {
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

		// Skip shortcut folders (have a .shortcut marker or invisible prefix)
		if isShortcutFolder(fullPath) {
			continue
		}
		// Skip hidden/system files
		if strings.HasPrefix(name, ".") || name == "map.txt" {
			continue
		}
		// Skip .disabled suffix folders (console dirs themselves, not ROMs)
		if strings.HasSuffix(name, ".disabled") {
			continue
		}

		tag := extractTag(name)
		if tag == "" {
			continue
		}
		consoles = append(consoles, ConsoleDir{
			Name:    name,
			Tag:     tag,
			Path:    fullPath,
			Display: extractDisplayName(name),
		})
	}

	sort.Slice(consoles, func(i, j int) bool {
		return strings.ToLower(consoles[i].Display) < strings.ToLower(consoles[j].Display)
	})
	log.Printf("scanConsoleDirs: found %d console folders", len(consoles))
	return consoles, nil
}

// scanROMs returns all ROM files in a console directory.
func scanROMs(consoleDir string) ([]ROMFile, error) {
	entries, err := os.ReadDir(consoleDir)
	if err != nil {
		return nil, fmt.Errorf("reading rom dir: %w", err)
	}

	var roms []ROMFile
	for _, e := range entries {
		name := e.Name()
		// Skip hidden/system files
		if strings.HasPrefix(name, ".") || name == "map.txt" {
			continue
		}

		if e.IsDir() {
			dirPath := filepath.Join(consoleDir, name)
			if _, err := os.Stat(filepath.Join(dirPath, name+".m3u")); err == nil {
				roms = append(roms, ROMFile{
					Name:        name,
					Path:        dirPath,
					Display:     name,
					IsMultiDisc: true,
				})
			} else if _, err := os.Stat(filepath.Join(dirPath, name+".cue")); err == nil {
				roms = append(roms, ROMFile{
					Name:        name,
					Path:        dirPath,
					Display:     name,
					IsCueFolder: true,
				})
			}
			continue
		}
		roms = append(roms, ROMFile{
			Name:    name,
			Path:    filepath.Join(consoleDir, name),
			Display: stripExtension(name),
		})
	}

	sort.Slice(roms, func(i, j int) bool {
		return strings.ToLower(roms[i].Display) < strings.ToLower(roms[j].Display)
	})
	log.Printf("scanROMs: dir=%s roms=%d", consoleDir, len(roms))
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

// updateShortcutBg regenerates the bg.png for a shortcut folder using the provided source art.
func updateShortcutBg(shortcutPath, artSrcPath string, settings AppSettings) {
	useGlobalBg, forceBlack := settings.artworkBgParams()
	generateArtworkBg(artSrcPath, shortcutPath, useGlobalBg, forceBlack)
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

// ── Image compositing ────────────────────────────────────────
// Ported verbatim from nextui-shortcuts-pak/device.go

// generateArtworkBg composites a fullscreen bg.png for a shortcut's .media/ folder.
func generateArtworkBg(artSrcPath, destFolder string, useGlobalBg, forceBlack bool) {
	var artImg image.Image
	if _, err := os.Stat(artSrcPath); err == nil {
		img, err := loadPNGImage(artSrcPath)
		if err != nil {
			log.Printf("generateArtworkBg: load art: %v", err)
			return
		}
		artImg = img
	} else if !forceBlack {
		return
	}

	screenW, screenH := screenDimensions()
	canvas := image.NewNRGBA(image.Rect(0, 0, screenW, screenH))

	pix := canvas.Pix
	for i := 0; i < len(pix); i += 4 {
		pix[i], pix[i+1], pix[i+2], pix[i+3] = 0x00, 0x00, 0x00, 0xff
	}

	if useGlobalBg {
		bgPath := globalBgPath()
		if bgImg, err := loadPNGImage(bgPath); err == nil {
			srcW, srcH := bgImg.Bounds().Dx(), bgImg.Bounds().Dy()
			scaleX := float64(screenW) / float64(srcW)
			scaleY := float64(screenH) / float64(srcH)
			scale := scaleX
			if scaleY > scaleX {
				scale = scaleY
			}
			newW := int(float64(srcW) * scale)
			newH := int(float64(srcH) * scale)
			scaledBg := image.NewNRGBA(image.Rect(0, 0, newW, newH))
			xdraw.BiLinear.Scale(scaledBg, scaledBg.Bounds(), bgImg, bgImg.Bounds(), xdraw.Src, nil)
			offX := (newW - screenW) / 2
			offY := (newH - screenH) / 2
			xdraw.Draw(canvas, canvas.Bounds(), scaledBg, image.Point{offX, offY}, xdraw.Src)
		}
	}

	if artImg != nil {
		maxW := int(float64(screenW) * 0.45)
		maxH := int(float64(screenH) * 0.60)
		artW, artH := thumbnailFit(artImg.Bounds().Dx(), artImg.Bounds().Dy(), maxW, maxH)
		scaledArt := image.NewNRGBA(image.Rect(0, 0, artW, artH))
		xdraw.BiLinear.Scale(scaledArt, scaledArt.Bounds(), artImg, artImg.Bounds(), xdraw.Over, nil)
		applyRoundedCorners(scaledArt, 40)

		const rightMargin = 30
		targetX := max(0, screenW-artW-rightMargin)
		centerY := screenH/2 - artH/2
		artDst := image.Rect(targetX, centerY, targetX+artW, centerY+artH)
		xdraw.Draw(canvas, artDst, scaledArt, image.Point{}, xdraw.Over)
	}

	mediaDir := filepath.Join(destFolder, ".media")
	if err := os.MkdirAll(mediaDir, 0755); err != nil {
		log.Printf("generateArtworkBg: mkdir .media: %v", err)
		return
	}
	f, err := os.Create(filepath.Join(mediaDir, "bg.png"))
	if err != nil {
		log.Printf("generateArtworkBg: create bg.png: %v", err)
		return
	}
	defer f.Close()
	if err := png.Encode(f, canvas); err != nil {
		log.Printf("generateArtworkBg: encode: %v", err)
	}
	log.Printf("generateArtworkBg: wrote %s/.media/bg.png (%dx%d)", destFolder, screenW, screenH)
}

func screenDimensions() (int, int) {
	if isBrick {
		return 1024, 768
	}
	return 1280, 720
}

func globalBgPath() string {
	return filepath.Join(getSDCardPath(), "bg.png")
}

func loadPNGImage(path string) (image.Image, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return png.Decode(f)
}

func applyRoundedCorners(img *image.NRGBA, radius int) {
	b := img.Bounds()
	w, h := b.Dx(), b.Dy()
	if radius <= 0 || w == 0 || h == 0 {
		return
	}
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			dx := 0
			if x < radius {
				dx = radius - x
			} else if x >= w-radius {
				dx = x - (w - radius - 1)
			}
			dy := 0
			if y < radius {
				dy = radius - y
			} else if y >= h-radius {
				dy = y - (h - radius - 1)
			}
			if dx*dx+dy*dy > radius*radius {
				off := img.PixOffset(x, y)
				img.Pix[off], img.Pix[off+1], img.Pix[off+2], img.Pix[off+3] = 0, 0, 0, 0
			}
		}
	}
}

func thumbnailFit(srcW, srcH, maxW, maxH int) (int, int) {
	if srcW == 0 || srcH == 0 {
		return maxW, maxH
	}
	newW, newH := maxW, srcH*maxW/srcW
	if newH > maxH {
		newH = maxH
		newW = srcW * maxH / srcH
	}
	return newW, newH
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

const (
	ArtworkModeBlack     = 0
	ArtworkModeWallpaper = 1
	ArtworkModeFallback  = 2
)

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
	Region      string   `json:"region"`                 // "us", "eu", "jp", "wor"
	ArtworkMode int      `json:"artwork_mode"`           // see ArtworkMode* constants
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

func (s AppSettings) artworkBgParams() (useGlobalBg, forceBlack bool) {
	switch s.ArtworkMode {
	case ArtworkModeWallpaper:
		return true, true
	case ArtworkModeFallback:
		return true, false
	default:
		return false, true
	}
}

func defaultSettings() AppSettings {
	return AppSettings{
		ArtworkPrio: DefaultArtworkPriority,
		Region:      "us",
		ArtworkMode: ArtworkModeWallpaper,
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
	if s.Region == "" {
		s.Region = "us"
	}
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

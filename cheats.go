package main

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unicode"

	"github.com/BrandonKowalski/certifiable"
	uatomic "go.uber.org/atomic"
)

// cheats.go — Git-based Libretro cheat file manager.
//
// Uses git sparse-checkout to fetch only the cheat directories needed from
// the libretro-database repository. On embedded platforms (tg5040/tg5050),
// a bundled git binary is used; on macOS, the system git is used.
//
// On first run, performs a shallow sparse clone. On subsequent runs, pulls
// the latest changes. Only directories matching systems present on the
// device are checked out, minimizing disk usage.
//
// Source: https://github.com/libretro/libretro-database/tree/master/cht/

const (
	cheatRepoURL    = "https://github.com/libretro/libretro-database.git"
	cheatRepoBranch = "master"
)

// ── Git binary resolution ────────────────────────────────────

// getGitBin returns the path to the git binary.
// On macOS, uses the system git. On embedded platforms, uses the bundled binary.
func getGitBin() string {
	if platform == PlatformMac {
		// Prefer the macOS system git explicitly.
		if _, err := os.Stat("/usr/bin/git"); err == nil {
			return "/usr/bin/git"
		}
		return "git"
	}
	execPath, err := os.Executable()
	if err != nil {
		return "git"
	}
	return filepath.Join(filepath.Dir(execPath), "resources", "bin", "git")
}

// checkGitAvailable verifies that git binary exists and is executable.
func checkGitAvailable() error {
	gitBin := getGitBin()
	resolvedGitBin := gitBin
	if !strings.ContainsRune(gitBin, os.PathSeparator) {
		pathGit, err := exec.LookPath(gitBin)
		if err != nil {
			return fmt.Errorf("git binary not found in PATH: %s", gitBin)
		}
		resolvedGitBin = pathGit
	}
	log.Printf("cheats: git binary: %s (resolved: %s)", gitBin, resolvedGitBin)

	info, err := os.Stat(resolvedGitBin)
	if err != nil {
		return fmt.Errorf("git binary not found: %s", resolvedGitBin)
	}
	if info.IsDir() {
		return fmt.Errorf("git path is a directory, not a binary: %s", resolvedGitBin)
	}

	// Try running git --version to verify it works.
	cmd := exec.Command(resolvedGitBin, "--version")
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("git --version failed: %w\noutput: %s", err, string(out))
	}
	log.Printf("cheats: git version check passed: %s", strings.TrimSpace(string(out)))
	return nil
}

// ── CA certificate bundle ────────────────────────────────────

var (
	caCertPath string
	caCertOnce sync.Once
)

// ensureCACertFile writes the embedded CA certificate bundle to a file on disk
// so that the bundled git binary can verify HTTPS connections.
func ensureCACertFile() string {
	caCertOnce.Do(func() {
		certs := certifiable.CACerts()
		if len(certs) == 0 {
			log.Printf("cheats: warning: embedded CA certs are empty")
			return
		}
		// Write next to the cheat repo so it survives across launches.
		certDir := filepath.Join(filepath.Dir(getCheatRepoPath()), "certs")
		if err := os.MkdirAll(certDir, 0755); err != nil {
			log.Printf("cheats: mkdir certs: %v", err)
			return
		}
		p := filepath.Join(certDir, "ca-certificates.crt")
		if err := os.WriteFile(p, certs, 0644); err != nil {
			log.Printf("cheats: write certs: %v", err)
			return
		}
		caCertPath = p
		log.Printf("cheats: CA cert bundle written to %s (%d bytes)", p, len(certs))
	})
	return caCertPath
}

// ── Git command helpers ──────────────────────────────────────

// gitCmd creates an exec.Cmd for git with the proper binary and environment.
func gitCmd(args ...string) *exec.Cmd {
	cmd := exec.Command(getGitBin(), args...)
	if platform != PlatformMac {
		execPath, _ := os.Executable()
		pakDir := filepath.Dir(execPath)
		libDir := filepath.Join(pakDir, "resources", "lib")
		binDir := filepath.Join(pakDir, "resources", "bin")
		env := os.Environ()
		env = append(env,
			"LD_LIBRARY_PATH="+libDir+":"+os.Getenv("LD_LIBRARY_PATH"),
			"GIT_EXEC_PATH="+binDir,
			"GIT_TEMPLATE_DIR=", // suppress "templates not found" warning
		)
		// Point git at our bundled CA certs so HTTPS works.
		if p := ensureCACertFile(); p != "" {
			env = append(env, "GIT_SSL_CAINFO="+p)
		}
		cmd.Env = env
	}
	return cmd
}

// gitCmdInRepo creates a git command that runs inside the cheat repo.
func gitCmdInRepo(args ...string) *exec.Cmd {
	fullArgs := append([]string{"-C", getCheatRepoPath()}, args...)
	return gitCmd(fullArgs...)
}

// runGit executes a git command and returns combined output.
func runGit(cmd *exec.Cmd) (string, error) {
	log.Printf("cheats: exec %s %v", cmd.Path, cmd.Args[1:])

	out, err := cmd.CombinedOutput()
	outStr := string(out)

	if err != nil {
		log.Printf("cheats: git error (exit code %v)", err)
		log.Printf("cheats: git output:\n%s", outStr)
		return outStr, err
	}

	if outStr != "" {
		log.Printf("cheats: git output:\n%s", outStr)
	}
	return outStr, nil
}

// splitCRLF is a bufio.SplitFunc that splits on \r, \n, or \r\n.
// This is needed because git writes progress lines using \r to overwrite
// the current terminal line rather than emitting a new \n-terminated line.
func splitCRLF(data []byte, atEOF bool) (advance int, token []byte, err error) {
	if atEOF && len(data) == 0 {
		return 0, nil, nil
	}
	for i, b := range data {
		if b == '\r' || b == '\n' {
			end := i + 1
			if b == '\r' && end < len(data) && data[end] == '\n' {
				end++
			}
			return end, data[:i], nil
		}
	}
	if atEOF {
		return len(data), data, nil
	}
	return 0, nil, nil
}

// gitProgressRe matches git's "Receiving objects: N%" progress lines.
var gitProgressRe = regexp.MustCompile(`Receiving objects:\s+(\d+)%`)

// parseGitProgressLine extracts a [0,1] progress value from a git output line.
func parseGitProgressLine(line string) (float64, bool) {
	m := gitProgressRe.FindStringSubmatch(line)
	if m == nil {
		return 0, false
	}
	n, _ := strconv.Atoi(m[1])
	return float64(n) / 100.0, true
}

// removeGitLocks removes stale git lock files that a previously killed git
// process may have left behind. Called before every git command.
func removeGitLocks() {
	repoPath := getCheatRepoPath()
	locks := []string{
		filepath.Join(repoPath, ".git", "index.lock"),
		filepath.Join(repoPath, ".git", "info", "sparse-checkout.lock"),
	}
	for _, lock := range locks {
		if err := os.Remove(lock); err == nil {
			log.Printf("cheats: removed stale lock file: %s", lock)
		}
	}
}

// runGitStreaming runs a git command with real-time progress and interrupt support.
// It reads stderr line-by-line (splitting on \r or \n) and calls setProgress
// whenever a "Receiving objects: N%" line is detected.
// If interruptSignal becomes non-zero the git process is killed within ~16ms.
// Returns context.Canceled when killed by the interrupt signal.
func runGitStreaming(cmd *exec.Cmd, interruptSignal *uatomic.Int32, setProgress func(float64)) (string, error) {
	removeGitLocks()
	log.Printf("cheats: exec %s %v", cmd.Path, cmd.Args[1:])

	stderrPipe, err := cmd.StderrPipe()
	if err != nil {
		return "", fmt.Errorf("stderr pipe: %w", err)
	}
	var stdoutBuf strings.Builder
	cmd.Stdout = &stdoutBuf

	if err := cmd.Start(); err != nil {
		return "", fmt.Errorf("start: %w", err)
	}

	// Interrupt watcher: kills the process within ~16ms of the signal being set.
	done := make(chan struct{})
	var killed int32
	go func() {
		ticker := time.NewTicker(16 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				if interruptSignal != nil && interruptSignal.Load() != 0 {
					atomic.StoreInt32(&killed, 1)
					if cmd.Process != nil {
						_ = cmd.Process.Kill()
					}
				}
			case <-done:
				return
			}
		}
	}()

	// Stream stderr, parse progress lines.
	var stderrLines []string
	scanner := bufio.NewScanner(stderrPipe)
	scanner.Split(splitCRLF)
	for scanner.Scan() {
		line := scanner.Text()
		if line != "" {
			stderrLines = append(stderrLines, line)
		}
		if p, ok := parseGitProgressLine(line); ok && setProgress != nil {
			setProgress(p)
		}
	}

	waitErr := cmd.Wait()
	close(done)

	combined := stdoutBuf.String()
	if len(stderrLines) > 0 {
		combined += strings.Join(stderrLines, "\n")
	}

	if atomic.LoadInt32(&killed) != 0 {
		return combined, context.Canceled
	}
	if waitErr != nil {
		log.Printf("cheats: git error: %v\noutput: %s", waitErr, combined)
		return combined, waitErr
	}
	if combined != "" {
		log.Printf("cheats: git output:\n%s", combined)
	}
	return combined, nil
}

// ── Sparse checkout management ───────────────────────────────

// isRepoInitialized checks if the cheat repo has been cloned.
func isRepoInitialized() bool {
	gitDir := filepath.Join(getCheatRepoPath(), ".git")
	info, err := os.Stat(gitDir)
	return err == nil && info.IsDir()
}

// initCheatRepo performs the initial shallow sparse clone.
func initCheatRepo(interruptSignal *uatomic.Int32, setMessage func(string), setProgress func(float64)) error {
	repoPath := getCheatRepoPath()
	if err := os.MkdirAll(filepath.Dir(repoPath), 0755); err != nil {
		return fmt.Errorf("mkdir: %w", err)
	}

	// Verify git is available before attempting clone.
	if err := checkGitAvailable(); err != nil {
		return fmt.Errorf("git check failed: %w", err)
	}

	setMessage("Cloning cheat database...")
	log.Printf("cheats: initializing sparse clone at %s", repoPath)

	cmd := gitCmd("clone",
		"--sparse",
		"--filter=blob:none",
		"--depth=1",
		"--branch", cheatRepoBranch,
		"--single-branch",
		"--no-tags",
		"--progress", // force progress output even when not a tty
		cheatRepoURL,
		repoPath,
	)

	if _, err := runGitStreaming(cmd, interruptSignal, setProgress); err != nil {
		return fmt.Errorf("clone failed: %w", err)
	}
	return nil
}

// ensureSystemCheckedOut makes sure a specific system's cheat directory
// is included in the sparse checkout.
func ensureSystemCheckedOut(libretroDir string, interruptSignal *uatomic.Int32, setMessage func(string), setProgress func(float64)) error {
	localDir := filepath.Join(getCheatRepoPath(), "cht", libretroDir)
	if info, err := os.Stat(localDir); err == nil && info.IsDir() {
		return nil // already checked out
	}

	chtPath := "cht/" + libretroDir
	setMessage(fmt.Sprintf("Checking out %s...", libretroDir))
	log.Printf("cheats: sparse-checkout add %s", chtPath)

	cmd := gitCmdInRepo("sparse-checkout", "add", chtPath)
	if _, err := runGitStreaming(cmd, interruptSignal, setProgress); err != nil {
		return err
	}
	return nil
}

// updateCheatRepo pulls the latest changes.
func updateCheatRepo(interruptSignal *uatomic.Int32, setMessage func(string), setProgress func(float64)) error {
	setMessage("Updating cheat database...")
	log.Printf("cheats: pulling latest changes")

	cmd := gitCmdInRepo("pull", "--ff-only", "--progress")
	out, err := runGitStreaming(cmd, interruptSignal, setProgress)
	if err != nil {
		return err
	}

	log.Printf("cheats: pull: %s", strings.TrimSpace(out))
	return nil
}

// ── Cheat matching ───────────────────────────────────────────

// cheatCandidate represents a single .cht file with its extracted region metadata.
type cheatCandidate struct {
	Path    string
	Regions []string // normalized SS region codes: "us", "eu", "jp", "wor", etc.
}

// CheatList maps normalized game name → list of cheat file candidates.
type CheatList map[string][]cheatCandidate

var parenthetical = regexp.MustCompile(`\s*\([^)]*\)`)

// cheatRegionMap maps region keywords found in cheat/ROM filenames to
// ScreenScraper region codes. Only full words and 2-letter abbreviations
// are included to avoid false positives from tags like (Code Breaker).
var cheatRegionMap = map[string]string{
	// Full names (as used in Libretro cheat database)
	"usa":       "us",
	"europe":    "eu",
	"japan":     "jp",
	"world":     "wor",
	"france":    "fr",
	"germany":   "de",
	"spain":     "es",
	"italy":     "it",
	"portugal":  "pt",
	"australia": "eu",
	"korea":     "kr",
	"china":     "cn",
	"taiwan":    "tw",
	"brazil":    "pt",
	// Common 2-letter abbreviations (from ROM naming conventions)
	"us": "us",
	"eu": "eu",
	"jp": "jp",
	"fr": "fr",
	"de": "de",
	"es": "es",
	"it": "it",
	"pt": "pt",
	"kr": "kr",
	"cn": "cn",
	"tw": "tw",
}

// extractCheatRegions parses parenthetical groups from a filename and returns
// any recognized region codes (normalized to ScreenScraper codes).
// Example: "Pokemon - Emerald Version (USA, Europe) (Code Breaker)"
//
//	→ ["us", "eu"]
func extractCheatRegions(name string) []string {
	matches := parenthetical.FindAllString(name, -1)
	var regions []string
	seen := map[string]bool{}
	for _, m := range matches {
		inner := strings.Trim(m, " ()")
		for _, part := range strings.Split(inner, ",") {
			part = strings.TrimSpace(part)
			if code, ok := cheatRegionMap[strings.ToLower(part)]; ok {
				if !seen[code] {
					seen[code] = true
					regions = append(regions, code)
				}
			}
		}
	}
	return regions
}

// normalizeCheatName normalizes a game name for fuzzy matching:
//  1. Strip parenthetical groups: (USA), (Europe), (Rev 1), (Code Breaker), etc.
//  2. Lowercase
//  3. Strip punctuation
//  4. Collapse whitespace
func normalizeCheatName(name string) string {
	name = parenthetical.ReplaceAllString(name, "")
	name = strings.ToLower(name)
	name = strings.Map(func(r rune) rune {
		if unicode.IsPunct(r) {
			return -1
		}
		return r
	}, name)
	name = strings.Join(strings.Fields(name), " ")
	return strings.TrimSpace(name)
}

// BuildCheatListFromRepo scans the local checkout for .cht files
// and builds a CheatList for matching. Multiple cheat files with the
// same normalized name (but different regions) are stored as candidates.
func BuildCheatListFromRepo(libretroDir string) (CheatList, error) {
	chtDir := filepath.Join(getCheatRepoPath(), "cht", libretroDir)

	entries, err := os.ReadDir(chtDir)
	if err != nil {
		return nil, fmt.Errorf("read cheat dir: %w", err)
	}

	list := make(CheatList, len(entries))
	var fileCount int
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(strings.ToLower(entry.Name()), ".cht") {
			continue
		}
		gameName := strings.TrimSuffix(entry.Name(), filepath.Ext(entry.Name()))
		normalized := normalizeCheatName(gameName)
		regions := extractCheatRegions(gameName)
		list[normalized] = append(list[normalized], cheatCandidate{
			Path:    filepath.Join(chtDir, entry.Name()),
			Regions: regions,
		})
		fileCount++
	}
	log.Printf("cheats: indexed %d files into %d unique names for %s", fileCount, len(list), libretroDir)
	return list, nil
}

// MatchCheat finds the best matching cheat file path for a ROM display name.
// When multiple cheat files match the same normalized name, the candidate
// whose region tags best match the ROM's regions (or the user's region
// priority) is preferred.
func MatchCheat(romDisplayName string, list CheatList, regionPrio []string) (string, bool) {
	normalized := normalizeCheatName(romDisplayName)
	candidates, ok := list[normalized]
	if !ok || len(candidates) == 0 {
		return "", false
	}
	if len(candidates) == 1 {
		return candidates[0].Path, true
	}

	// Multiple candidates — pick the best by region.
	romRegions := extractCheatRegions(romDisplayName)
	best := pickBestCheatCandidate(candidates, romRegions, regionPrio)
	log.Printf("cheats: %d candidates for %q, picked %s (romRegions=%v)",
		len(candidates), romDisplayName, filepath.Base(best), romRegions)
	return best, true
}

// pickBestCheatCandidate selects the best cheat file from multiple candidates
// based on region overlap with the ROM and the user's region priority.
func pickBestCheatCandidate(candidates []cheatCandidate, romRegions, regionPrio []string) string {
	bestScore := -1
	bestPath := candidates[0].Path

	for _, c := range candidates {
		score := scoreCheatRegionMatch(c.Regions, romRegions, regionPrio)
		if score > bestScore {
			bestScore = score
			bestPath = c.Path
		}
	}
	return bestPath
}

// scoreCheatRegionMatch scores how well a cheat file's regions match:
//
//	1000+ = direct region overlap with ROM (best)
//	 500  = cheat is "World" (universal)
//	 100  = cheat matches user's region priority (decreasing by position)
//	  50  = cheat has no region info (neutral fallback)
//	   0  = no match at all
func scoreCheatRegionMatch(cheatRegions, romRegions, regionPrio []string) int {
	// Highest: direct region overlap with ROM.
	if len(romRegions) > 0 && len(cheatRegions) > 0 {
		overlap := 0
		for _, cr := range cheatRegions {
			for _, rr := range romRegions {
				if cr == rr {
					overlap++
				}
			}
		}
		if overlap > 0 {
			return 1000 + overlap
		}
	}

	// Medium: "World" cheat is universal.
	for _, r := range cheatRegions {
		if r == "wor" {
			return 500
		}
	}

	// Low: matches user's configured region priority.
	if len(cheatRegions) > 0 && len(regionPrio) > 0 {
		for i, prio := range regionPrio {
			for _, r := range cheatRegions {
				if r == prio {
					return 100 - i
				}
			}
		}
	}

	// Neutral: no region info at all (some cheats have no region tag).
	if len(cheatRegions) == 0 {
		return 50
	}

	return 0
}

// ── Console cheat downloader ─────────────────────────────────

// downloadCheatsForConsole downloads cheats for all ROMs in a console directory.
// regionPrio is the user's region priority (ScreenScraper codes) used to pick
// the best cheat file when multiple region variants exist.
func downloadCheatsForConsole(console ConsoleDir, regionPrio []string, interruptSignal *uatomic.Int32, setProgress func(float64), setMessage func(string)) (ScrapeSummary, error) {
	libretroDir, ok := LibretroSystemDirs[console.Tag]
	if !ok {
		return ScrapeSummary{}, fmt.Errorf("no Libretro cheat directory for system %s", console.Tag)
	}

	setProgress(0.0)

	// scaleProgress maps a sub-range [offset, offset+scale] onto the overall [0,1] bar.
	scaleProgress := func(scale, offset float64) func(float64) {
		return func(p float64) { setProgress(offset + p*scale) }
	}

	// Initialize or update the git repo (progress [0.0, 0.3]).
	if !isRepoInitialized() {
		if err := initCheatRepo(interruptSignal, setMessage, scaleProgress(0.3, 0.0)); err != nil {
			if errors.Is(err, context.Canceled) {
				return ScrapeSummary{}, nil
			}
			return ScrapeSummary{}, fmt.Errorf("init cheat repo: %w", err)
		}
	} else {
		if err := updateCheatRepo(interruptSignal, setMessage, scaleProgress(0.3, 0.0)); err != nil {
			if errors.Is(err, context.Canceled) {
				return ScrapeSummary{}, nil
			}
			log.Printf("cheats: pull failed (will use existing data): %v", err)
		}
	}
	setProgress(0.3)

	// Ensure this system's cheats are checked out (progress [0.3, 0.5]).
	if err := ensureSystemCheckedOut(libretroDir, interruptSignal, setMessage, scaleProgress(0.2, 0.3)); err != nil {
		if errors.Is(err, context.Canceled) {
			return ScrapeSummary{}, nil
		}
		return ScrapeSummary{}, fmt.Errorf("checkout %s: %w", libretroDir, err)
	}
	setProgress(0.5)

	// Build cheat list from local files.
	setMessage("Building cheat list...")
	cheatList, err := BuildCheatListFromRepo(libretroDir)
	if err != nil {
		return ScrapeSummary{}, fmt.Errorf("build cheat list: %w", err)
	}
	if len(cheatList) == 0 {
		return ScrapeSummary{}, fmt.Errorf("no cheats available for %s (%s)", console.Tag, libretroDir)
	}
	log.Printf("cheats: %d cheats available for %s", len(cheatList), libretroDir)

	// Scan ROMs.
	roms, err := scanROMs(console.Path, false)
	if err != nil {
		return ScrapeSummary{}, fmt.Errorf("scan roms: %w", err)
	}

	var summary ScrapeSummary
	summary.Total = len(roms)
	cheatsDir := filepath.Join(getCheatsPath(), console.Tag)

	for i, rom := range roms {
		if interruptSignal != nil && interruptSignal.Load() != 0 {
			log.Printf("cheats: interrupted by user after %d/%d ROMs", i, len(roms))
			break
		}
		// ROM loop progress [0.5, 1.0].
		setProgress(0.5 + float64(i)/float64(len(roms))*0.5)
		setMessage(fmt.Sprintf("(%d/%d) %s", i+1, len(roms), rom.Display))

		srcPath, found := MatchCheat(rom.Display, cheatList, regionPrio)
		if !found {
			log.Printf("cheats: no match for %s", rom.Display)
			summary.NotFound++
			continue
		}

		destPath := filepath.Join(cheatsDir, rom.Display+".cht")

		// Skip if already exists.
		if _, err := os.Stat(destPath); err == nil {
			log.Printf("cheats: skip %s (exists)", rom.Display)
			summary.Found++
			continue
		}

		// Copy the .cht file from the local checkout.
		if err := copyCheatFile(srcPath, destPath); err != nil {
			log.Printf("cheats: copy error for %s: %v", rom.Display, err)
			summary.Errors++
			continue
		}
		summary.Found++
		log.Printf("cheats: %s → %s", rom.Display, destPath)
	}

	setProgress(1.0)
	log.Printf("cheats: done. total=%d found=%d notFound=%d errors=%d",
		summary.Total, summary.Found, summary.NotFound, summary.Errors)
	return summary, nil
}

// copyCheatFile copies a file from src to dst, creating parent directories as needed.
func copyCheatFile(src, dst string) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}

	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}

	if _, err = io.Copy(out, in); err != nil {
		out.Close()
		return err
	}
	if err := out.Sync(); err != nil {
		out.Close()
		return err
	}
	return out.Close()
}

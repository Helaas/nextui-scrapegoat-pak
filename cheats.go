package main

import (
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"unicode"

	"github.com/BrandonKowalski/certifiable"
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
	log.Printf("cheats: git binary: %s", gitBin)

	info, err := os.Stat(gitBin)
	if err != nil {
		return fmt.Errorf("git binary not found: %s", gitBin)
	}
	if info.IsDir() {
		return fmt.Errorf("git path is a directory, not a binary: %s", gitBin)
	}

	// Try running git --version to verify it works.
	cmd := exec.Command(gitBin, "--version")
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

// ── Sparse checkout management ───────────────────────────────

// isRepoInitialized checks if the cheat repo has been cloned.
func isRepoInitialized() bool {
	gitDir := filepath.Join(getCheatRepoPath(), ".git")
	info, err := os.Stat(gitDir)
	return err == nil && info.IsDir()
}

// initCheatRepo performs the initial shallow sparse clone.
func initCheatRepo(setMessage func(string)) error {
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
		cheatRepoURL,
		repoPath,
	)

	if _, err := runGit(cmd); err != nil {
		return fmt.Errorf("clone failed: %w", err)
	}
	return nil
}

// ensureSystemCheckedOut makes sure a specific system's cheat directory
// is included in the sparse checkout.
func ensureSystemCheckedOut(libretroDir string, setMessage func(string)) error {
	localDir := filepath.Join(getCheatRepoPath(), "cht", libretroDir)
	if info, err := os.Stat(localDir); err == nil && info.IsDir() {
		return nil // already checked out
	}

	chtPath := "cht/" + libretroDir
	setMessage(fmt.Sprintf("Checking out %s...", libretroDir))
	log.Printf("cheats: sparse-checkout add %s", chtPath)

	cmd := gitCmdInRepo("sparse-checkout", "add", chtPath)
	if _, err := runGit(cmd); err != nil {
		return err
	}
	return nil
}

// updateCheatRepo pulls the latest changes.
func updateCheatRepo(setMessage func(string)) error {
	setMessage("Updating cheat database...")
	log.Printf("cheats: pulling latest changes")

	cmd := gitCmdInRepo("pull", "--ff-only")
	out, err := runGit(cmd)
	if err != nil {
		return err
	}

	log.Printf("cheats: pull: %s", strings.TrimSpace(out))
	return nil
}

// ── Cheat matching ───────────────────────────────────────────

// CheatList maps normalized game name → local .cht file path.
type CheatList map[string]string

var parenthetical = regexp.MustCompile(`\s*\([^)]*\)`)

// normalizeCheatName normalizes a game name for fuzzy matching:
//  1. Strip parenthetical groups: (USA), (Europe), (Rev 1), etc.
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
// and builds a CheatList for matching.
func BuildCheatListFromRepo(libretroDir string) (CheatList, error) {
	chtDir := filepath.Join(getCheatRepoPath(), "cht", libretroDir)

	entries, err := os.ReadDir(chtDir)
	if err != nil {
		return nil, fmt.Errorf("read cheat dir: %w", err)
	}

	list := make(CheatList, len(entries))
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(strings.ToLower(entry.Name()), ".cht") {
			continue
		}
		gameName := strings.TrimSuffix(entry.Name(), filepath.Ext(entry.Name()))
		normalized := normalizeCheatName(gameName)
		list[normalized] = filepath.Join(chtDir, entry.Name())
	}
	return list, nil
}

// MatchCheat finds the best matching cheat file path for a ROM display name.
func MatchCheat(romDisplayName string, list CheatList) (string, bool) {
	normalized := normalizeCheatName(romDisplayName)
	if path, ok := list[normalized]; ok {
		return path, true
	}
	return "", false
}

// ── Console cheat downloader ─────────────────────────────────

// downloadCheatsForConsole downloads cheats for all ROMs in a console directory.
func downloadCheatsForConsole(console ConsoleDir, setProgress func(float64), setMessage func(string)) (ScrapeSummary, error) {
	libretroDir, ok := LibretroSystemDirs[console.Tag]
	if !ok {
		return ScrapeSummary{}, fmt.Errorf("no Libretro cheat directory for system %s", console.Tag)
	}

	setProgress(0.0)

	// Initialize or update the git repo.
	if !isRepoInitialized() {
		if err := initCheatRepo(setMessage); err != nil {
			return ScrapeSummary{}, fmt.Errorf("init cheat repo: %w", err)
		}
	} else {
		if err := updateCheatRepo(setMessage); err != nil {
			log.Printf("cheats: pull failed (will use existing data): %v", err)
		}
	}

	// Ensure this system's cheats are checked out.
	if err := ensureSystemCheckedOut(libretroDir, setMessage); err != nil {
		return ScrapeSummary{}, fmt.Errorf("checkout %s: %w", libretroDir, err)
	}

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
	roms, err := scanROMs(console.Path)
	if err != nil {
		return ScrapeSummary{}, fmt.Errorf("scan roms: %w", err)
	}

	var summary ScrapeSummary
	summary.Total = len(roms)
	cheatsDir := filepath.Join(getCheatsPath(), console.Tag)

	for i, rom := range roms {
		setProgress(float64(i) / float64(len(roms)))
		setMessage(fmt.Sprintf("(%d/%d) %s", i+1, len(roms), rom.Display))

		srcPath, found := MatchCheat(rom.Display, cheatList)
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
	defer out.Close()

	_, err = io.Copy(out, in)
	return err
}

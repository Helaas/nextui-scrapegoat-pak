package main

import (
	"errors"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	_ "github.com/BrandonKowalski/certifiable"
	gaba "github.com/BrandonKowalski/gabagool/v2/pkg/gabagool"
)

// Platform represents the target device.
type Platform string

const (
	PlatformMac    Platform = "mac"
	PlatformTG5040 Platform = "tg5040"
	PlatformTG5050 Platform = "tg5050"
)

var platform Platform

func main() {
	platform = PlatformTG5040
	platformEnv := strings.ToUpper(os.Getenv("PLATFORM"))
	if strings.Contains(platformEnv, "TG5050") {
		platform = PlatformTG5050
	} else if strings.Contains(platformEnv, "TG5040") || strings.Contains(platformEnv, "TG3040") {
		platform = PlatformTG5040
	} else if platformEnv == "" && runtime.GOOS == "darwin" {
		platform = PlatformMac
	}

	// On macOS, set ENVIRONMENT=DEV so Gabagool skips power-button handling
	// (avoids negative WaitGroup panic in closeWindow).
	if platform == PlatformMac {
		os.Setenv("ENVIRONMENT", "DEV")
	}

	log.SetFlags(log.LstdFlags | log.Lmicroseconds)
	log.SetPrefix("scrapegoat: ")

	logPath := getLogPath()

	// Write log output to both stderr and the log file so we can inspect
	// git errors (and everything else) on the device after a run.
	if err := os.MkdirAll(filepath.Dir(logPath), 0755); err == nil {
		if lf, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644); err == nil {
			log.SetOutput(io.MultiWriter(os.Stderr, lf))
			// lf intentionally not closed — lives for the process lifetime.
		}
	}

	log.Printf("startup: platform=%s device=%s logPath=%s", platform, os.Getenv("DEVICE"), logPath)

	// Verify ScreenScraper dev credentials were embedded at build time
	if err := checkDevCredentials(); err != nil {
		log.Printf("warning: %v", err)
	}

	gaba.Init(gaba.Options{
		WindowTitle:    "ScrapeGoat",
		ShowBackground: true,
		LogPath:        logPath,
		IsNextUI:       platform != PlatformMac,
	})
	defer gaba.Close()

	runApp()
}

func runApp() {
	for {
		action := showMainMenu()
		switch action {
		case mainActionScrapeArtwork:
			scrapeArtworkFlow()
		case mainActionDownloadCheats:
			downloadCheatsFlow()
		case mainActionSettings:
			showSettingsScreen()
		case mainActionQuit:
			return
		}
	}
}

// isErrCancelled checks if the error is a Gabagool user-cancelled error.
func isErrCancelled(err error) bool {
	return errors.Is(err, gaba.ErrCancelled)
}

// logError logs a non-nil, non-cancelled error.
func logError(context string, err error) {
	if err != nil && !isErrCancelled(err) {
		log.Printf("%s: %v", context, err)
	}
}

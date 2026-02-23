package main

import (
	"fmt"
	"log"

	gaba "github.com/BrandonKowalski/gabagool/v2/pkg/gabagool"
	"github.com/BrandonKowalski/gabagool/v2/pkg/gabagool/constants"
	"go.uber.org/atomic"
)

// ── Main menu ────────────────────────────────────────────────

type mainAction int

const (
	mainActionQuit mainAction = iota
	mainActionScrapeArtwork
	mainActionDownloadCheats
	mainActionSettings
)

func showMainMenu() mainAction {
	items := []gaba.MenuItem{
		{Text: "Scrape Artwork"},
		{Text: "Download Cheats"},
		{Text: "Settings"},
	}

	opts := gaba.DefaultListOptions("ScrapeGoat", items)
	opts.FooterHelpItems = []gaba.FooterHelpItem{
		{ButtonName: "B", HelpText: "Quit"},
		{ButtonName: "A", HelpText: "Select"},
	}

	result, err := gaba.List(opts)
	if isErrCancelled(err) {
		return mainActionQuit
	}
	if err != nil {
		logError("main menu", err)
		return mainActionQuit
	}
	if len(result.Selected) == 0 {
		return mainActionQuit
	}

	switch result.Selected[0] {
	case 0:
		log.Printf("ui: main menu -> scrape artwork")
		return mainActionScrapeArtwork
	case 1:
		log.Printf("ui: main menu -> download cheats")
		return mainActionDownloadCheats
	case 2:
		log.Printf("ui: main menu -> settings")
		return mainActionSettings
	default:
		return mainActionQuit
	}
}

// ── Scrape Artwork flow ──────────────────────────────────────

func scrapeArtworkFlow() {
	// Step 1: Load settings and warn if user credentials not set
	settings := loadSettings()
	if settings.SSUsername == "" {
		showWarning("No ScreenScraper.fr user credentials set.\n\nScraping will proceed at basic rate (~1 req/min, single-threaded).\n\nFor much faster speeds, go to Settings and add your username and password.")
	}

	// Step 2: Pick a console (only show systems with a ScreenScraper ID)
	console, ok := pickConsoleForScraping(settings)
	if !ok {
		return
	}

	// Step 3: Pick scrape mode
	missingOnly, ok := pickScrapeMode()
	if !ok {
		return
	}

	// Step 4: Scrape with progress bar and live stats HUD
	var progress atomic.Float64
	progress.Store(0.0)

	var interruptSignal atomic.Int32
	var dynMsg atomic.String

	modeDesc := "all ROMs"
	if missingOnly {
		modeDesc = "ROMs without artwork"
	}
	title := fmt.Sprintf("Scraping %s (%s)...", console.Display, modeDesc)
	dynMsg.Store(title) // shown immediately while the first ROM is being fetched

	summary, err := gaba.ProcessMessage(title,
		gaba.ProcessMessageOptions{
			ShowThemeBackground: true,
			ShowProgressBar:     true,
			Progress:            &progress,
			DynamicMessage:      &dynMsg,
			InterruptButton:     constants.VirtualButtonY,
			InterruptSignal:     &interruptSignal,
			FooterHelpItems: []gaba.FooterHelpItem{
				{ButtonName: "Y", HelpText: "Stop & Install"},
			},
		},
		func() (ScrapeSummary, error) {
			return scrapeConsole(console, missingOnly, settings,
				&interruptSignal,
				func(p float64) { progress.Store(p) },
				func(msg string) {
					dynMsg.Store(msg)
					log.Printf("scrape: %s", msg)
				},
			)
		},
	)

	if err != nil && !isErrCancelled(err) {
		logError("scrape artwork", err)
		showError(fmt.Sprintf("Scraping failed:\n\n%s", err.Error()))
		return
	}

	// Step 5: Show summary
	showScrapeSummary(summary)
}

// pickConsoleForScraping shows only systems that have a ScreenScraper platform ID.
func pickConsoleForScraping(settings AppSettings) (ConsoleDir, bool) {
	consoles, err := scanConsoleDirs(settings.ShowHidden)
	if err != nil {
		logError("scanning consoles", err)
		showError("Could not read ROM folders.")
		return ConsoleDir{}, false
	}

	// Filter to systems we can scrape.
	var scrapable []ConsoleDir
	for _, c := range consoles {
		if _, ok := SSPlatformIDs[c.Tag]; ok {
			scrapable = append(scrapable, c)
		}
	}

	if len(scrapable) == 0 {
		showError("No supported systems found.\n\nNo ROM folders with a known system tag\nwere found.")
		return ConsoleDir{}, false
	}

	items := make([]gaba.MenuItem, len(scrapable))
	for i, c := range scrapable {
		items[i] = gaba.MenuItem{Text: c.Display}
	}

	opts := gaba.DefaultListOptions("Select System", items)
	opts.FooterHelpItems = []gaba.FooterHelpItem{
		{ButtonName: "B", HelpText: "Back"},
		{ButtonName: "A", HelpText: "Select"},
	}

	result, err := gaba.List(opts)
	if isErrCancelled(err) {
		return ConsoleDir{}, false
	}
	if err != nil || len(result.Selected) == 0 {
		return ConsoleDir{}, false
	}

	return scrapable[result.Selected[0]], true
}

// pickScrapeMode asks the user whether to scrape all ROMs or only those without artwork.
// Returns (missingOnly, ok).
func pickScrapeMode() (bool, bool) {
	items := []gaba.MenuItem{
		{Text: "Scrape missing only  (skip ROMs with existing artwork)"},
		{Text: "Scrape all  (overwrite existing artwork)"},
	}

	opts := gaba.DefaultListOptions("Scrape Mode", items)
	opts.FooterHelpItems = []gaba.FooterHelpItem{
		{ButtonName: "B", HelpText: "Back"},
		{ButtonName: "A", HelpText: "Select"},
	}

	result, err := gaba.List(opts)
	if isErrCancelled(err) {
		return false, false
	}
	if err != nil || len(result.Selected) == 0 {
		return false, false
	}

	return result.Selected[0] == 0, true
}

func showScrapeSummary(s ScrapeSummary) {
	msg := fmt.Sprintf(
		"Scraping complete!\n\nTotal:     %d\nFound:     %d\nNot found: %d\nErrors:    %d",
		s.Total, s.Found, s.NotFound, s.Errors,
	)
	gaba.ConfirmationMessage(msg,
		[]gaba.FooterHelpItem{
			{ButtonName: "A", HelpText: "OK", IsConfirmButton: true},
		},
		gaba.MessageOptions{ConfirmButton: constants.VirtualButtonA},
	)
}

// ── Download Cheats flow ─────────────────────────────────────

func downloadCheatsFlow() {
	settings := loadSettings()

	// Step 1: Pick a console (only show systems with libretro cheats)
	console, ok := pickConsoleForCheats(settings)
	if !ok {
		return
	}

	// Step 2: Download with progress bar
	var progress atomic.Float64
	progress.Store(0.0)

	var interruptSignal atomic.Int32

	summary, err := gaba.ProcessMessage(
		fmt.Sprintf("Downloading cheats for %s...", console.Display),
		gaba.ProcessMessageOptions{
			ShowThemeBackground: true,
			ShowProgressBar:     true,
			Progress:            &progress,
			InterruptButton:     constants.VirtualButtonY,
			InterruptSignal:     &interruptSignal,
			FooterHelpItems: []gaba.FooterHelpItem{
				{ButtonName: "Y", HelpText: "Stop"},
			},
		},
		func() (ScrapeSummary, error) {
			return downloadCheatsForConsole(console, &interruptSignal,
				func(p float64) { progress.Store(p) },
				func(msg string) { log.Printf("cheats: %s", msg) },
			)
		},
	)

	if err != nil && !isErrCancelled(err) {
		logError("download cheats", err)
		showError(fmt.Sprintf("Cheat download failed:\n\n%s", err.Error()))
		return
	}

	showCheatSummary(summary)
}

// pickConsoleForCheats shows only systems that have a libretro cheat directory.
func pickConsoleForCheats(settings AppSettings) (ConsoleDir, bool) {
	consoles, err := scanConsoleDirs(settings.ShowHidden)
	if err != nil {
		logError("scanning consoles", err)
		showError("Could not read ROM folders.")
		return ConsoleDir{}, false
	}

	var supported []ConsoleDir
	for _, c := range consoles {
		if _, ok := LibretroSystemDirs[c.Tag]; ok {
			supported = append(supported, c)
		}
	}

	if len(supported) == 0 {
		showError("No supported systems found.\n\nNo ROM folders with a known system tag\nwere found.")
		return ConsoleDir{}, false
	}

	items := make([]gaba.MenuItem, len(supported))
	for i, c := range supported {
		items[i] = gaba.MenuItem{Text: c.Display}
	}

	opts := gaba.DefaultListOptions("Select System", items)
	opts.FooterHelpItems = []gaba.FooterHelpItem{
		{ButtonName: "B", HelpText: "Back"},
		{ButtonName: "A", HelpText: "Select"},
	}

	result, err := gaba.List(opts)
	if isErrCancelled(err) {
		return ConsoleDir{}, false
	}
	if err != nil || len(result.Selected) == 0 {
		return ConsoleDir{}, false
	}

	return supported[result.Selected[0]], true
}

func showCheatSummary(s ScrapeSummary) {
	msg := fmt.Sprintf(
		"Download complete!\n\nTotal:     %d\nDownloaded: %d\nNot found:  %d\nErrors:     %d",
		s.Total, s.Found, s.NotFound, s.Errors,
	)
	gaba.ConfirmationMessage(msg,
		[]gaba.FooterHelpItem{
			{ButtonName: "A", HelpText: "OK", IsConfirmButton: true},
		},
		gaba.MessageOptions{ConfirmButton: constants.VirtualButtonA},
	)
}

// ── Settings screen ──────────────────────────────────────────

func showSettingsScreen() {
	for {
		settings := loadSettings()

		// Show username with masked display
		usernameDisplay := "<not set>"
		if settings.SSUsername != "" {
			usernameDisplay = settings.SSUsername
		}
		passwordDisplay := "<not set>"
		if settings.SSPassword != "" {
			passwordDisplay = "<set>"
		}

		showHiddenDisplay := "Off"
		if settings.ShowHidden {
			showHiddenDisplay = "On"
		}

		items := []gaba.MenuItem{
			{Text: fmt.Sprintf("Username: %s", usernameDisplay)},
			{Text: fmt.Sprintf("Password: %s", passwordDisplay)},
			{Text: "Artwork Options..."},
			{Text: fmt.Sprintf("Include hidden/disabled: %s", showHiddenDisplay)},
		}

		opts := gaba.DefaultListOptions("Settings", items)
		opts.FooterHelpItems = []gaba.FooterHelpItem{
			{ButtonName: "B", HelpText: "Back"},
			{ButtonName: "A", HelpText: "Edit"},
		}

		result, err := gaba.List(opts)
		if isErrCancelled(err) {
			return
		}
		if err != nil || len(result.Selected) == 0 {
			return
		}

		switch result.Selected[0] {
		case 0:
			editUsername(&settings)
		case 1:
			editPassword(&settings)
		case 2:
			editArtworkOptions(&settings)
		case 3:
			settings.ShowHidden = !settings.ShowHidden
			logError("saving settings", saveSettings(settings))
		}
	}
}

func editUsername(settings *AppSettings) {
	res, err := gaba.Keyboard(settings.SSUsername, "Y: Cancel")
	if isErrCancelled(err) || res == nil {
		return
	}
	settings.SSUsername = res.Text
	logError("saving settings", saveSettings(*settings))
}

func editPassword(settings *AppSettings) {
	res, err := gaba.Keyboard(settings.SSPassword, "Y: Cancel")
	if isErrCancelled(err) || res == nil {
		return
	}
	settings.SSPassword = res.Text
	logError("saving settings", saveSettings(*settings))
}

func editArtworkOptions(settings *AppSettings) {
	for {
		// Build artwork priority summary (top 3 types).
		artSummary := ""
		for i, t := range settings.ArtworkTypes() {
			if i >= 3 {
				artSummary += " → …"
				break
			}
			if i > 0 {
				artSummary += " → "
			}
			artSummary += MediaTypeDisplay(t)
		}

		// Build region priority summary (top 3 regions).
		regionSummary := ""
		for i, r := range settings.RegionTypes() {
			if i >= 3 {
				regionSummary += " → …"
				break
			}
			if i > 0 {
				regionSummary += " → "
			}
			regionSummary += RegionDisplay(r)
		}

		items := []gaba.ItemWithOptions{
			{
				Item: gaba.MenuItem{Text: "Artwork priority"},
				Options: []gaba.Option{
					{DisplayName: artSummary, Value: "edit", Type: gaba.OptionTypeClickable},
				},
				SelectedOption: 0,
			},
			{
				Item: gaba.MenuItem{Text: "Region priority"},
				Options: []gaba.Option{
					{DisplayName: regionSummary, Value: "edit", Type: gaba.OptionTypeClickable},
				},
				SelectedOption: 0,
			},
		}

		listOpts := gaba.OptionListSettings{
			ConfirmButton: constants.VirtualButtonStart,
			FooterHelpItems: []gaba.FooterHelpItem{
				{ButtonName: "B", HelpText: "Back"},
				{ButtonName: "A", HelpText: "Edit"},
				{ButtonName: "START", HelpText: "Done"},
			},
		}

		result, err := gaba.OptionsList("Artwork Options", listOpts, items)
		if isErrCancelled(err) {
			return
		}
		if err != nil {
			logError("artwork options", err)
			return
		}
		if result == nil {
			return
		}

		if result.Action == gaba.ListActionSelected {
			switch result.Selected {
			case 0:
				editArtworkPriority(settings)
			case 1:
				editRegionPriority(settings)
			}
			continue // re-render to show updated summaries
		}

		// START pressed — all saves already handled inside the sub-editors.
		log.Printf("ui: artwork options done: prio=%v regionPrio=%v", settings.ArtworkPrio, settings.RegionPrio)
		return
	}
}

// editArtworkPriority shows a reorderable list of all media types.
// The order determines fallback priority when scraping artwork.
func editArtworkPriority(settings *AppSettings) {
	// Build the list: all media types in the user's current priority order.
	currentPrio := settings.ArtworkTypes()

	var menuItems []gaba.MenuItem
	for _, v := range currentPrio {
		menuItems = append(menuItems, gaba.MenuItem{
			Text:     MediaTypeDisplay(v),
			Metadata: v,
		})
	}

	opts := gaba.DefaultListOptions("Artwork Priority", menuItems)
	opts.ReorderButton = constants.VirtualButtonX
	opts.ActionButton = constants.VirtualButtonStart
	opts.FooterHelpItems = []gaba.FooterHelpItem{
		{ButtonName: "B", HelpText: "Cancel"},
		{ButtonName: "X", HelpText: "Reorder"},
		{ButtonName: "START", HelpText: "Save"},
	}

	result, err := gaba.List(opts)
	if isErrCancelled(err) {
		return
	}
	if err != nil {
		logError("artwork priority", err)
		return
	}
	if result == nil || (result.Action != gaba.ListActionTriggered && result.Action != gaba.ListActionConfirmed) {
		return // user cancelled
	}

	// Collect items in their new order.
	var newPrio []string
	for _, item := range result.Items {
		if v, ok := item.Metadata.(string); ok {
			newPrio = append(newPrio, v)
		}
	}

	settings.ArtworkPrio = newPrio
	log.Printf("ui: artwork priority updated: %v", newPrio)
	logError("saving settings", saveSettings(*settings))
}

// editRegionPriority shows a reorderable list of all regions.
// The order determines fallback priority when resolving media and game names.
// "cus" is excluded from the list (it is always appended automatically by RegionTypes).
func editRegionPriority(settings *AppSettings) {
	// Build the list: all user-selectable regions in current priority order.
	var menuItems []gaba.MenuItem
	for _, v := range settings.RegionTypes() {
		if v == "cus" {
			continue // automatic catch-all, not user-orderable
		}
		menuItems = append(menuItems, gaba.MenuItem{
			Text:     RegionDisplay(v),
			Metadata: v,
		})
	}

	opts := gaba.DefaultListOptions("Region Priority", menuItems)
	opts.ReorderButton = constants.VirtualButtonX
	opts.ActionButton = constants.VirtualButtonStart
	opts.FooterHelpItems = []gaba.FooterHelpItem{
		{ButtonName: "B", HelpText: "Cancel"},
		{ButtonName: "X", HelpText: "Reorder"},
		{ButtonName: "START", HelpText: "Save"},
	}

	result, err := gaba.List(opts)
	if isErrCancelled(err) {
		return
	}
	if err != nil {
		logError("region priority", err)
		return
	}
	if result == nil || (result.Action != gaba.ListActionTriggered && result.Action != gaba.ListActionConfirmed) {
		return // user cancelled
	}

	// Collect items in their new order.
	var newPrio []string
	for _, item := range result.Items {
		if v, ok := item.Metadata.(string); ok {
			newPrio = append(newPrio, v)
		}
	}

	settings.RegionPrio = newPrio
	log.Printf("ui: region priority updated: %v", newPrio)
	logError("saving settings", saveSettings(*settings))
}

// ── Utility ──────────────────────────────────────────────────

func showError(message string) {
	gaba.ConfirmationMessage(message,
		[]gaba.FooterHelpItem{
			{ButtonName: "B", HelpText: "Back"},
		},
		gaba.MessageOptions{},
	)
}

func showWarning(message string) {
	gaba.ConfirmationMessage(message,
		[]gaba.FooterHelpItem{
			{ButtonName: "A", HelpText: "Continue"},
		},
		gaba.MessageOptions{},
	)
}

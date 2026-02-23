package main

import (
	"encoding/json"
	"fmt"
	"image"
	"image/jpeg"
	"image/png"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// screenscraper.go — ScreenScraper.fr API client (JSON mode).
//
// API docs: https://www.screenscraper.fr/webapi2.php
// Endpoint: https://api.screenscraper.fr/api2/jeuInfos.php

const (
	ssAPIBase   = "https://api.screenscraper.fr/api2"
	ssSoftName  = "ScrapeGoat-v1.0.0"
	ssUserAgent = "ScrapeGoat/1.0.0"
	ssRateDelay = 500 * time.Millisecond // courtesy delay for single-threaded mode
)

// Build-time variables injected via -ldflags from .env.local.
var (
	ssDevID       string // Developer ID
	ssDevPwd      string // Developer password
	ssDebugPwd    string // Debug password (optional — enables debug mode, 100 uses/day)
	ssForceLevel  string // Force user level (optional — e.g. "30")
	ssForceUpdate string // Force cache update (optional — "1")
)

// isDebug returns true when debug mode is enabled at build time.
func isDebug() bool { return ssDebugPwd != "" }

// checkDevCredentials verifies that dev credentials were embedded at build time.
func checkDevCredentials() error {
	if ssDevID == "" || ssDevPwd == "" {
		return fmt.Errorf("developer credentials not embedded — rebuild with 'make'")
	}
	if isDebug() {
		log.Printf("[DEBUG] debug mode enabled (forceLevel=%s forceUpdate=%s)", ssForceLevel, ssForceUpdate)
	}
	return nil
}

// ── Client ───────────────────────────────────────────────────

// SSClient is a ScreenScraper.fr API client.
type SSClient struct {
	Username string
	Password string
	client   *http.Client
}

// newSSClient creates a new ScreenScraper client.
func newSSClient(username, password string) *SSClient {
	return &SSClient{
		Username: username,
		Password: password,
		client:   &http.Client{Timeout: 30 * time.Second},
	}
}

// ── JSON response types ──────────────────────────────────────

// SSResult holds the result of a successful ROM lookup.
type SSResult struct {
	GameName      string
	MediaURL      string
	MediaFormat   string // "png" or "jpg"
	RequestsToday int
	MaxRequests   int
	MaxThreads    int
}

type ssJSONResponse struct {
	Header   ssResponseHeader `json:"header"`
	Response ssResponseBody   `json:"response"`
}

type ssResponseHeader struct {
	Success string `json:"success"`
	Error   string `json:"error"`
}

type ssResponseBody struct {
	Serveurs ssServeurs `json:"serveurs"`
	SSUser   ssUser     `json:"ssuser"`
	Jeu      ssGame     `json:"jeu"`
}

type ssServeurs struct {
	CloseForNonMember string `json:"closefornomember"`
	CloseForLeecher   string `json:"closeforleecher"`
}

type ssUser struct {
	MaxThreads        string `json:"maxthreads"`
	RequestsToday     string `json:"requeststoday"`
	MaxRequestsPerDay string `json:"maxrequestsperday"`
}

type ssGame struct {
	ID     string    `json:"id"`
	Names  []ssName  `json:"noms"`
	Medias []ssMedia `json:"medias"`
}

type ssName struct {
	Region string `json:"region"`
	Text   string `json:"text"`
}

type ssMedia struct {
	Type   string `json:"type"`
	Region string `json:"region"`
	Format string `json:"format"`
	URL    string `json:"url"`
}

// ── ROM search ───────────────────────────────────────────────

// SearchROM looks up a ROM on ScreenScraper.fr.
// First attempts MD5 hash matching; falls back to filename-only if hash lookup returns no results.
// artworkTypes is a priority-ordered list of media types to try.
func (c *SSClient) SearchROM(rom ROMFile, systemID int, artworkTypes []string, region string) (*SSResult, error) {
	md5Hash, fileSize, err := computeMD5(rom)
	if err != nil {
		log.Printf("SearchROM: md5 failed for %s: %v — falling back to filename", rom.Name, err)
		md5Hash = ""
		fileSize = 0
	}

	result, err := c.searchROMRequest(rom.Name, md5Hash, fileSize, systemID, artworkTypes, region)
	if err != nil {
		return nil, err
	}
	if result != nil {
		return result, nil
	}

	// Hash lookup returned no results — retry without hash.
	if md5Hash != "" {
		log.Printf("SearchROM: hash lookup returned no results for %s — retrying with filename only", rom.Name)
		result, err = c.searchROMRequest(rom.Name, "", 0, systemID, artworkTypes, region)
		if err != nil {
			return nil, err
		}
	}
	return result, nil
}

func (c *SSClient) searchROMRequest(romName, md5Hash string, fileSize int64, systemID int, artworkTypes []string, region string) (*SSResult, error) {
	params := url.Values{}
	params.Set("devid", ssDevID)
	params.Set("devpassword", ssDevPwd)
	params.Set("softname", ssSoftName)
	params.Set("output", "json")
	params.Set("romnom", romName)
	params.Set("systemeid", strconv.Itoa(systemID))

	if c.Username != "" {
		params.Set("ssid", c.Username)
		params.Set("sspassword", c.Password)
	}
	if md5Hash != "" {
		params.Set("md5", md5Hash)
		params.Set("romtaille", strconv.FormatInt(fileSize, 10))
	}

	// Debug mode parameters.
	if isDebug() {
		params.Set("devdebugpassword", ssDebugPwd)
		if ssForceLevel != "" {
			params.Set("forcelevel", ssForceLevel)
		}
		if ssForceUpdate != "" {
			params.Set("forceupdate", ssForceUpdate)
		}
	}

	reqURL := ssAPIBase + "/jeuInfos.php?" + params.Encode()
	log.Printf("searchROMRequest: GET %s", sanitizeLogURL(reqURL))

	req, err := http.NewRequest("GET", reqURL, nil)
	if err != nil {
		return nil, fmt.Errorf("build request: %w", err)
	}
	req.Header.Set("User-Agent", ssUserAgent)

	resp, err := c.client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("http get: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body: %w", err)
	}

	if isDebug() {
		log.Printf("[DEBUG] response: status=%d size=%d", resp.StatusCode, len(body))
	}

	// Handle error status codes.
	if err := checkHTTPStatus(resp.StatusCode); err != nil {
		return nil, err
	}

	var data ssJSONResponse
	if err := json.Unmarshal(body, &data); err != nil {
		if isDebug() {
			log.Printf("[DEBUG] parse error, body preview: %s", truncateStr(string(body), 500))
		}
		return nil, fmt.Errorf("parse json: %w", err)
	}

	if isDebug() {
		u := data.Response.SSUser
		s := data.Response.Serveurs
		log.Printf("[DEBUG] ssuser: maxthreads=%s requests=%s/%s",
			u.MaxThreads, u.RequestsToday, u.MaxRequestsPerDay)
		log.Printf("[DEBUG] serveurs: closefornomember=%s closeforleecher=%s",
			s.CloseForNonMember, s.CloseForLeecher)
	}

	game := data.Response.Jeu
	if game.ID == "" {
		return nil, nil // not found
	}

	mediaURL, mediaFormat := resolveMedia(game.Medias, artworkTypes, region)
	if mediaURL == "" {
		return nil, nil // found game but no matching media
	}

	requestsToday, _ := strconv.Atoi(data.Response.SSUser.RequestsToday)
	maxRequests, _ := strconv.Atoi(data.Response.SSUser.MaxRequestsPerDay)
	maxThreads, _ := strconv.Atoi(data.Response.SSUser.MaxThreads)

	return &SSResult{
		GameName:      resolveGameName(game.Names, region),
		MediaURL:      strings.TrimSpace(mediaURL),
		MediaFormat:   mediaFormat,
		RequestsToday: requestsToday,
		MaxRequests:   maxRequests,
		MaxThreads:    maxThreads,
	}, nil
}

// checkHTTPStatus returns a descriptive error for known ScreenScraper HTTP status codes.
func checkHTTPStatus(status int) error {
	switch status {
	case 200:
		return nil
	case 404:
		return nil // not found is handled by caller checking empty game
	case 429:
		return fmt.Errorf("thread limit reached (HTTP 429) — too many concurrent requests")
	case 430:
		return fmt.Errorf("daily quota exceeded (HTTP 430)")
	case 431:
		return fmt.Errorf("too many unrecognized ROMs (HTTP 431)")
	case 423:
		return fmt.Errorf("ScreenScraper API is temporarily closed (HTTP 423)")
	case 426:
		return fmt.Errorf("software has been blacklisted by ScreenScraper (HTTP 426)")
	default:
		if status >= 400 {
			return fmt.Errorf("unexpected status %d", status)
		}
		return nil
	}
}

// ── Media / name resolution ──────────────────────────────────

// resolveMedia finds the best media URL by trying each type in priority order.
// For each type, it tries the preferred region first, then falls back through
// standard regions. Returns the first match found across the type priority list.
func resolveMedia(medias []ssMedia, mediaTypes []string, preferredRegion string) (string, string) {
	regionFallback := []string{preferredRegion, "wor", "us", "eu", "jp", "cus"}
	seen := map[string]bool{}
	var regions []string
	for _, r := range regionFallback {
		if !seen[r] {
			seen[r] = true
			regions = append(regions, r)
		}
	}

	// Try each media type in priority order.
	for _, mediaType := range mediaTypes {
		var candidates []ssMedia
		for _, m := range medias {
			if m.Type == mediaType {
				candidates = append(candidates, m)
			}
		}
		if len(candidates) == 0 {
			continue // this type not available, try next
		}

		// Try preferred region first, then fallbacks.
		for _, region := range regions {
			for _, m := range candidates {
				if m.Region == region {
					log.Printf("resolveMedia: matched type=%s region=%s", mediaType, region)
					return m.URL, m.Format
				}
			}
		}

		// No region match — use first available for this type.
		log.Printf("resolveMedia: matched type=%s (no region match, using first)", mediaType)
		return candidates[0].URL, candidates[0].Format
	}

	log.Printf("resolveMedia: no match for types=%v", mediaTypes)
	return "", ""
}

// resolveGameName picks the best game name for the given region.
func resolveGameName(names []ssName, region string) string {
	for _, r := range []string{region, "wor", "us", "eu", "jp"} {
		for _, n := range names {
			if n.Region == r {
				return strings.TrimSpace(n.Text)
			}
		}
	}
	if len(names) > 0 {
		return strings.TrimSpace(names[0].Text)
	}
	return ""
}

// ── Media download ───────────────────────────────────────────

// DownloadMedia downloads a media URL and saves it as a PNG file at destPath.
// If the source is a JPEG it is decoded and re-encoded as PNG.
func (c *SSClient) DownloadMedia(mediaURL, destPath string) error {
	req, err := http.NewRequest("GET", mediaURL, nil)
	if err != nil {
		return fmt.Errorf("build request: %w", err)
	}
	req.Header.Set("User-Agent", ssUserAgent)

	resp, err := c.client.Do(req)
	if err != nil {
		return fmt.Errorf("http get: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return fmt.Errorf("unexpected status %d", resp.StatusCode)
	}

	contentType := resp.Header.Get("Content-Type")
	isJPEG := strings.Contains(contentType, "jpeg") || strings.HasSuffix(strings.ToLower(mediaURL), ".jpg")

	if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
		return fmt.Errorf("mkdir: %w", err)
	}

	if !isJPEG {
		f, err := os.Create(destPath)
		if err != nil {
			return fmt.Errorf("create file: %w", err)
		}
		defer f.Close()
		_, err = io.Copy(f, resp.Body)
		return err
	}

	// Decode JPEG and re-encode as PNG.
	img, err := jpeg.Decode(resp.Body)
	if err != nil {
		log.Printf("DownloadMedia: jpeg decode failed: %v", err)
		return fmt.Errorf("jpeg decode: %w", err)
	}
	f, err := os.Create(destPath)
	if err != nil {
		return fmt.Errorf("create file: %w", err)
	}
	defer f.Close()
	return png.Encode(f, img)
}

// ── Quota ────────────────────────────────────────────────────

// QuotaRemaining returns the remaining daily request quota.
func (r *SSResult) QuotaRemaining() int {
	if r == nil || r.MaxRequests == 0 {
		return -1
	}
	return r.MaxRequests - r.RequestsToday
}

// ── URL sanitization ─────────────────────────────────────────

// sanitizeLogURL strips credentials from a URL for safe logging.
func sanitizeLogURL(rawURL string) string {
	u, err := url.Parse(rawURL)
	if err != nil {
		return rawURL
	}
	q := u.Query()
	for _, key := range []string{"sspassword", "devpassword", "devdebugpassword"} {
		if q.Get(key) != "" {
			q.Set(key, "***")
		}
	}
	u.RawQuery = q.Encode()
	return u.String()
}

func truncateStr(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "..."
}

// ── Console scraping (multithreaded) ──────────────────────────

// scrapeConsole scrapes all ROMs in a console directory.
// Uses concurrent workers sized by the API's maxthreads response.
func scrapeConsole(console ConsoleDir, missingOnly bool, settings AppSettings, setProgress func(float64), setMessage func(string)) (ScrapeSummary, error) {
	roms, err := scanROMs(console.Path)
	if err != nil {
		return ScrapeSummary{}, fmt.Errorf("scan roms: %w", err)
	}

	systemID, ok := SSPlatformIDs[console.Tag]
	if !ok {
		return ScrapeSummary{}, fmt.Errorf("no ScreenScraper ID for system %s", console.Tag)
	}

	// Filter ROMs if missing-only mode.
	var toScrape []ROMFile
	var preExisting int
	for _, rom := range roms {
		if missingOnly && artworkExists(console.Name, rom.Display) {
			log.Printf("scrapeConsole: skip %s (artwork exists)", rom.Display)
			preExisting++
			continue
		}
		toScrape = append(toScrape, rom)
	}

	client := newSSClient(settings.SSUsername, settings.SSPassword)
	total := len(roms)
	var summary ScrapeSummary
	summary.Total = total
	summary.Found = preExisting // pre-existing artwork counts as found

	if len(toScrape) == 0 {
		setProgress(1.0)
		return summary, nil
	}

	// Phase 1: Scrape first ROM sequentially to learn maxthreads.
	maxThreads := 1
	firstResult := scrapeOneROM(client, toScrape[0], console, systemID, settings, &summary)
	if firstResult != nil && firstResult.MaxThreads > 1 {
		maxThreads = firstResult.MaxThreads
	}
	log.Printf("scrapeConsole: maxthreads=%d (from API)", maxThreads)

	var completed int64 = 1 // first ROM already done
	setProgress(float64(completed) / float64(total))

	if len(toScrape) == 1 {
		setProgress(1.0)
		log.Printf("scrapeConsole: done. total=%d found=%d notFound=%d errors=%d",
			summary.Total, summary.Found, summary.NotFound, summary.Errors)
		return summary, nil
	}

	// Phase 2: Scrape remaining ROMs with worker pool.
	sem := make(chan struct{}, maxThreads)
	var mu sync.Mutex
	var aborted int32 // atomic flag for early abort

	var wg sync.WaitGroup
	for _, rom := range toScrape[1:] {
		if atomic.LoadInt32(&aborted) != 0 {
			break
		}

		sem <- struct{}{} // acquire slot
		wg.Add(1)

		go func(rom ROMFile) {
			defer wg.Done()
			defer func() { <-sem }() // release slot

			if atomic.LoadInt32(&aborted) != 0 {
				return
			}

			var localSummary ScrapeSummary
			result := scrapeOneROM(client, rom, console, systemID, settings, &localSummary)

			mu.Lock()
			summary.Found += localSummary.Found
			summary.NotFound += localSummary.NotFound
			summary.Errors += localSummary.Errors
			if result != nil && result.MaxRequests > 0 && result.RequestsToday >= result.MaxRequests {
				atomic.StoreInt32(&aborted, 1)
				log.Printf("scrapeConsole: quota exceeded (%d/%d)", result.RequestsToday, result.MaxRequests)
			}
			mu.Unlock()

			n := atomic.AddInt64(&completed, 1)
			setProgress(float64(n) / float64(total))
		}(rom)
	}
	wg.Wait()

	setProgress(1.0)
	log.Printf("scrapeConsole: done. total=%d found=%d notFound=%d errors=%d",
		summary.Total, summary.Found, summary.NotFound, summary.Errors)
	return summary, nil
}

// scrapeOneROM searches for a single ROM and downloads its artwork.
// If summary is non-nil, updates counts under no lock (caller's responsibility).
// Returns the SSResult on success, nil on failure/not-found.
func scrapeOneROM(client *SSClient, rom ROMFile, console ConsoleDir, systemID int, settings AppSettings, summary *ScrapeSummary) *SSResult {
	result, err := client.SearchROM(rom, systemID, settings.ArtworkTypes(), settings.Region)
	if err != nil {
		if strings.Contains(err.Error(), "daily quota") || strings.Contains(err.Error(), "thread limit") {
			log.Printf("scrapeConsole: %v", err)
		} else {
			log.Printf("scrapeConsole: search error for %s: %v", rom.Name, err)
		}
		if summary != nil {
			summary.Errors++
		}
		return nil
	}
	if result == nil {
		log.Printf("scrapeConsole: not found: %s", rom.Name)
		if summary != nil {
			summary.NotFound++
		}
		return nil
	}

	destPath := artworkSrcPath(console.Name, rom.Display)
	if err := client.DownloadMedia(result.MediaURL, destPath); err != nil {
		log.Printf("scrapeConsole: download error for %s: %v", rom.Name, err)
		if summary != nil {
			summary.Errors++
		}
		return nil
	}

	if summary != nil {
		summary.Found++
	}
	log.Printf("scrapeConsole: saved artwork for %s → %s", rom.Display, destPath)

	// Regenerate bg.png for any matching shortcuts.
	shortcuts, err := findShortcutsForROM(console.Name, rom.Display)
	if err != nil {
		log.Printf("scrapeConsole: findShortcuts error: %v", err)
	}
	for _, scPath := range shortcuts {
		updateShortcutBg(scPath, destPath, settings)
	}

	return result
}

// Ensure image package decoders are registered.
var _ image.Image = (*image.NRGBA)(nil)

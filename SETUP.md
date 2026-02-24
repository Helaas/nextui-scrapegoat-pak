# ScrapeGoat PAK — Setup Guide

## Credentials & Environment Variables

This application requires credentials to authenticate with the ScreenScraper API. Credentials are **not stored in git** for security reasons. 

### Required Credentials

**ScreenScraper.fr Developer Credentials**
- `SCREENSCRAPER_DEV_ID`: Developer ID from screenscraper.fr
- `SCREENSCRAPER_DEV_PASSWORD`: Developer password from screenscraper.fr

Without these, API requests will have severe rate limits (~1 request per min).

**Optional User Credentials** (used for higher rate limits)
- Username and password are configured via the application's Settings screen
- Stored locally in `~/.userdata/shared/ScrapeGoat/settings.json`
- This file is never committed to git (ignored in `.gitignore`)

## Local Development Setup

### 1. Copy the Example Environment File

```bash
cp .env.example .env.local
```

### 2. Obtain Developer Credentials

1. Visit [screenscraper.fr](https://www.screenscraper.fr/)
2. Create an account
3. Go to [webapi2.php](https://www.screenscraper.fr/webapi2.php)
4. Request developer API access
5. You'll receive an email with:
   - Developer ID
   - Developer Password

### 3. Update `.env.local` with Real Credentials

Edit `.env.local` and replace the placeholders:

```bash
SCREENSCRAPER_DEV_ID=your_actual_dev_id
SCREENSCRAPER_DEV_PASSWORD=your_actual_dev_password
```

**Important**: `.env.local` is in `.gitignore` and will never be committed.

### 4. Load Credentials into Environment (Two Options)

**Option A: Use `.env` file (Recommended for Local Dev)**

For local development, the simplest approach is to set environment variables before running:

```bash
export SCREENSCRAPER_DEV_ID=$(grep SCREENSCRAPER_DEV_ID .env.local | cut -d '=' -f2)
export SCREENSCRAPER_DEV_PASSWORD=$(grep SCREENSCRAPER_DEV_PASSWORD .env.local | cut -d '=' -f2)
./build/scrapegoat
```

Or use a bash function in your shell profile:

```bash
alias scrapegoat-dev='export $(cat .env.local | xargs) && ./build/scrapegoat'
```

**Option B: Use System Environment Variables**

Set environment variables in your shell profile (`~/.zshrc`, `~/.bashrc`, etc.):

```bash
export SCREENSCRAPER_DEV_ID="your_dev_id"
export SCREENSCRAPER_DEV_PASSWORD="your_dev_password"
```

Then run the application normally:
```bash
./build/scrapegoat
```

### 5. Verify Setup

When you start the application, you should see in the logs:
- No warning about missing credentials, OR
- Only warnings about missing user credentials (which are optional)

If you see:
```
WARNING: SCREENSCRAPER_DEV_ID and/or SCREENSCRAPER_DEV_PASSWORD not set.
```

Then credentials aren't loaded yet. Check that:
1. `.env.local` contains real (non-placeholder) values
2. Environment variables are set before running the app
3. Variable names match exactly (case-sensitive)

## Deployment Setup

### On Embedded Devices (TG5040/TG5050)

For production deployment on NextUI hardware:

1. **Set environment variables** in your deployment script or system configuration
2. **Never include `.env` files** in the deployed package
3. **Never include `.env.local` files** in the repository

Example deployment script:

```bash
#!/bin/bash
# deployment.sh - Only for maintainers with real credentials

export SCREENSCRAPER_DEV_ID="<real_credential>"
export SCREENSCRAPER_DEV_PASSWORD="<real_credential>"

# Build the application
make clean build

# Package for deployment
# (your packaging commands here)
```

### Via CI/CD (GitHub Actions, etc.)

Store credentials as repository secrets:

1. Go to Settings → Secrets and variables → Actions
2. Add secrets:
   - `SCREENSCRAPER_DEV_ID`
   - `SCREENSCRAPER_DEV_PASSWORD`

3. Use in workflow:

```yaml
- name: Build with credentials
  env:
    SCREENSCRAPER_DEV_ID: ${{ secrets.SCREENSCRAPER_DEV_ID }}
    SCREENSCRAPER_DEV_PASSWORD: ${{ secrets.SCREENSCRAPER_DEV_PASSWORD }}
  run: make build
```

## Security Best Practices

1. **Never commit `.env.local` or real credentials to git**
   - `.gitignore` protects you, but be extra careful

2. **`.env.example` contains only placeholders**
   - This is committed and shows developers what variables they need
   - Always review `.env.example` before updating your `.env.local`

3. **Credentials are case-sensitive**
   - `SCREENSCRAPER_DEV_ID` ≠ `screenscraper_dev_id`

4. **Log sanitization**
   - The application automatically strips credentials from logged URLs
   - Check logs don't contain `sspassword` or `devpassword` values

5. **User credentials storage**
   - Settings stored at `~/.userdata/shared/ScrapeGoat/settings.json`
   - Stored as plaintext JSON (consider encryption for production)
   - This location is NOT tracked by git

## Troubleshooting

### "Missing required credentials"
- Check environment variables are set: `echo $SCREENSCRAPER_DEV_ID`
- Check `.env.local` has real values (not placeholders)
- Restart the application after setting env vars

### "API rate limit exceeded" or "Developer not authenticated"
- Credentials are not being passed correctly to the API
- Verify credentials are correct in `.env.local`
- Check the application startup logs for credential loading messages

### "Invalid username/password" (for user credentials)
- These are configured via the Settings screen, not environment variables
- Run the app and go to Settings → ScreenScraper Credentials
- Enter your screenscraper.fr user credentials there

## File Overview

| File | Purpose | Committed to Git? |
|------|---------|---|
| `.env.example` | Shows required env variables with placeholders | ✅ Yes |
| `.env.local` | Your actual credentials for local development | ❌ No (in .gitignore) |
| `.gitignore` | Tells git to ignore credentials files | ✅ Yes |
| `~/.userdata/shared/ScrapeGoat/settings.json` | User credentials & app settings | ❌ No (local user file) |

## See Also

- [.env.example](.env.example) — Environment variable reference
- [screenscraper.go](screenscraper.go#L33-L56) — Credential loading code
- [main.go](main.go#L41-L44) — Where credentials are loaded at startup

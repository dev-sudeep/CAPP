# CAPP — Compact App: A Simple App Bundle Utility

CAPP (Compact App) packages an application and its install/uninstall logic into a single `.capp` file (a ZIP archive). Users can install applications locally or from remote mirrors. The toolchain consists of a single C program with 13 subcommands.

---

## Features

- **Simple bundling**: Package entire applications into `.capp` files with install/uninstall scripts
- **Remote installation**: Install packages directly from GitHub mirrors with automatic latest version detection
- **Cross-platform**: Works on Linux, macOS, Windows, and Android Termux
- **Security**: All install/uninstall scripts are reviewed by users before execution
- **Version management**: Semantic version parsing and comparison for automatic latest version selection
- **Package metadata**: Bundles can ship a `metadata.json` with author, description, and version info
- **Instruction caching**: Instructions persist in `~/.capp/data/` for later viewing
- **No dependencies**: Uses only the standard C library + system tools (curl, zip, unzip)

---

## Bundle Structure

```
MyApp.capp  (ZIP archive containing:)
├── install.sh / install.bat     — runs on installation
├── uninstall.sh / uninstall.bat — runs on uninstallation
├── metadata.json                — package metadata (optional)
├── instructions.<ext>           — usage documentation (optional)
├── <main file>                  — source code, binary, etc.
└── <anything else>
```

> **Note:** `.sh` scripts are used on Linux and macOS; `.bat` scripts are used on Windows. The installer selects the correct variant automatically at compile time.

### metadata.json format

**Required fields**
- `name`
- `version`

**Optional fields**
- `author`
- `description`

**Custom fields**
Any additional string, number, boolean, or array fields are allowed. Arrays are displayed as comma-separated lists in `capp show`.

```json
{
  "name": "MyApp",
  "version": "1.2.0",
  "author": "Your Name",
  "description": "A short description of what the app does.",
  "homepage": "https://example.com",
  "tags": ["cli", "demo"]
}
```

If no `metadata.json` is provided in the bundle, CAPP generates a minimal one with the package name and version populated from the mirror's `packages.txt`.

---

## Subcommand Reference

| Subcommand | Arguments | Description |
|---|---|---|
| `create` | — | Interactively bundle a folder into a `.capp` file |
| `install` | `[-V\|--verbose] <App.capp>` | Install a bundle from a local file |
| `install-remote` | `[-V\|--verbose] [-v <version>] <AppName>` | Install the latest version by default, or a specific version with `-v` |
| `uninstall` | `[-V\|--verbose] <AppName>` | Uninstall a previously installed app |
| `update` | `[-V\|--verbose]` | Refresh `available.txt` from all mirrors, show upgradeable packages |
| `upgrade` | `[-V\|--verbose] [AppName]` | Upgrade one package, or all installed packages if no argument given |
| `list` | `[-V\--verbose]` | List all packages available on the configured mirrors |
| `list-installed` | `[-V\|--verbose]` | List all installed packages |
| `search` | `<query>` | Search available and installed packages by name or metadata |
| `show` | `<AppName>` | Show metadata and install status for any package |
| `man` | `[-V\|--verbose] <AppName>` | Open instructions for a package |
| `clear-cache` | — | Remove cached instructions files (preserves metadata) |

---

## Creating a Bundle

### Method 1 — Manual

1. Place all required files in a folder.
2. ZIP the folder and rename the archive to `AppName.capp`.

### Method 2 — Using the Bundler

```sh
capp create
```

Enter the path to the source folder and the application name when prompted. The bundler creates `AppName.capp` in the current directory.

---

## Installing a Bundle

### From a Local File

```sh
capp install MyApp.capp
```

Steps performed:

1. Verifies the package is not already installed (if it is, install is aborted)
2. Creates `~/.capp/bundles/`, `~/.capp/bin/`, and `~/.capp/data/` if needed
3. Extracts the `.capp` archive to a temporary directory
4. Displays the full contents of `install.sh` (or `install.bat` on Windows)
5. Prompts you to confirm before proceeding (`[y/N]`)
6. Runs the install script
7. Saves `metadata.json` to `~/.capp/data/AppName/`
8. Opens `instructions.<ext>` (if present) with your default viewer and caches it in `~/.capp/data/AppName/`
9. Moves the `.capp` bundle to `~/.capp/bundles/` for use by the uninstaller and `man` command
10. Registers the app in `~/.capp/bundles/installed.txt`
11. Cleans up the temporary extraction directory

### From a Mirror

```sh
capp install-remote MyApp
capp install-remote -v 1.2.0 MyApp
```

By default, CAPP resolves and installs the latest semantic version from the configured mirror. Pass `-v <version>` to install a specific version instead. `-V` / `--verbose` can be added to show detailed progress.
If the package is already installed, `install-remote` exits with an error instead of reinstalling.

---

## Uninstalling

```sh
capp uninstall MyApp
# or equivalently:
capp uninstall MyApp.capp
```

1. Confirms you want to uninstall (`[y/N]`)
2. Extracts the stored bundle from `~/.capp/bundles/`
3. Displays `uninstall.sh` / `uninstall.bat` and prompts for confirmation
4. Runs the uninstall script
5. Removes the stored bundle and cleans up temporary files
6. Removes the app from `installed.txt`

---

## Mirror Management

### update

```sh
capp update
```

Fetches `packages.txt` and `packages.json` from every configured mirror, merges the results (deduplicating by name and version) into `~/.capp/bundles/available.txt` and `~/.capp/bundles/packages.json`, then reports which installed packages have a newer version available on the mirror.

```
=== CAPP — Update ===

[capp] Fetching package list from: https://...

  MyApp                     1.0.0  →  2.0.0
  OtherApp                  3.1.0  (up to date)

[capp] Run 'capp upgrade' to upgrade all, or 'capp upgrade <AppName>' for one.
```

### upgrade

```sh
capp upgrade           # upgrade all installed packages
capp upgrade MyApp     # upgrade a single package
```

Only upgrades packages that are recorded in `installed.txt`. For each package, CAPP runs the old uninstall script (with your confirmation), then installs the latest version from the mirror. Requires a prior `capp update`.

---

## Browsing Packages

### list

```sh
capp list
```

Lists every package in `~/.capp/bundles/available.txt`, showing only the latest version of each unique package name. Requires a prior `capp update`.

```
=== CAPP — Available Packages ===

  MyApp                     2.0.0
  OtherApp                  3.1.0

  2 package(s) available.
```

### list-installed

```sh
capp list-installed
```

Lists every package recorded in `~/.capp/bundles/installed.txt`.

```
=== CAPP — Installed Packages ===

  MyApp
  OtherApp

  2 package(s) installed.
```

### search

```sh
capp search <query>
```

Case-insensitive search across two sources:

- **Available packages** — searched by name against `available.txt`. Results marked with `*` are currently installed.
- **Installed packages** — searched by name, version, author, and description against each app's `metadata.json`. Only packages recorded in `installed.txt` appear here.

Requires a prior `capp update` for the available packages section to be populated.

### show

```sh
capp show MyApp
```

Displays full metadata for any package (installed or not), sourcing each field independently:

- **Name, Version, Author, Description** — read from `~/.capp/data/MyApp/metadata.json` if the data directory exists; otherwise a notice is shown.
- **Custom fields** — any additional fields found in `packages.json` or local `metadata.json`.
- **Installed** — always derived from `installed.txt` (`yes` or `no`), regardless of whether a data directory exists.
- **Latest** — the highest version found in `available.txt`, if present.

```
=== CAPP — Show: MyApp ===

  Name        : MyApp
  Version     : 1.0.0
  Author      : Your Name
  Description : Does useful things.
  Installed   : yes
  Latest      : 2.0.0  (MyApp-2.0.0.capp)
```

---

## Reading Instructions

```sh
capp man MyApp
capp man --verbose MyApp
```

Opens the cached instructions file from `~/.capp/data/MyApp/`. If the file was cleared by `clear-cache`, it is automatically re-extracted from the stored bundle in `~/.capp/bundles/` before opening. If the package name does not exist (neither installed nor available via `packages.json`), `capp man` exits with an error and does **not** create `~/.capp/data/<AppName>/`. On Linux without `xdg-open`, plain-text instruction files are printed directly to the terminal.

---

## Cache Management

```sh
capp clear-cache
```

Removes only the `instructions.*` files from every app's data directory in `~/.capp/data/`. The `metadata.json` for each app is **preserved**. Use this to free disk space; `capp man` will re-extract instructions from the stored bundle on demand.

---

## Building from Source

```sh
# Linux / macOS
gcc -o capp capp.c

# Windows (MinGW / MSYS2)
gcc -o capp.exe capp.c
```

### Runtime Dependencies

| Platform | Required Tools | Purpose |
|---|---|---|
| Linux / macOS | `curl` | Fetching from mirrors |
| Linux / macOS | `zip` | Creating `.capp` bundles |
| Linux / macOS | `unzip` | Installing and uninstalling |
| Windows | PowerShell | `Compress-Archive` / `Expand-Archive` |
| Windows | `curl` | Fetching from mirrors |

---

## Storage Layout

| Path | Purpose |
|---|---|
| `~/.capp/bundles/` | Stored `.capp` files for installed apps |
| `~/.capp/bundles/installed.txt` | Registry of installed app names |
| `~/.capp/bundles/available.txt` | Merged mirror package list (from `capp update`) |
| `~/.capp/bundles/packages.json` | Merged mirror metadata (from `capp update`) |
| `~/.capp/bin/` | App executables (managed by install scripts) |
| `~/.capp/data/<AppName>/metadata.json` | Package metadata |
| `~/.capp/data/<AppName>/instructions.*` | Cached instructions (clearable) |

File formats:
- `installed.txt`: one package name per line.
- `available.txt`: `name|version|filename` (same format as `packages.txt`).
- `metadata.json`: see the `metadata.json format` section above.
- `packages.json`: see the `packages.json Format (Required)` section below.

On Windows, replace `~` with `%USERPROFILE%` and `/` with `\`.

---

## Handling Dependencies

Dependencies are handled inside the install script:

```bash
#!/bin/bash
# install.sh

set -e

echo "Checking dependencies..."

if command -v apt-get &> /dev/null; then
    sudo apt-get install -y nodejs python3
elif command -v brew &> /dev/null; then
    brew install node python3
fi

echo "Installing MyApp..."
mkdir -p ~/.capp/apps/myapp
cp -r ./app/* ~/.capp/apps/myapp/

echo "Done."
```

---

## Security

Before executing any install or uninstall script, CAPP prints its full contents and requires explicit confirmation. **Always read the script before typing `y`.** Typing `N` aborts with no changes made to your system.

Instructions files are also checked for executable magic bytes and shebangs before being opened or cached. Any instructions file that appears to be executable is rejected outright.

---

## Mirror Configuration

### Environment Variable

```sh
export CAPP_MIRROR=https://raw.githubusercontent.com/yourusername/my-pkg-mirror/main
capp install-remote MyApp
```

### packages.txt Format

```
name|version|filename
MyApp|1.0.0|MyApp-1.0.0.capp
MyApp|2.0.0|MyApp-2.0.0.capp
OtherApp|3.1.0|OtherApp-3.1.0.capp
```

CAPP automatically selects the highest semantic version when installing or upgrading.
The same line format is used for the local `~/.capp/bundles/available.txt`.

### packages.json Format (Required)

`packages.json` is required on mirrors and provides richer metadata (description, author, custom fields) and the canonical `filename` for downloads.

**Required per-package fields**
- `name`
- `version`
- `filename`

**Optional fields**
- `author`
- `description`

**Custom fields**
Any additional string, number, boolean, or array fields are allowed and are surfaced by `capp show` alongside `metadata.json` fields.

```json
{
  "packages": [
    {
      "name": "MyApp",
      "version": "2.0.0",
      "filename": "MyApp-2.0.0.capp",
      "author": "Your Name",
      "description": "A short description of what the app does.",
      "homepage": "https://example.com",
      "tags": ["cli", "demo"]
    }
  ]
}
```

Extra fields are preserved and surfaced where relevant; only `name`, `version`, `filename`, `author`, and `description` are required for built-in features.

---

## Contributing Packages to the CAPP Mirror

### Step 1 — Fork and Clone

```sh
git clone https://github.com/yourusername/CAPP-mirror.git
cd CAPP-mirror
```

### Step 2 — Add Your Package

Copy your `.capp` file into `packages/` then regenerate `packages.txt`:

```sh
cp /path/to/MyApp-1.0.0.capp packages/
./generate_packages.sh
```

### Step 3 — Commit and Pull Request

```sh
git add packages/MyApp-1.0.0.capp packages.txt
git commit -m "Add MyApp 1.0.0"
git push origin add/myapp
```

Open a pull request against `dev-sudeep/CAPP-mirror:main`. The maintainer will verify that scripts are safe, `packages.txt` is correctly formatted, and versioning is consistent.

---

## Setting Up Your Own Mirror

```
my-pkg-mirror/
├── packages.txt
├── packages.json
└── packages/
    ├── MyApp-1.0.0.capp
    └── MyApp-2.0.0.capp
```

Use this script to auto-generate `packages.txt`:

```bash
#!/bin/bash
# generate_packages.sh
rm -f packages.txt
for zipfile in packages/*.capp; do
    [ -f "$zipfile" ] || continue
    filename=$(basename "$zipfile")
    name=$(echo "$filename" | sed 's/-[^-]*\.capp$//')
    version=$(echo "$filename" | sed 's/^.*-\([^-]*\)\.capp$/\1/')
    echo "$name|$version|$filename" >> packages.txt
done
cat packages.txt
```

Point CAPP at your mirror:

```sh
export CAPP_MIRROR=https://raw.githubusercontent.com/yourusername/my-pkg-mirror/main
capp update
capp list
```

---

## Platform Support

- ✓ Linux (all distributions with standard tools)
- ✓ macOS (10.15+)
- ✓ Windows (PowerShell + curl)
- ✓ Android Termux

---

## Limitations

- Packages are limited to ~100 MB per file on GitHub (use GitHub Releases for larger files)
- Install/uninstall scripts are platform-specific (separate `.sh` for Unix, `.bat` for Windows)
- No built-in dependency resolution between CAPP packages (handled in install scripts)

---

## License

MIT License. Provided as-is — feel free to modify and distribute.

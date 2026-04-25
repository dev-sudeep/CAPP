# CAPP — Compact App: A Simple App Bundle Utility

CAPP (Compact App) packages an application and its install/uninstall logic into a single `.capp` file (a ZIP archive). Users can install applications locally or from remote mirrors. The toolchain consists of a single C program with 6 subcommands: **create**, **install**, **install-remote**, **uninstall**, and **clear-cache**.

---

## Features

- **Simple bundling**: Package entire applications into `.capp` files with install/uninstall scripts
- **Remote installation**: Install packages directly from GitHub mirrors with automatic latest version detection
- **Cross-platform**: Works on Linux, macOS, Windows, and Android Termux
- **Security**: All install scripts are reviewed by users before execution
- **Version management**: Semantic version parsing and comparison for automatic latest version selection
- **Instruction caching**: Instructions persist in cache for later viewing via xdg-open or other viewers
- **No dependencies**: Uses only standard C library + system tools (curl, zip, unzip)

---

## Bundle Structure

```
MyApp.capp  (ZIP archive containing:)
├── install.sh / install.bat     — runs on installation
├── uninstall.sh / uninstall.bat — runs on uninstallation
├── instructions.<ext>           — usage documentation (optional)
├── <main file>                  — source code, binary, etc.
└── <anything else>
```

> **Note:** `.sh` scripts are used on Linux and macOS; `.bat` scripts are used on Windows. The installer selects the correct variant automatically at compile time.

---

## Creating a Bundle

### Method 1 — Manual

1. Place all required files in a folder.
2. ZIP the folder and rename the archive to `AppName.capp`.

### Method 2 — Using the Bundler

1. Place all required files in a folder.
2. Compile `capp` and run `capp create`:
   ```sh
   capp create
   ```
3. Enter the path to the source folder when prompted.
4. Enter the application name (without `.capp`) when prompted.
5. The bundler creates `AppName.capp` in the current directory.

---

## Installing a Bundle

### From Local File

```sh
capp install MyApp.capp
```

The installer performs these steps in order:

1. **Creates** `~/.capp/bundles/`, `~/.capp/bin/`, and `~/.capp/cache/` if needed
2. **Extracts** the `.capp` archive to a temporary directory
3. **Displays** the full contents of `install.sh` (or `install.bat` on Windows)
4. **Prompts** you to review the script for anything suspicious and confirm before proceeding (`[y/N]`)
5. **Runs** the install script
6. **Stores** the `.capp` bundle in `~/.capp/bundles/` for later use by the uninstaller
7. **Opens** `instructions.<ext>` (if present) with your default viewer
8. **Caches** the instructions file in `~/.capp/cache/AppName/` for persistent access
9. **Cleans up** the temporary extraction directory
10. **Prompts** you to add `~/.capp/bin` to your PATH

### From Remote Mirror

```sh
capp install-remote MyApp
```

The installer automatically fetches the latest version from your configured mirror and installs it. 

Supported environment variable:
```bash
export CAPP_MIRROR=https://raw.githubusercontent.com/yourusername/my-pkg-mirror/main
capp install-remote MyApp
```

The mirror will be checked for a `packages.txt` file with this format:

```
name|version|filename
MyApp|1.0.0|MyApp-1.0.0.capp
MyApp|1.2.0|MyApp-1.2.0.capp
MyApp|2.0.0|MyApp-2.0.0.capp
OtherApp|3.1.0|OtherApp-3.1.0.capp
```

CAPP automatically selects the highest semantic version (2.0.0 in this example).

---

## Uninstalling a Bundle

```sh
capp uninstall MyApp
# or equivalently:
capp uninstall MyApp.capp
```

The uninstaller looks up the stored bundle in `~/.capp/bundles/` automatically. It performs these steps:

1. **Confirms** you want to uninstall the named application (`[y/N]`)
2. **Verifies** the bundle exists in `~/.capp/bundles/`; exits with a clear error if not installed
3. **Extracts** the bundle to a temporary directory
4. **Displays** the full contents of `uninstall.sh` (or `uninstall.bat` on Windows)
5. **Prompts** you to review the script and confirm before proceeding (`[y/N]`)
6. **Runs** the uninstall script
7. **Cleans up** the temporary directory and stored bundle

---

## Clearing Cache

```sh
capp clear-cache
```

Removes all cached instruction files from `~/.capp/cache/`. Useful for freeing up disk space after viewing instructions.



## Building from Source

The program is single-file C99 with no external dependencies beyond a C standard library and platform tools.

```sh
# Linux / macOS
gcc -o capp capp.c

# Windows (MinGW / MSYS2)
gcc -o capp.exe capp.c
```

### Runtime Dependencies

| Platform      | Required Tools | Purpose                  |
|---------------|----------------|--------------------------|
| Linux / macOS | `curl`         | Fetching from mirrors    |
| Linux / macOS | `zip`          | Bundler — creating `.capp` |
| Linux / macOS | `unzip`        | Installer / uninstaller  |
| Windows       | PowerShell     | `Compress-Archive` / `Expand-Archive` |
| Windows       | `curl`         | Fetching from mirrors    |

---

## Bundle and Executable Storage

Installed bundles and app executables are kept at:

| Platform      | Bundles Path                  | Executables Path              | Cache Path                    |
|---------------|-------------------------------|-------------------------------|-------------------------------|
| Linux / macOS | `~/.capp/bundles/`            | `~/.capp/bin/`                | `~/.capp/cache/`              |
| Windows       | `%USERPROFILE%\.capp\bundles\` | `%USERPROFILE%\.capp\bin\`    | `%USERPROFILE%\.capp\cache\`  |

These directories are created automatically on first use.

---

## Handling Dependencies

If a package has external dependencies (system libraries, other applications, etc.), handle them in the install script:

```bash
#!/bin/bash
# install.sh - example with dependency handling

set -e

echo "Checking and installing dependencies..."

# Auto-install system dependencies
if command -v apt-get &> /dev/null; then
    # Debian/Ubuntu
    sudo apt-get update
    sudo apt-get install -y nodejs python3 libssl-dev
elif command -v brew &> /dev/null; then
    # macOS
    brew install node python3 openssl
elif command -v choco &> /dev/null; then
    # Windows
    choco install nodejs python3 -y
fi

echo "✓ Dependencies installed"

# Now install the actual application
echo "Installing MyApp..."
mkdir -p ~/.capp/apps/myapp
cp -r ./app/* ~/.capp/apps/myapp/

if [ -f ./package.json ]; then
    cd ~/.capp/apps/myapp
    npm install
fi

echo "✓ MyApp installed successfully"
```

The install script runs **before** any cleanup, so dependencies are installed in the user's environment before the bundle is cached.

---

## Security Note

Before executing any install or uninstall script, CAPP prints the full script contents to the terminal and requires explicit confirmation. **Always read the script carefully** before typing `y`. If anything looks suspicious, type `N` to abort — no changes will be made to your system.

---

## Usage Examples

### Create a Bundle

```bash
capp create
```

### Install from Local File

```bash
capp install MyApp.capp
```

### Install from Mirror

```bash
export CAPP_MIRROR=https://raw.githubusercontent.com/yourusername/my-pkg-mirror/main
capp install-remote MyApp
```

### Uninstall

```bash
capp uninstall MyApp
```

### Clear Cache

```bash
capp clear-cache
```

---

## Platform Support

- ✓ Linux (all distributions with standard tools)
- ✓ macOS (10.15+)
- ✓ Windows (PowerShell + curl)
- ✓ Android Termux

---

## Limitations

- Packages are limited to ~100MB per file on GitHub (use GitHub Releases for larger files)
- Install/uninstall scripts are platform-specific (need separate .sh for Unix, .bat for Windows)
- No built-in package dependency resolution (handled in install scripts)

---

## License

MIT License

This project is provided as-is. Feel free to modify and distribute.

---

## Contributing Packages to CAPP Mirror

To add a package to the official CAPP mirror (`dev-sudeep/CAPP-mirror`):

### Step 1: Fork the Repository

Fork `https://github.com/dev-sudeep/CAPP-mirror` to your own GitHub account.

### Step 2: Clone Your Fork

```bash
git clone https://github.com/yourusername/CAPP-mirror.git
cd CAPP-mirror
```

### Step 3: Create a Feature Branch

```bash
git checkout -b add/package-name
# or for updates:
git checkout -b update/package-name
```

### Step 4: Add Your Package

1. Copy your `.capp` file to the `packages/` directory
2. Run the `generate_packages.sh` script to update `packages.txt`

```bash
./generate_packages.sh
```

### Step 5: Commit and Push

```bash
git add packages/YourApp-1.0.0.capp packages.txt
git commit -m "Add YourApp 1.0.0"
git push origin add/package-name
```

### Step 6: Create a Pull Request

1. Go to `https://github.com/yourusername/CAPP-mirror`
2. Click "Compare & pull request"
3. Set the base to `dev-sudeep/CAPP-mirror:main`
4. Add a description of your package (what it does, dependencies, etc.)
5. Submit the pull request

### Step 7: Review and Merge

The maintainer will review your package, checking:
- Install/uninstall scripts are safe
- `packages.txt` is correctly formatted
- Version numbering makes sense
- No malicious code

Once approved, your package is merged to `main` and immediately available to users!

---

## Setting Up Your Own Mirror

If you want to create your own independent mirror:

### Step 1: Create a Repository

```bash
git clone https://github.com/yourusername/my-pkg-mirror.git
cd my-pkg-mirror
mkdir packages
```

### Step 2: Create Directory Structure

```
my-pkg-mirror/
├── packages.txt
└── packages/
    ├── MyApp-1.0.0.capp
    └── MyApp-1.2.0.capp
```

### Step 3: Generate packages.txt

Create a script to auto-generate the list:

```bash
#!/bin/bash
# generate_packages.sh

rm -f packages.txt

for zipfile in packages/*.capp; do
    if [ -f "$zipfile" ]; then
        filename=$(basename "$zipfile")
        name=$(echo "$filename" | sed 's/-[^-]*\.capp$//')
        version=$(echo "$filename" | sed 's/^.*-\([^-]*\)\.capp$/\1/')
        
        echo "$name|$version|$filename" >> packages.txt
    fi
done

echo "Generated packages.txt:"
cat packages.txt
```

Run it:
```bash
chmod +x generate_packages.sh
./generate_packages.sh
```

### Step 4: Add Packages and Push

```bash
cp /path/to/MyApp-1.0.0.capp packages/
./generate_packages.sh
git add packages.txt packages/
git commit -m "Add MyApp 1.0.0"
git push origin main
```

### Step 5: Use Your Mirror

```bash
export CAPP_MIRROR=https://raw.githubusercontent.com/yourusername/my-pkg-mirror/main
capp install-remote MyApp
```

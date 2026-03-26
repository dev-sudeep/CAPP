# CAPP — A Simple App Bundle Utility

CAPP packages an application and its install/uninstall logic into a single
`.capp` file (a renamed ZIP archive). The toolchain consists of three C
programs: **bundler**, **capp-install**, and **capp-uninstall**.

---

## Bundle Structure

```
MyApp.capp  (ZIP archive containing:)
├── install.sh / install.bat     — runs on installation
├── uninstall.sh / uninstall.bat — runs on uninstallation
├── instructions.<ext>           — usage documentation
├── <main file>                  — source code, binary, etc.
└── <anything else>
```

> **Note:** `.sh` scripts are used on Linux and macOS; `.bat` scripts are used
> on Windows. The bundler and installer select the correct variant automatically
> at compile time.

---

## Creating a Bundle

### Method 1 — Manual

1. Place all required files in a folder.
2. ZIP the folder and rename the archive to `AppName.capp`.

### Method 2 — Using the Bundler

1. Place all required files in a folder.
2. Compile and run `bundler`:
   ```sh
   ./bundler
   ```
3. Enter the path to the source folder when prompted.
4. Enter the application name (without `.capp`) when prompted.
5. The bundler creates `AppName.capp` in the current directory.

---

## Installing a Bundle

```sh
capp-install MyApp.capp
```

The installer performs these steps in order:

1. **Extracts** the `.capp` archive to a temporary directory.
2. **Displays** the full contents of `install.sh` (or `install.bat` on Windows).
3. **Prompts** you to review the script for anything suspicious and confirm
   before proceeding (`[y/N]`). Entering anything other than `y` aborts cleanly.
4. **Runs** the install script.
5. **Cleans up** the temporary extraction directory.
6. **Stores** the `.capp` bundle in `~/.capp/bundles/` (Windows:
   `%USERPROFILE%\.capp\bundles\`) for later use by the uninstaller.

After installation the original `.capp` file is moved — it lives at
`~/.capp/bundles/MyApp.capp`.

---

## Uninstalling a Bundle

```sh
capp-uninstall MyApp
# or equivalently:
capp-uninstall MyApp.capp
```

The uninstaller looks up the stored bundle in `~/.capp/bundles/` automatically —
you do not need to supply a file path. It performs these steps:

1. **Confirms** you want to uninstall the named application (`[y/N]`).
2. **Verifies** the bundle exists in `~/.capp/bundles/`; exits with a clear
   error if the application is not installed.
3. **Extracts** the bundle to a temporary directory inside `~/.capp/bundles/`.
4. **Displays** the full contents of `uninstall.sh` (or `uninstall.bat` on
   Windows).
5. **Prompts** you to review the script and confirm before proceeding (`[y/N]`).
   Entering anything other than `y` aborts cleanly and removes the temp directory.
6. **Runs** the uninstall script.
7. **Cleans up** the temporary extraction directory.
8. **Removes** the stored `.capp` bundle from `~/.capp/bundles/`.

---

## Building from Source

All three programs are single-file C99 with no external dependencies beyond a
C standard library and the platform's `zip`/`unzip` utilities.

```sh
# Linux / macOS
gcc -o bundler        bundler.c
gcc -o capp-install   install.c
gcc -o capp-uninstall uninstall.c

# Windows (MinGW / MSYS2)
gcc -o bundler.exe        bundler.c
gcc -o capp-install.exe   install.c
gcc -o capp-uninstall.exe uninstall.c
```

### Runtime dependencies

| Platform      | Required tool | Purpose                        |
|---------------|---------------|--------------------------------|
| Linux / macOS | `zip`         | Bundler — creating `.capp`     |
| Linux / macOS | `unzip`       | Installer / uninstaller        |
| Windows       | PowerShell    | `Compress-Archive` / `Expand-Archive` |

---

## Bundle Storage

Installed bundles are kept at:

| Platform | Path                              |
|----------|-----------------------------------|
| Linux / macOS | `~/.capp/bundles/`           |
| Windows  | `%USERPROFILE%\.capp\bundles\`    |

This directory is created automatically on first install.

---

## Security Note

Before executing any install or uninstall script, CAPP prints the full script
contents to the terminal and requires explicit confirmation. **Always read the
script carefully** before typing `y`. If anything looks suspicious, type `N` to
abort — no changes will be made to your system.

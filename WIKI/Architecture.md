# Architectural Overview

CAPP's architecture revolves around a minimalist, portable C program (`capp.c`) that interacts with the host operating system using standard shell commands to handle `.capp` archives.

## Bundle Structure
A `.capp` file is a ZIP archive containing everything an application needs:
- `install.sh` / `install.bat`: The setup script that executes upon installation.
- `uninstall.sh` / `uninstall.bat`: The teardown script that executes upon uninstallation.
- `metadata.json`: Vital package information including `name`, `version`, `author`, and `description`.
- `instructions.<ext>`: Optional usage documentation displayed to the user after installation.
- Application assets (source code, binaries, configuration files).

## System State & Storage Layout
CAPP manages application states strictly within the user's home directory (`~/.capp/` or `%USERPROFILE%\.capp`):
- `~/.capp/bundles/`: Caches downloaded `.capp` archives and keeps track of packages via `installed.txt` (installed apps), `available.txt` (mirror app list), and `packages.json`.
- `~/.capp/bin/`: The target directory where application executables are typically placed by the install scripts.
- `~/.capp/data/<AppName>/`: Stores individual `metadata.json` and cached instructions for each app.

## Mirror System
Remote mirrors host a `packages.txt` and a `packages.json` to index available `.capp` files. When querying a mirror, CAPP parses semantic versions from the index to automate version resolution and fetch the latest packages seamlessly.

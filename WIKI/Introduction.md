# Introduction

**CAPP (Compact App)** is a lightweight, dependency-free application bundle utility. It is designed to package an application along with its installation and uninstallation logic into a single `.capp` file (which is effectively a standard ZIP archive). 

CAPP simplifies software distribution by allowing users to install applications either locally or remotely from configured mirrors, complete with version management, package metadata, and instruction caching. The tool is cross-platform and supports Linux, macOS, Windows, and Android Termux, utilizing native system tools (like `curl`, `zip`, `unzip`, and PowerShell) alongside a single C program.

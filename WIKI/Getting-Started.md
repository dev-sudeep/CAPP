# Getting Started

## Prerequisites & Compilation
Building CAPP from source requires only a C compiler (`gcc`). CAPP also relies on a few runtime tools depending on the OS:
- **Linux/macOS:** `curl`, `zip`, `unzip`
- **Windows:** `curl`, PowerShell (`Compress-Archive` / `Expand-Archive`)

Compile the core program:
```bash
# Linux / macOS
gcc -o capp capp.c

# Windows (MinGW / MSYS2)
gcc -o capp.exe capp.c
```

## Basic Commands
Once compiled, you can use the `./capp` binary to manage packages:

1. **Update and Explore Apps:**
   Refresh the list of available packages from the remote mirror and view them:
   ```bash
   ./capp update
   ./capp list
   ```
2. **Install an App:**
   Install a remote app by name or deploy a locally created `.capp` bundle:
   ```bash
   ./capp install-remote <AppName>
   # or
   ./capp install MyApp.capp
   ```
3. **Read Instructions:**
   View the manual for a previously installed package:
   ```bash
   ./capp man <AppName>
   ```
4. **Uninstall an App:**
   Safely remove an application and trigger its teardown script:
   ```bash
   ./capp uninstall <AppName>
   ```

## Creating Your First App (Using `C_hello`)
You can use the bundled `C_hello` example to understand `.capp` packaging.
1. Inspect the `C_hello/` directory to see its `install.sh`, `uninstall.sh`, and `metadata.json`.
2. Run the interactive bundler:
   ```bash
   ./capp create
   ```
3. Enter `C_hello` when prompted for the folder path and application name.
4. CAPP will generate `C_hello.capp`. You can then distribute it or test it using `./capp install C_hello.capp`.

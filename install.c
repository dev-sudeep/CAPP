/*
 * install.c - CAPP Bundle Installer
 * Extracts a .capp bundle, runs its install script, then stores the
 * bundle in ~/.capp/bundles/ for later use by capp-uninstall.
 *
 * Compile:
 *   Linux/macOS: gcc -o capp-install install.c
 *   Windows (MinGW): gcc -o capp-install.exe install.c
 *
 * Usage: capp-install <App.capp>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
  #include <windows.h>   /* FindFirstFile / FindNextFile */
  #define PATH_SEP "\\"
  #define UNZIP_CMD      "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\""
  #define INSTALL_SCRIPT "install.bat"
  #define RUN_INSTALL    "cmd /c \"%s" PATH_SEP INSTALL_SCRIPT "\""
  #define RMDIR_CMD      "rmdir /s /q \"%s\""
  #define MKDIR_CMD      "powershell -Command \"New-Item -ItemType Directory -Force -Path '%s'\" > nul"
  #define MOVE_CMD       "move /Y \"%s\" \"%s\" > nul"
  #define CAT_CMD        "type \"%s\""
  #define OPEN_CMD       "start \"\" \"%s\""
  #define HOME_ENV       "USERPROFILE"
  #define BUNDLES_SUBDIR "\\.capp\\bundles"
#else
  #include <dirent.h>    /* opendir / readdir */
  #include <unistd.h>    /* getcwd() */
  #include <sys/stat.h>  /* stat(), chmod() */
  #define PATH_SEP "/"
  #define UNZIP_CMD      "unzip -o '%s' -d '%s'"
  #define INSTALL_SCRIPT "install.sh"
  #define RUN_INSTALL    "bash '%s" PATH_SEP INSTALL_SCRIPT "'"
  #define RMDIR_CMD      "rm -rf '%s'"
  #define MKDIR_CMD      "mkdir -p '%s'"
  #define MOVE_CMD       "mv -f '%s' '%s'"
  #define CAT_CMD        "cat '%s'"
  #ifdef __APPLE__
    #define OPEN_CMD     "open '%s'"
  #else
    #define OPEN_CMD     "xdg-open '%s'"
  #endif
  #define HOME_ENV       "HOME"
  #define BUNDLES_SUBDIR "/.capp/bundles"
#endif

#define MAX_PATH 512
#define MAX_CMD  1024

/* Check that the bundle filename ends with .capp */
static int has_capp_ext(const char *name) {
    size_t len = strlen(name);
    return len > 5 && strcmp(name + len - 5, ".capp") == 0;
}

/* Extract just the filename (basename) from a path */
static const char *basename_of(const char *path) {
    const char *p;
#ifdef _WIN32
    if ((p = strrchr(path, '\\'))) return p + 1;
#endif
    if ((p = strrchr(path, '/'))) return p + 1;
    return path;
}

/* Derive the app name (filename without .capp) */
static void make_app_name(const char *bundle, char *out, size_t out_sz) {
    const char *base = basename_of(bundle);
    size_t base_len = strlen(base) - 5; /* strip ".capp" */
    size_t copy_len = base_len < out_sz - 1 ? base_len : out_sz - 1;
    strncpy(out, base, copy_len);
    out[copy_len] = '\0';
}

/* Build the path to the ~/.capp/bundles directory */
static int get_bundles_dir(char *out, size_t out_sz) {
    const char *home = getenv(HOME_ENV);
    if (!home || strlen(home) == 0) {
        fprintf(stderr, "Error: Cannot determine home directory ($" HOME_ENV " not set).\n");
        return 0;
    }
    snprintf(out, out_sz, "%s%s", home, BUNDLES_SUBDIR);
    return 1;
}

/*
 * Print the contents of the install script and ask the user to confirm
 * they have reviewed it and wish to proceed.
 * Returns 1 if the user confirms, 0 if they abort.
 */
static int review_script(const char *extract_dir) {
    char script_path[MAX_PATH];
    char cmd[MAX_CMD];
    char answer[8];

    snprintf(script_path, sizeof(script_path),
             "%s" PATH_SEP INSTALL_SCRIPT, extract_dir);

    /* Check the script actually exists in the bundle */
    FILE *f = fopen(script_path, "r");
    if (!f) {
        fprintf(stderr, "[install] Warning: " INSTALL_SCRIPT " not found in bundle.\n");
        return 0;
    }
    fclose(f);

    printf("\n[install] ---- Contents of " INSTALL_SCRIPT " ----\n\n");
    snprintf(cmd, sizeof(cmd), CAT_CMD, script_path);
    system(cmd);
    printf("\n[install] ---- End of " INSTALL_SCRIPT " ----\n\n");

    printf("[install] Please review the script above for anything suspicious.\n");
    printf("[install] Proceed with installation? [y/N]: ");
    fflush(stdout);

    if (!fgets(answer, sizeof(answer), stdin)) return 0;
    answer[strcspn(answer, "\r\n")] = '\0';
    return (answer[0] == 'y' || answer[0] == 'Y');
}

/* Known plain-text extensions that can be printed to the terminal */
static const char *TEXT_EXTS[] = {
    ".txt", ".md", ".rst", ".csv", ".log", ".ini", ".cfg",
    ".xml", ".json", ".yaml", ".yml", ".toml", NULL
};

/* Returns 1 if the file extension is a known plain-text format */
static int is_text_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;
    for (int i = 0; TEXT_EXTS[i]; i++) {
        if (strcmp(dot, TEXT_EXTS[i]) == 0) return 1;
    }
    return 0;
}

/*
 * Scan the first SAMPLE_BYTES of a file for NUL bytes.
 * A NUL byte is a strong indicator of binary content.
 */
#define SAMPLE_BYTES 512
static int is_binary_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1; /* can't read — treat as binary to be safe */
    unsigned char buf[SAMPLE_BYTES];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == 0) return 1;
    }
    return 0;
}

/*
 * Returns 1 if the file is an executable binary or a shell script.
 * Detection is based purely on file content (magic bytes / shebang),
 * never on permission bits — a normal document with +x set is NOT flagged.
 *
 * Checks performed:
 *   ELF binary         : magic 0x7f 'E' 'L' 'F'
 *   Windows PE binary  : magic 'M' 'Z'
 *   Mach-O 32-bit      : magic 0xCE 0xFA 0xED 0xFE
 *   Mach-O 64-bit      : magic 0xCF 0xFA 0xED 0xFE
 *   Mach-O fat binary  : magic 0xCA 0xFE 0xBA 0xBE
 *   Shell / interpreter: shebang line (#!)
 *   Windows executables: .exe .com .msi .scr .pif (binary launchers)
 *   PowerShell scripts : .ps1 .psm1 .psd1 (no magic byte — extension only)
 *   Windows scripts    : .bat .cmd (no magic byte — extension only)
 */
static int is_executable_file(const char *path) {
    unsigned char buf[SAMPLE_BYTES] = {0};
    size_t n = 0;

    FILE *f = fopen(path, "rb");
    if (f) {
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
    }

    /* ELF binary (Linux / Unix) */
    if (n >= 4 &&
        buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F')
        return 1;

    /* Windows PE binary */
    if (n >= 2 && buf[0] == 'M' && buf[1] == 'Z')
        return 1;

    /* Mach-O 32-bit little-endian */
    if (n >= 4 &&
        buf[0] == 0xCE && buf[1] == 0xFA && buf[2] == 0xED && buf[3] == 0xFE)
        return 1;

    /* Mach-O 64-bit little-endian */
    if (n >= 4 &&
        buf[0] == 0xCF && buf[1] == 0xFA && buf[2] == 0xED && buf[3] == 0xFE)
        return 1;

    /* Mach-O fat binary */
    if (n >= 4 &&
        buf[0] == 0xCA && buf[1] == 0xFE && buf[2] == 0xBA && buf[3] == 0xBE)
        return 1;

    /* Shebang — shell script or any interpreter script (#!) */
    if (n >= 2 && buf[0] == '#' && buf[1] == '!')
        return 1;

    /*
     * Extension-based checks for script types that have no magic bytes.
     * Applied on all platforms so a Windows script dropped into a Linux
     * bundle is still caught.
     */
    {
        /* Build a lowercase copy of the extension for comparison */
        const char *dot = strrchr(path, '.');
        if (dot) {
            char lower_ext[16] = {0};
            size_t ei;
            for (ei = 0; ei < 15 && dot[ei]; ei++)
                lower_ext[ei] = (char)tolower((unsigned char)dot[ei]);

            /* PowerShell: .ps1 (script), .psm1 (module), .psd1 (data/manifest) */
            if (strcmp(lower_ext, ".ps1")  == 0) return 1;
            if (strcmp(lower_ext, ".psm1") == 0) return 1;
            if (strcmp(lower_ext, ".psd1") == 0) return 1;

            /* Windows shell scripts */
            if (strcmp(lower_ext, ".bat") == 0) return 1;
            if (strcmp(lower_ext, ".cmd") == 0) return 1;

            /* Windows binary launchers */
            if (strcmp(lower_ext, ".exe") == 0) return 1;
            if (strcmp(lower_ext, ".com") == 0) return 1;
            if (strcmp(lower_ext, ".msi") == 0) return 1;
            if (strcmp(lower_ext, ".scr") == 0) return 1;
            if (strcmp(lower_ext, ".pif") == 0) return 1;
        }
    }

    return 0;
}

/*
 * Remove all execute permission bits from a file before the OS opens it.
 * This prevents accidental or malicious execution even if the file somehow
 * passes the is_executable_file() check.
 *
 * Unix/macOS : chmod a-x via the POSIX chmod() syscall.
 * Windows    : uses icacls to deny Execute File permission for Everyone.
 *
 * Logs a warning but does NOT abort if the permission change fails — the
 * is_executable_file() check already acts as the primary gate.
 */
static void strip_exec_permissions(const char *path) {
#ifdef _WIN32
    char cmd[MAX_CMD];
    /* Deny execute for the Everyone group (well-known SID "*S-1-1-0") */
    snprintf(cmd, sizeof(cmd),
             "icacls \"%s\" /deny *S-1-1-0:(X) > nul 2>&1", path);
    if (system(cmd) != 0) {
        fprintf(stderr, "[install] Warning: Could not strip execute permission "
                        "from '%s' via icacls.\n", basename_of(path));
    }
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "[install] Warning: stat() failed on '%s'; "
                        "could not strip execute bits.\n", basename_of(path));
        return;
    }
    /* Remove all three execute bits (user, group, other) */
    mode_t new_mode = st.st_mode & ~(S_IXUSR | S_IXGRP | S_IXOTH);
    if (chmod(path, new_mode) != 0) {
        fprintf(stderr, "[install] Warning: chmod() failed on '%s'; "
                        "could not strip execute bits.\n", basename_of(path));
    }
#endif
}

/*
 * Search extract_dir for a file whose stem is "instructions" (any extension).
 * Returns -1 and prints an error if the file is detected as an executable or
 * shell script — the caller must abort and clean up in that case.
 * On macOS  → open with `open`
 * On Windows → open with `start`
 * On Linux  → try xdg-open; if unavailable and file is text, cat it.
 * Returns 0 on success (including when no instructions file is present).
 */
static int find_and_open_instructions(const char *extract_dir) {
    char found_path[MAX_PATH];
    found_path[0] = '\0';

#ifdef _WIN32
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\instructions.*", extract_dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[install] No instructions file found in bundle.\n");
        return 0;
    }
    snprintf(found_path, sizeof(found_path), "%s\\%s", extract_dir, fd.cFileName);
    FindClose(h);

#else
    DIR *dir = opendir(extract_dir);
    if (!dir) {
        fprintf(stderr, "[install] Warning: Could not open extract dir to find instructions.\n");
        return 0;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strncmp(name, "instructions", 12) == 0 && name[12] == '.') {
            snprintf(found_path, sizeof(found_path), "%s/%s", extract_dir, name);
            break;
        }
    }
    closedir(dir);

    if (found_path[0] == '\0') {
        printf("[install] No instructions file found in bundle.\n");
        return 0;
    }
#endif

    printf("[install] Found instructions: %s\n", found_path);

    /* Safety check: refuse to open if the file is an executable or script */
    if (is_executable_file(found_path)) {
        fprintf(stderr, "[install] SECURITY ERROR: The instructions file '%s'\n",
                basename_of(found_path));
        fprintf(stderr, "[install]   is an executable binary or shell script.\n");
        fprintf(stderr, "[install]   This bundle may be malicious. Aborting.\n");
        return -1;
    }

    /*
     * Strip execute permission bits before handing the file to the OS.
     * This ensures that even if a file slips past the content check (e.g. a
     * novel binary format), the OS cannot execute it as a program.
     */
    strip_exec_permissions(found_path);

#if defined(_WIN32) || defined(__APPLE__)
    /* macOS: open  |  Windows: start — delegate to the OS default viewer */
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), OPEN_CMD, found_path);
    printf("[install] Opening instructions...\n");
    system(cmd);

#else
    /* Linux: prefer xdg-open; fall back to cat for text files */
    if (system("which xdg-open > /dev/null 2>&1") == 0) {
        char cmd[MAX_CMD];
        snprintf(cmd, sizeof(cmd), OPEN_CMD, found_path);
        printf("[install] Opening instructions with xdg-open...\n");
        /* system() blocks until xdg-open exits, so the file is still
         * present on disk when the viewer reads it. */
        system(cmd);
    } else {
        printf("[install] xdg-open not found.\n");
        /* Print only if the extension is a known text format AND the file
         * contains no binary (NUL) bytes. */
        if (!is_text_ext(basename_of(found_path)) || is_binary_file(found_path)) {
            printf("[install] Instructions file is not a displayable text format"
                   " — cannot show in terminal.\n");
        } else {
            const char *name = basename_of(found_path);
            printf("[install] ---- Contents of %s ----\n\n", name);
            char cmd[MAX_CMD];
            snprintf(cmd, sizeof(cmd), CAT_CMD, found_path);
            system(cmd);
            printf("\n[install] ---- End of %s ----\n", name);
        }
    }
#endif

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <App.capp>\n", argv[0]);
        return 1;
    }

    const char *bundle = argv[1];

    if (!has_capp_ext(bundle)) {
        fprintf(stderr, "Error: '%s' does not have a .capp extension.\n", bundle);
        return 1;
    }

    char app_name[MAX_PATH];
    make_app_name(bundle, app_name, sizeof(app_name));

    /* Temporary extraction directory — resolved to an absolute path so that
     * tools like xdg-open (which calls realpath internally) never receive a
     * bare relative name like "hello_capp_tmp". */
    char extract_dir[MAX_PATH];
    {
        char rel[MAX_PATH];
        snprintf(rel, sizeof(rel), "%s_capp_tmp", app_name);
#ifdef _WIN32
        if (!_fullpath(extract_dir, rel, sizeof(extract_dir)))
            snprintf(extract_dir, sizeof(extract_dir), "%s", rel);
#else
        /* realpath() requires the path to already exist; use getcwd + join */
        char cwd[MAX_PATH];
        if (getcwd(cwd, sizeof(cwd)))
            snprintf(extract_dir, sizeof(extract_dir), "%s/%s", cwd, rel);
        else
            snprintf(extract_dir, sizeof(extract_dir), "%s", rel);
#endif
    }

    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

    char cmd[MAX_CMD];

    printf("=== CAPP Installer ===\n");
    printf("[install] Bundle   : %s\n", bundle);
    printf("[install] App name : %s\n\n", app_name);

    /* Step 1: Extract bundle to temp dir */
    snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle, extract_dir);
    printf("[install] Extracting bundle...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Extraction failed. Is 'unzip' installed?\n");
        return 1;
    }

    /* Step 2: Review install script before running */
    if (!review_script(extract_dir)) {
        printf("[install] Installation aborted by user.\n");
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 3: Run install script */
    snprintf(cmd, sizeof(cmd), RUN_INSTALL, extract_dir);
    printf("[install] Running install script...\n");

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\n[install] Install script exited with code %d.\n", ret);
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 4: Open the instructions file from the bundle */
    if (find_and_open_instructions(extract_dir) != 0) {
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 5: Clean up temp extraction folder */
    printf("[install] Cleaning up temporary files...\n");
    snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
    system(cmd);

    /* Step 6: Ensure ~/.capp/bundles/ exists */
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, bundles_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Could not create bundles directory '%s'.\n", bundles_dir);
        return 1;
    }

    /* Step 7: Move the .capp bundle into ~/.capp/bundles/ */
    char dest_path[MAX_PATH];
    snprintf(dest_path, sizeof(dest_path), "%s%s%s.capp",
             bundles_dir, PATH_SEP, app_name);

    snprintf(cmd, sizeof(cmd), MOVE_CMD, bundle, dest_path);
    printf("[install] Storing bundle in: %s\n", dest_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "Warning: Could not move bundle to '%s'.\n", dest_path);
        fprintf(stderr, "         You may need to move '%s' manually.\n", bundle);
    }

    printf("[install] Installation complete!\n");
    printf("[install] To uninstall, run: capp-uninstall %s\n", app_name);
    return 0;
}

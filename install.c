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

#ifdef _WIN32
  #define PATH_SEP "\\"
  #define UNZIP_CMD      "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\""
  #define INSTALL_SCRIPT "install.bat"
  #define RUN_INSTALL    "cmd /c \"%s" PATH_SEP INSTALL_SCRIPT "\""
  #define RMDIR_CMD      "rmdir /s /q \"%s\""
  #define MKDIR_CMD      "powershell -Command \"New-Item -ItemType Directory -Force -Path '%s'\" > nul"
  #define MOVE_CMD       "move /Y \"%s\" \"%s\" > nul"
  #define CAT_CMD        "type \"%s\""
  #define HOME_ENV       "USERPROFILE"
  #define BUNDLES_SUBDIR "\\.capp\\bundles"
#else
  #define PATH_SEP "/"
  #define UNZIP_CMD      "unzip -o '%s' -d '%s'"
  #define INSTALL_SCRIPT "install.sh"
  #define RUN_INSTALL    "bash '%s" PATH_SEP INSTALL_SCRIPT "'"
  #define RMDIR_CMD      "rm -rf '%s'"
  #define MKDIR_CMD      "mkdir -p '%s'"
  #define MOVE_CMD       "mv -f '%s' '%s'"
  #define CAT_CMD        "cat '%s'"
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

    /* Temporary extraction directory (in current working dir) */
    char extract_dir[MAX_PATH];
    snprintf(extract_dir, sizeof(extract_dir), "%s_capp_tmp", app_name);

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

    /* Step 4: Clean up temp extraction folder */
    printf("[install] Cleaning up temporary files...\n");
    snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
    system(cmd);

    /* Step 5: Ensure ~/.capp/bundles/ exists */
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, bundles_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Could not create bundles directory '%s'.\n", bundles_dir);
        return 1;
    }

    /* Step 6: Move the .capp bundle into ~/.capp/bundles/ */
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
    return 0;
}

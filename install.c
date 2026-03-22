/*
 * install.c - CAPP Bundle Installer
 * Extracts a .capp bundle and runs its install.sh script.
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
  #define UNZIP_CMD "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\""
  #define INSTALL_SCRIPT "install.bat"   /* Windows uses a .bat equivalent */
  #define RUN_INSTALL    "cmd /c \"%s" PATH_SEP INSTALL_SCRIPT "\""
  #define RMDIR_CMD      "rmdir /s /q \"%s\""
#else
  #define PATH_SEP "/"
  #define UNZIP_CMD "unzip -o '%s' -d '%s'"
  #define INSTALL_SCRIPT "install.sh"
  #define RUN_INSTALL    "bash '%s" PATH_SEP INSTALL_SCRIPT "'"
  #define RMDIR_CMD      "rm -rf '%s'"
#endif

#define MAX_PATH 512
#define MAX_CMD  1024

/* Check that the bundle filename ends with .capp */
static int has_capp_ext(const char *name) {
    size_t len = strlen(name);
    return len > 5 && strcmp(name + len - 5, ".capp") == 0;
}

/* Derive a temporary extraction directory name from the bundle filename */
static void make_extract_dir(const char *bundle, char *out, size_t out_sz) {
    /* Strip path prefix — use only the filename */
    const char *base = bundle;
    const char *p;
#ifdef _WIN32
    if ((p = strrchr(bundle, '\\'))) base = p + 1;
#endif
    if ((p = strrchr(base, '/'))) base = p + 1;

    /* Copy up to (but not including) the .capp extension */
    size_t base_len = strlen(base) - 5; /* subtract ".capp" */
    size_t copy_len = base_len < out_sz - 1 ? base_len : out_sz - 1;
    strncpy(out, base, copy_len);
    out[copy_len] = '\0';
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

    /* Derive extraction directory */
    char extract_dir[MAX_PATH];
    make_extract_dir(bundle, extract_dir, sizeof(extract_dir));

    char cmd[MAX_CMD];

    printf("=== CAPP Installer ===\n");
    printf("[install] Bundle  : %s\n", bundle);
    printf("[install] Extracting to: %s/\n\n", extract_dir);

    /* Step 1: Extract */
    snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle, extract_dir);
    printf("[install] Running: %s\n", cmd);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Extraction failed. Is 'unzip' installed?\n");
        return 1;
    }

    /* Step 2: Run install script */
    snprintf(cmd, sizeof(cmd), RUN_INSTALL, extract_dir);
    printf("\n[install] Running install script: %s\n", cmd);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\n[install] install script exited with code %d.\n", ret);
        /* Clean up on failure */
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 3: Clean up extracted folder */
    printf("\n[install] Cleaning up temporary files...\n");
    snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
    system(cmd);

    printf("[install] Installation complete!\n");
    return 0;
}

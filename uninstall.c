/*
 * uninstall.c - CAPP Bundle Uninstaller
 * Extracts a .capp bundle and runs its uninstall.sh script.
 *
 * Compile:
 *   Linux/macOS: gcc -o capp-uninstall uninstall.c
 *   Windows (MinGW): gcc -o capp-uninstall.exe uninstall.c
 *
 * Usage: capp-uninstall <App.capp>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define PATH_SEP "\\"
  #define UNZIP_CMD      "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\""
  #define UNINSTALL_SCRIPT "uninstall.bat"
  #define RUN_UNINSTALL    "cmd /c \"%s" PATH_SEP UNINSTALL_SCRIPT "\""
  #define RMDIR_CMD        "rmdir /s /q \"%s\""
#else
  #define PATH_SEP "/"
  #define UNZIP_CMD        "unzip -o '%s' -d '%s'"
  #define UNINSTALL_SCRIPT "uninstall.sh"
  #define RUN_UNINSTALL    "bash '%s" PATH_SEP UNINSTALL_SCRIPT "'"
  #define RMDIR_CMD        "rm -rf '%s'"
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
    const char *base = bundle;
    const char *p;
#ifdef _WIN32
    if ((p = strrchr(bundle, '\\'))) base = p + 1;
#endif
    if ((p = strrchr(base, '/'))) base = p + 1;

    size_t base_len = strlen(base) - 5;
    size_t copy_len = base_len < out_sz - 1 ? base_len : out_sz - 1;
    strncpy(out, base, copy_len);
    out[copy_len] = '\0';
}

/* Prompt the user for confirmation before proceeding */
static int confirm_uninstall(const char *app_name) {
    char answer[8];
    printf("[uninstall] Are you sure you want to uninstall '%s'? [y/N]: ", app_name);
    fflush(stdout);

    if (!fgets(answer, sizeof(answer), stdin)) {
        return 0;
    }
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

    /* Derive app name and extraction directory */
    char extract_dir[MAX_PATH];
    make_extract_dir(bundle, extract_dir, sizeof(extract_dir));

    char cmd[MAX_CMD];

    printf("=== CAPP Uninstaller ===\n");
    printf("[uninstall] Bundle : %s\n\n", bundle);

    /* Step 1: Confirmation */
    if (!confirm_uninstall(extract_dir)) {
        printf("[uninstall] Uninstall cancelled.\n");
        return 0;
    }

    printf("\n[uninstall] Extracting to: %s/\n", extract_dir);

    /* Step 2: Extract */
    snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle, extract_dir);
    printf("[uninstall] Running: %s\n", cmd);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Extraction failed. Is 'unzip' installed?\n");
        return 1;
    }

    /* Step 3: Run uninstall script */
    snprintf(cmd, sizeof(cmd), RUN_UNINSTALL, extract_dir);
    printf("\n[uninstall] Running uninstall script: %s\n", cmd);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\n[uninstall] Uninstall script exited with code %d.\n", ret);
        /* Clean up even on failure */
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 4: Clean up extracted folder */
    printf("\n[uninstall] Cleaning up temporary files...\n");
    snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
    system(cmd);

    printf("[uninstall] Uninstallation complete!\n");
    return 0;
}

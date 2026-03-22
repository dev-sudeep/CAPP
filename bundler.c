/*
 * bundler.c - CAPP Bundle Creator
 * Bundles a folder of files into a .capp archive.
 *
 * Compile:
 *   Linux/macOS: gcc -o bundler bundler.c
 *   Windows (MinGW): gcc -o bundler.exe bundler.c
 *
 * Usage: Run the binary and follow the prompts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define PATH_SEP "\\"
  #define ZIP_CMD "powershell -Command \"Compress-Archive -Path '%s\\*' -DestinationPath '%s'\""
#elif defined(__APPLE__)
  #define PATH_SEP "/"
  #define ZIP_CMD "cd '%s' && zip -r '../%s' . -x '*.DS_Store'"
#else
  #define PATH_SEP "/"
  #define ZIP_CMD "cd '%s' && zip -r '../%s' ."
#endif

#define MAX_PATH  512
#define MAX_NAME  256
#define MAX_CMD  1024

/* Strip trailing slash/backslash from a path string */
static void strip_trailing_sep(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '/' || s[len - 1] == '\\')) {
        s[--len] = '\0';
    }
}

int main(void) {
    char folder[MAX_PATH];
    char app_name[MAX_NAME];
    char out_file[MAX_PATH];
    char cmd[MAX_CMD];

    printf("=== CAPP Bundler ===\n\n");

    /* 1. Source folder */
    printf("Enter the path to the source folder: ");
    if (!fgets(folder, sizeof(folder), stdin)) {
        fprintf(stderr, "Error reading input.\n");
        return 1;
    }
    folder[strcspn(folder, "\r\n")] = '\0';
    strip_trailing_sep(folder);

    if (strlen(folder) == 0) {
        fprintf(stderr, "Error: Folder path cannot be empty.\n");
        return 1;
    }

    /* 2. App name */
    printf("Enter the application name (without .capp): ");
    if (!fgets(app_name, sizeof(app_name), stdin)) {
        fprintf(stderr, "Error reading input.\n");
        return 1;
    }
    app_name[strcspn(app_name, "\r\n")] = '\0';

    if (strlen(app_name) == 0) {
        fprintf(stderr, "Error: Application name cannot be empty.\n");
        return 1;
    }

    /* 3. Build output filename */
    snprintf(out_file, sizeof(out_file), "%s.capp", app_name);

    printf("\n[bundler] Source  : %s\n", folder);
    printf("[bundler] Output  : %s\n\n", out_file);

    /* 4. Build and run zip command */
#ifdef _WIN32
    /* Windows: Compress-Archive writes a .zip; rename to .capp afterward */
    char tmp_zip[MAX_PATH];
    snprintf(tmp_zip, sizeof(tmp_zip), "%s.zip", app_name);
    snprintf(cmd, sizeof(cmd),
        "powershell -Command \"Compress-Archive -Path '%s\\*' -DestinationPath '%s' -Force\"",
        folder, tmp_zip);
    printf("[bundler] Running: %s\n", cmd);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Compression failed.\n");
        return 1;
    }
    /* Rename .zip -> .capp */
    snprintf(cmd, sizeof(cmd), "rename \"%s\" \"%s\"", tmp_zip, out_file);
    system(cmd);
#else
    snprintf(cmd, sizeof(cmd), ZIP_CMD, folder, out_file);
    printf("[bundler] Running: %s\n", cmd);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Compression failed. Is 'zip' installed?\n");
        return 1;
    }
#endif

    printf("\n[bundler] Success! Bundle created: %s\n", out_file);
    printf("[bundler] To install: unzip '%s' and run install.sh\n", out_file);

    return 0;
}

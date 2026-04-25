/*
 * capp.c - Compact App (CAPP) utility with mirror support
 *
 * Compile:
 *   Linux/macOS : gcc -o capp capp.c
 *   Windows     : gcc -o capp.exe capp.c
 *
 * Usage:
 *   capp create                              — interactively bundle a folder into a .capp
 *   capp install   <App.capp>                — install a bundle from local file
 *   capp install-remote <AppName> <Version> — install a bundle from mirror
 *   capp uninstall <AppName>                 — uninstall a previously installed app
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Platform-specific definitions ────────────────────────────────────────── */

#ifdef _WIN32
  #include <windows.h>       /* FindFirstFile, _fullpath */

  #define PATH_SEP          "\\"
  #define HOME_ENV          "USERPROFILE"
  #define BUNDLES_SUBDIR    "\\.capp\\bundles"
  #define BIN_SUBDIR        "\\.capp\\bin"

  /* create */
  #define ZIP_CMD           "powershell -Command \"Compress-Archive -Path '%s\\*' -DestinationPath '%s' -Force\""

  /* install / uninstall shared */
  #define UNZIP_CMD         "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\""
  #define RMDIR_CMD         "rmdir /s /q \"%s\""
  #define MKDIR_CMD         "powershell -Command \"New-Item -ItemType Directory -Force -Path '%s'\" > nul"
  #define MOVE_CMD          "move /Y \"%s\" \"%s\" > nul"
  #define REMOVE_FILE_CMD   "del /f /q \"%s\""
  #define CAT_CMD           "type \"%s\""
  #define OPEN_CMD          "start \"\" \"%s\""

  /* scripts */
  #define INSTALL_SCRIPT    "install.bat"
  #define UNINSTALL_SCRIPT  "uninstall.bat"
  #define RUN_INSTALL       "cmd /c \"%s" PATH_SEP INSTALL_SCRIPT "\""
  #define RUN_UNINSTALL     "cmd /c \"%s" PATH_SEP UNINSTALL_SCRIPT "\""

#else
  #include <dirent.h>        /* opendir / readdir */
  #include <unistd.h>        /* getcwd */
  #include <sys/stat.h>      /* stat, chmod */

  #define PATH_SEP          "/"
  #define HOME_ENV          "HOME"
  #define BUNDLES_SUBDIR    "/.capp/bundles"
  #define BIN_SUBDIR        "/.capp/bin"

  /* create */
  #ifdef __APPLE__
    #define ZIP_CMD         "cd '%s' && zip -r '../%s' . -x '*.DS_Store'"
    #define OPEN_CMD        "open '%s'"
  #else
    #define ZIP_CMD         "cd '%s' && zip -r '../%s' ."
    #define OPEN_CMD        "xdg-open '%s'"
  #endif

  /* install / uninstall shared */
  #define UNZIP_CMD         "unzip -o '%s' -d '%s'"
  #define RMDIR_CMD         "rm -rf '%s'"
  #define MKDIR_CMD         "mkdir -p '%s'"
  #define MOVE_CMD          "mv -f '%s' '%s'"
  #define REMOVE_FILE_CMD   "rm -f '%s'"
  #define CAT_CMD           "cat '%s'"

  /* scripts */
  #define INSTALL_SCRIPT    "install.sh"
  #define UNINSTALL_SCRIPT  "uninstall.sh"
  #define RUN_INSTALL       "bash '%s" PATH_SEP INSTALL_SCRIPT "'"
  #define RUN_UNINSTALL     "bash '%s" PATH_SEP UNINSTALL_SCRIPT "'"
#endif

#define MAX_PATH   512
#define MAX_CMD   1024
#define SAMPLE_BYTES 512

/* ── Mirror configuration ──────────────────────────────────────────────────── */

typedef struct {
    char url[MAX_PATH];
} Mirror;

/* Default mirrors - user can override via env var or config file */
static Mirror DEFAULT_MIRRORS[] = {
    {"https://raw.githubusercontent.com/dev-sudeep/CAPP-mirror/refs/heads/main"},
    {""} /* sentinel */
};

/* Get mirror URLs from environment or defaults */
static Mirror* get_mirrors(void) {
    const char *mirror_env = getenv("CAPP_MIRROR");
    if (mirror_env && strlen(mirror_env) > 0) {
        Mirror *mirrors = malloc(2 * sizeof(Mirror));
        strncpy(mirrors[0].url, mirror_env, MAX_PATH - 1);
        mirrors[0].url[MAX_PATH - 1] = '\0';
        mirrors[1].url[0] = '\0';
        return mirrors;
    }
    return DEFAULT_MIRRORS;
}

/* ── Shared helpers ────────────────────────────────────────────────────────── */

/* Extract the filename portion of a path */
static const char *basename_of(const char *path) {
    const char *p;
#ifdef _WIN32
    if ((p = strrchr(path, '\\'))) return p + 1;
#endif
    if ((p = strrchr(path, '/'))) return p + 1;
    return path;
}

/* Returns 1 if path ends with ".capp" */
static int has_capp_ext(const char *name) {
    size_t len = strlen(name);
    return len > 5 && strcmp(name + len - 5, ".capp") == 0;
}

/* Strip a trailing ".capp" from name in-place */
static void strip_capp_ext(char *name) {
    size_t len = strlen(name);
    if (len > 5 && strcmp(name + len - 5, ".capp") == 0)
        name[len - 5] = '\0';
}

/* Derive app name (basename without ".capp") */
static void make_app_name(const char *bundle, char *out, size_t out_sz) {
    const char *base = basename_of(bundle);
    size_t base_len = strlen(base) - 5;
    size_t copy_len = base_len < out_sz - 1 ? base_len : out_sz - 1;
    strncpy(out, base, copy_len);
    out[copy_len] = '\0';
}

/* Populate out with the path to ~/.capp/bundles */
static int get_bundles_dir(char *out, size_t out_sz) {
    const char *home = getenv(HOME_ENV);
    if (!home || strlen(home) == 0) {
        fprintf(stderr, "Error: $" HOME_ENV " is not set.\n");
        return 0;
    }
    snprintf(out, out_sz, "%s%s", home, BUNDLES_SUBDIR);
    return 1;
}

/* Populate out with the path to ~/.capp/bin */
static int get_bin_dir(char *out, size_t out_sz) {
    const char *home = getenv(HOME_ENV);
    if (!home || strlen(home) == 0) {
        fprintf(stderr, "Error: $" HOME_ENV " is not set.\n");
        return 0;
    }
    snprintf(out, out_sz, "%s%s", home, BIN_SUBDIR);
    return 1;
}

/* Populate out with the path to ~/.capp/cache */
static int get_cache_dir(char *out, size_t out_sz) {
    const char *home = getenv(HOME_ENV);
    if (!home || strlen(home) == 0) {
        fprintf(stderr, "Error: $" HOME_ENV " is not set.\n");
        return 0;
    }
#ifdef _WIN32
    snprintf(out, out_sz, "%s\\.capp\\cache", home);
#else
    snprintf(out, out_sz, "%s/.capp/cache", home);
#endif
    return 1;
}

/* Populate out with the path to the app's cache directory */
static int get_app_cache_dir(const char *app_name, char *out, size_t out_sz) {
    char cache_dir[MAX_PATH];
    if (!get_cache_dir(cache_dir, sizeof(cache_dir))) return 0;
#ifdef _WIN32
    snprintf(out, out_sz, "%s\\%s", cache_dir, app_name);
#else
    snprintf(out, out_sz, "%s/%s", cache_dir, app_name);
#endif
    return 1;
}

/* Strip trailing path separators from s */
static void strip_trailing_sep(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '/' || s[len - 1] == '\\'))
        s[--len] = '\0';
}

/* Check if an app is listed in installed.txt */
static int is_app_installed(const char *app_name) {
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 0;

    char installed_path[MAX_PATH];
    snprintf(installed_path, sizeof(installed_path), "%s%sinstalled.txt",
             bundles_dir, PATH_SEP);

    FILE *f = fopen(installed_path, "r");
    if (!f) return 0;

    char line[MAX_PATH];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, app_name) == 0) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

/* Add an app to installed.txt */
static void add_to_installed(const char *app_name) {
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return;

    char installed_path[MAX_PATH];
    snprintf(installed_path, sizeof(installed_path), "%s%sinstalled.txt",
             bundles_dir, PATH_SEP);

    FILE *f = fopen(installed_path, "a");
    if (!f) {
        fprintf(stderr, "[capp] Warning: Could not update installed.txt.\n");
        return;
    }
    fprintf(f, "%s\n", app_name);
    fclose(f);
}

/* Remove an app from installed.txt */
static void remove_from_installed(const char *app_name) {
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return;

    char installed_path[MAX_PATH];
    snprintf(installed_path, sizeof(installed_path), "%s%sinstalled.txt",
             bundles_dir, PATH_SEP);

    char temp_path[MAX_PATH];
    snprintf(temp_path, sizeof(temp_path), "%s%sinstalled.txt.tmp",
             bundles_dir, PATH_SEP);

    FILE *in = fopen(installed_path, "r");
    if (!in) return;

    FILE *out = fopen(temp_path, "w");
    if (!out) {
        fclose(in);
        fprintf(stderr, "[capp] Warning: Could not update installed.txt.\n");
        return;
    }

    char line[MAX_PATH];
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, app_name) != 0)
            fprintf(out, "%s\n", line);
    }

    fclose(in);
    fclose(out);

    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), MOVE_CMD, temp_path, installed_path);
    if (system(cmd) != 0)
        fprintf(stderr, "[capp] Warning: Could not update installed.txt.\n");
}

/* ── Executable / binary detection ────────────────────────────────────────── */

/* Known plain-text extensions */
static const char *TEXT_EXTS[] = {
    ".txt", ".md", ".rst", ".csv", ".log", ".ini", ".cfg",
    ".xml", ".json", ".yaml", ".yml", ".toml", NULL
};

static int is_text_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;
    for (int i = 0; TEXT_EXTS[i]; i++)
        if (strcmp(dot, TEXT_EXTS[i]) == 0) return 1;
    return 0;
}

/* Returns 1 if the file contains a NUL byte in its first SAMPLE_BYTES */
static int is_binary_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    unsigned char buf[SAMPLE_BYTES];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    for (size_t i = 0; i < n; i++)
        if (buf[i] == 0) return 1;
    return 0;
}

/*
 * Returns 1 if the file is an executable binary or runnable script.
 * Detection uses magic bytes and shebang — never permission bits.
 */
static int is_executable_file(const char *path) {
    unsigned char buf[SAMPLE_BYTES] = {0};
    size_t n = 0;
    FILE *f = fopen(path, "rb");
    if (f) { n = fread(buf, 1, sizeof(buf), f); fclose(f); }

    if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') return 1;
    if (n >= 2 && buf[0] == 'M'  && buf[1] == 'Z') return 1;
    if (n >= 4 && buf[0] == 0xCE && buf[1] == 0xFA && buf[2] == 0xED && buf[3] == 0xFE) return 1;
    if (n >= 4 && buf[0] == 0xCF && buf[1] == 0xFA && buf[2] == 0xED && buf[3] == 0xFE) return 1;
    if (n >= 4 && buf[0] == 0xCA && buf[1] == 0xFE && buf[2] == 0xBA && buf[3] == 0xBE) return 1;
    if (n >= 2 && buf[0] == '#'  && buf[1] == '!') return 1;

    /* Extension-based (applied on all platforms) */
    const char *dot = strrchr(path, '.');
    if (dot) {
        char ext[16] = {0};
        for (size_t i = 0; i < 15 && dot[i]; i++)
            ext[i] = (char)tolower((unsigned char)dot[i]);
        if (!strcmp(ext, ".ps1"))  return 1;
        if (!strcmp(ext, ".psm1")) return 1;
        if (!strcmp(ext, ".psd1")) return 1;
        if (!strcmp(ext, ".bat"))  return 1;
        if (!strcmp(ext, ".cmd"))  return 1;
        if (!strcmp(ext, ".exe"))  return 1;
        if (!strcmp(ext, ".com"))  return 1;
        if (!strcmp(ext, ".msi"))  return 1;
        if (!strcmp(ext, ".scr"))  return 1;
        if (!strcmp(ext, ".pif"))  return 1;
    }
    return 0;
}

/*
 * Remove all execute permission bits from path before the OS opens it.
 */
static void strip_exec_permissions(const char *path) {
#ifdef _WIN32
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd),
             "icacls \"%s\" /deny *S-1-1-0:(X) > nul 2>&1", path);
    if (system(cmd) != 0)
        fprintf(stderr, "[capp] Warning: Could not strip execute permission from '%s'.\n",
                basename_of(path));
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "[capp] Warning: stat() failed on '%s'.\n", basename_of(path));
        return;
    }
    mode_t new_mode = st.st_mode & ~(S_IXUSR | S_IXGRP | S_IXOTH);
    if (chmod(path, new_mode) != 0)
        fprintf(stderr, "[capp] Warning: chmod() failed on '%s'.\n", basename_of(path));
#endif
}

/* ── Script review prompt ──────────────────────────────────────────────────── */

/*
 * Print the contents of script_name inside extract_dir and ask the user to
 * confirm they want to proceed.  Returns 1 on confirmation, 0 on abort.
 */
static int review_script(const char *extract_dir, const char *script_name) {
    char script_path[MAX_PATH];
    char cmd[MAX_CMD];
    char answer[8];

    snprintf(script_path, sizeof(script_path), "%s%s%s", extract_dir, PATH_SEP, script_name);

    FILE *f = fopen(script_path, "r");
    if (!f) {
        fprintf(stderr, "[capp] Warning: %s not found in bundle.\n", script_name);
        return 0;
    }
    fclose(f);

    printf("\n[capp] ---- Contents of %s ----\n\n", script_name);
    snprintf(cmd, sizeof(cmd), CAT_CMD, script_path);
    system(cmd);
    printf("\n[capp] ---- End of %s ----\n\n", script_name);

    printf("[capp] Please review the script above for anything suspicious.\n");
    printf("[capp] Proceed? [y/N]: ");
    fflush(stdout);

    if (!fgets(answer, sizeof(answer), stdin)) return 0;
    answer[strcspn(answer, "\r\n")] = '\0';
    return (answer[0] == 'y' || answer[0] == 'Y');
}

/* ── Instructions file ─────────────────────────────────────────────────────── */

/*
 * Search extract_dir for instructions.<ext>.
 * Aborts (returns -1) if the file is an executable or script.
 * Copies the file to cache directory so it persists after extraction cleanup.
 * Otherwise opens it with the platform viewer.
 * Returns 0 on success or when no instructions file exists.
 */
static int open_instructions(const char *extract_dir, const char *app_name) {
    char found_path[MAX_PATH];
    char cache_dir[MAX_PATH];
    char cached_path[MAX_PATH];
    char cmd[MAX_CMD];
    found_path[0] = '\0';

#ifdef _WIN32
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\instructions.*", extract_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[capp] No instructions file found in bundle.\n");
        return 0;
    }
    snprintf(found_path, sizeof(found_path), "%s\\%s", extract_dir, fd.cFileName);
    FindClose(h);
#else
    DIR *dir = opendir(extract_dir);
    if (!dir) {
        fprintf(stderr, "[capp] Warning: Could not scan bundle directory.\n");
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
        printf("[capp] No instructions file found in bundle.\n");
        return 0;
    }
#endif

    printf("[capp] Found instructions: %s\n", found_path);

    if (is_executable_file(found_path)) {
        fprintf(stderr, "[capp] SECURITY ERROR: instructions file '%s' is an executable"
                        " or script. Aborting.\n", basename_of(found_path));
        return -1;
    }

    strip_exec_permissions(found_path);

    /* Create cache directory and copy instructions there */
    if (!get_app_cache_dir(app_name, cache_dir, sizeof(cache_dir))) return 0;
    
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, cache_dir);
    system(cmd);
    
    snprintf(cached_path, sizeof(cached_path), "%s%s%s", 
             cache_dir, PATH_SEP, basename_of(found_path));
    
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "copy /Y \"%s\" \"%s\" > nul", found_path, cached_path);
#else
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", found_path, cached_path);
#endif
    
    if (system(cmd) != 0) {
        fprintf(stderr, "[capp] Warning: Could not cache instructions file.\n");
        return 0;
    }

#if defined(_WIN32) || defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), OPEN_CMD, cached_path);
    printf("[capp] Opening instructions...\n");
    system(cmd);
#else
    if (system("which xdg-open > /dev/null 2>&1") == 0) {
        snprintf(cmd, sizeof(cmd), OPEN_CMD, cached_path);
        printf("[capp] Opening instructions...\n");
        system(cmd);
    } else {
        printf("[capp] xdg-open not found.\n");
        if (!is_text_ext(basename_of(cached_path)) || is_binary_file(cached_path)) {
            printf("[capp] Instructions file is not a displayable text format.\n");
        } else {
            const char *name = basename_of(cached_path);
            printf("[capp] ---- Contents of %s ----\n\n", name);
            snprintf(cmd, sizeof(cmd), CAT_CMD, cached_path);
            system(cmd);
            printf("\n[capp] ---- End of %s ----\n", name);
        }
    }
#endif

    return 0;
}

/* ── Mirror support: fetch packages.txt ────────────────────────────────────── */

/*
 * Version structure for semantic versioning
 */
typedef struct {
    int major;
    int minor;
    int patch;
} VersionParts;

/*
 * Parse a version string into parts.
 * Handles formats like: 1.2.3, 1.2, 1
 * Returns the parsed version.
 */
static VersionParts parse_version(const char *version_str) {
    VersionParts v = {0, 0, 0};
    sscanf(version_str, "%d.%d.%d", &v.major, &v.minor, &v.patch);
    return v;
}

/*
 * Compare two versions.
 * Returns: > 0 if v1 > v2, 0 if equal, < 0 if v1 < v2
 */
static int compare_versions(const char *v1_str, const char *v2_str) {
    VersionParts v1 = parse_version(v1_str);
    VersionParts v2 = parse_version(v2_str);
    
    if (v1.major != v2.major) return v1.major - v2.major;
    if (v1.minor != v2.minor) return v1.minor - v2.minor;
    if (v1.patch != v2.patch) return v1.patch - v2.patch;
    return 0;
}

/*
 * Package info structure
 */
typedef struct {
    char *version;
    char *filename;
} PackageInfo;

/*
 * Parse packages.txt and find a package matching name.
 * If pkg_version is NULL, returns the latest (highest) version.
 * If pkg_version is specified, returns that specific version.
 * Format: name|version|filename
 * Returns a struct with version, filename, or NULL on failure.
 * Caller must free the returned struct and its members.
 */
static PackageInfo* find_package_in_list(const char *packages_txt, 
                                         const char *pkg_name,
                                         const char *pkg_version) {
    char *list_copy = malloc(strlen(packages_txt) + 1);
    strcpy(list_copy, packages_txt);
    
    PackageInfo *best_match = NULL;
    char *line = strtok(list_copy, "\n");
    
    while (line) {
        char name[256], version[256], filename[256];
        
        /* Parse the pipe-delimited line */
        if (sscanf(line, "%255[^|]|%255[^|]|%255s",
                   name, version, filename) >= 3) {
            
            /* If specific version requested, find exact match */
            if (pkg_version) {
                if (strcmp(name, pkg_name) == 0 && strcmp(version, pkg_version) == 0) {
                    if (best_match) {
                        free(best_match->version);
                        free(best_match->filename);
                        free(best_match);
                    }
                    best_match = malloc(sizeof(PackageInfo));
                    best_match->version = malloc(strlen(version) + 1);
                    best_match->filename = malloc(strlen(filename) + 1);
                    strcpy(best_match->version, version);
                    strcpy(best_match->filename, filename);
                    break; /* Found exact match, stop searching */
                }
            } else {
                /* No specific version: find latest (highest version number) */
                if (strcmp(name, pkg_name) == 0) {
                    if (!best_match || compare_versions(version, best_match->version) > 0) {
                        if (best_match) {
                            free(best_match->version);
                            free(best_match->filename);
                            free(best_match);
                        }
                        best_match = malloc(sizeof(PackageInfo));
                        best_match->version = malloc(strlen(version) + 1);
                        best_match->filename = malloc(strlen(filename) + 1);
                        strcpy(best_match->version, version);
                        strcpy(best_match->filename, filename);
                    }
                }
            }
        }
        line = strtok(NULL, "\n");
    }
    
    free(list_copy);
    return best_match;
}

/*
 * Fetch packages.txt from a mirror URL.
 * Returns the file contents as a string, or NULL on failure.
 * Caller must free the returned string.
 */
static char* fetch_packages_list(const char *mirror_url) {
    char curl_url[MAX_PATH];
    char temp_file[MAX_PATH];
    char temp_dir[MAX_PATH];
    char cmd[MAX_CMD];
    
    /* Get home directory - works on Linux, macOS, Android Termux, Windows */
    const char *home = getenv(HOME_ENV);
    if (!home) home = ".";
    
#ifdef _WIN32
    snprintf(temp_dir, sizeof(temp_dir), "%s\\.capp", home);
    snprintf(temp_file, sizeof(temp_file), "%s\\.capp\\packages.tmp", home);
#else
    snprintf(temp_dir, sizeof(temp_dir), "%s/.capp", home);
    snprintf(temp_file, sizeof(temp_file), "%s/.capp/packages.tmp", home);
#endif
    
    /* Create temp directory if it doesn't exist */
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, temp_dir);
    system(cmd);
    
    /* Check if curl exists */
    if (system("curl --version > /dev/null 2>&1") != 0) {
        fprintf(stderr, "[capp] Error: curl is not installed.\n");
        return NULL;
    }
    
    snprintf(curl_url, sizeof(curl_url), "%s/packages.txt", mirror_url);
    snprintf(cmd, sizeof(cmd), "curl -s -o '%s' '%s'", temp_file, curl_url);
    
    printf("[capp] Fetching package list from: %s\n", mirror_url);
    
    int curl_ret = system(cmd);
    
    if (curl_ret != 0) {
        fprintf(stderr, "[capp] Error: Failed to fetch from mirror. Check your internet connection.\n");
        return NULL;
    }
    
    /* Read the file */
    FILE *f = fopen(temp_file, "r");
    if (!f) {
        fprintf(stderr, "[capp] Error: Could not read downloaded file.\n");
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size == 0) {
        fprintf(stderr, "[capp] Error: Downloaded file is empty. Mirror may be unreachable or packages.txt doesn't exist.\n");
        fclose(f);
        return NULL;
    }
    
    char *content = malloc(size + 1);
    size_t read = fread(content, 1, size, f);
    fclose(f);
    
    content[read] = '\0';
    
    /* Check for HTTP error in content */
    if (strncmp(content, "<!DOCTYPE", 9) == 0 || 
        strncmp(content, "<html", 5) == 0 ||
        strstr(content, "404") != NULL ||
        strstr(content, "400") != NULL ||
        strstr(content, "403") != NULL ||
        strstr(content, "500") != NULL) {
        fprintf(stderr, "[capp] Error: HTTP error from mirror (file not found or server error).\n");
        free(content);
        return NULL;
    }
    
    /* Clean up temp file */
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "del /f /q \"%s\"", temp_file);
#else
    snprintf(cmd, sizeof(cmd), "rm -f '%s'", temp_file);
#endif
    system(cmd);
    
    return content;
}

/*
 * Download a .capp file from mirror to local path.
 */
static int download_capp_file(const char *mirror_url,
                               const char *filename,
                               const char *dest_path) {
    char curl_url[MAX_PATH];
    char cmd[MAX_CMD];
    
    snprintf(curl_url, sizeof(curl_url), "%s/packages/%s", mirror_url, filename);
    snprintf(cmd, sizeof(cmd), "curl -s -f -o '%s' '%s'", dest_path, curl_url);
    
    printf("[capp] Downloading: %s\n", filename);
    int ret = system(cmd);
    
    if (ret != 0) {
        fprintf(stderr, "[capp] Failed to download from: %s\n", mirror_url);
        return 0;
    }
    
    return 1;
}

/* ── Subcommand: create ────────────────────────────────────────────────────── */

static int cmd_create(void) {
    char folder[MAX_PATH];
    char app_name[MAX_PATH];
    char out_file[MAX_PATH];
    char cmd[MAX_CMD];

    printf("=== CAPP — Create Bundle ===\n\n");

    printf("Source folder path: ");
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

    printf("Application name (without .capp): ");
    if (!fgets(app_name, sizeof(app_name), stdin)) {
        fprintf(stderr, "Error reading input.\n");
        return 1;
    }
    app_name[strcspn(app_name, "\r\n")] = '\0';
    if (strlen(app_name) == 0) {
        fprintf(stderr, "Error: Application name cannot be empty.\n");
        return 1;
    }

    snprintf(out_file, sizeof(out_file), "%s.capp", app_name);

    printf("\n[capp] Source : %s\n", folder);
    printf("[capp] Output : %s\n\n", out_file);

#ifdef _WIN32
    char tmp_zip[MAX_PATH];
    snprintf(tmp_zip, sizeof(tmp_zip), "%s.zip", app_name);
    snprintf(cmd, sizeof(cmd),
        "powershell -Command \"Compress-Archive -Path '%s\\*' -DestinationPath '%s' -Force\"",
        folder, tmp_zip);
    printf("[capp] Compressing...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Compression failed.\n");
        return 1;
    }
    snprintf(cmd, sizeof(cmd), "rename \"%s\" \"%s\"", tmp_zip, out_file);
    system(cmd);
#else
    snprintf(cmd, sizeof(cmd), ZIP_CMD, folder, out_file);
    printf("[capp] Compressing...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Compression failed. Is 'zip' installed?\n");
        return 1;
    }
#endif

    printf("\n[capp] Bundle created: %s\n", out_file);
    printf("[capp] To install, run: capp install %s\n", out_file);
    return 0;
}

/* ── Subcommand: install ───────────────────────────────────────────────────── */

static int cmd_install(const char *bundle) {
    if (!has_capp_ext(bundle)) {
        fprintf(stderr, "Error: '%s' does not have a .capp extension.\n", bundle);
        return 1;
    }

    char app_name[MAX_PATH];
    make_app_name(bundle, app_name, sizeof(app_name));

    /* Resolve extract_dir to an absolute path before the directory exists */
    char extract_dir[MAX_PATH];
    {
        char rel[MAX_PATH];
        snprintf(rel, sizeof(rel), "%s_capp_tmp", app_name);
#ifdef _WIN32
        if (!_fullpath(extract_dir, rel, sizeof(extract_dir)))
            snprintf(extract_dir, sizeof(extract_dir), "%s", rel);
#else
        char cwd[MAX_PATH];
        if (getcwd(cwd, sizeof(cwd)))
            snprintf(extract_dir, sizeof(extract_dir), "%s/%s", cwd, rel);
        else
            snprintf(extract_dir, sizeof(extract_dir), "%s", rel);
#endif
    }

    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;
    char bin_dir[MAX_PATH];
    if (!get_bin_dir(bin_dir, sizeof(bin_dir))) return 1;

    char cmd[MAX_CMD];

    printf("=== CAPP — Install ===\n");
    printf("[capp] Bundle   : %s\n", bundle);
    printf("[capp] App name : %s\n\n", app_name);

    /* Step 1: Ensure ~/.capp/{bundles,bin}/ exists */
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, bundles_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Could not create bundles directory '%s'.\n", bundles_dir);
        return 1;
    }
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, bin_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Could not create executable directory '%s'.\n", bin_dir);
        return 1;
    }

    /* Step 2: Extract */
    snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle, extract_dir);
    printf("[capp] Extracting bundle...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Extraction failed. Is 'unzip' installed?\n");
        return 1;
    }

    /* Step 3: Review install script */
    if (!review_script(extract_dir, INSTALL_SCRIPT)) {
        printf("[capp] Installation aborted by user.\n");
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 4: Run install script */
    snprintf(cmd, sizeof(cmd), RUN_INSTALL, extract_dir);
    printf("[capp] Running install script...\n");
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[capp] Install script exited with code %d.\n", ret);
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 5: Open instructions */
    if (open_instructions(extract_dir, app_name) != 0) {
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 6: Move bundle to ~/.capp/bundles/ */
    char dest_path[MAX_PATH];
    snprintf(dest_path, sizeof(dest_path), "%s%s%s.capp", bundles_dir, PATH_SEP, app_name);
    snprintf(cmd, sizeof(cmd), MOVE_CMD, bundle, dest_path);
    printf("[capp] Storing bundle in: %s\n", dest_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "Warning: Could not move bundle to '%s'.\n", dest_path);
        fprintf(stderr, "         You may need to move '%s' manually.\n", bundle);
    }

    /* Step 7: Add to installed.txt */
    add_to_installed(app_name);

    /* Step 8: Clean up temp dir */
    printf("[capp] Cleaning up...\n");
    snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Warning: Could not remove temporary directory '%s'.\n", extract_dir);
    }

    printf("[capp] Installation complete!\n");
    printf("[capp] To uninstall, run: capp uninstall %s\n", app_name);
#ifdef _WIN32
    printf("[capp] Add '%s' to PATH in your PowerShell profile ($PROFILE).\n", bin_dir);
#else
    printf("[capp] Add '%s' to PATH in your ~/.bashrc.\n", bin_dir);
#endif
    return 0;
}

/* ── Subcommand: install-remote ────────────────────────────────────────────── */

/*
 * Download a package from mirror and install it.
 * Automatically fetches the latest version.
 * Usage: capp install-remote <AppName>
 */
static int cmd_install_remote(const char *pkg_name) {
    Mirror *mirrors = get_mirrors();
    char *packages_list = NULL;
    PackageInfo *pkg_info = NULL;
    char temp_capp[MAX_PATH];
    char cmd[MAX_CMD];
    int success = 0;

    printf("=== CAPP — Install from Mirror ===\n");
    printf("[capp] Package : %s\n", pkg_name);
    printf("[capp] Version : (latest)\n\n");

    /* Try each mirror */
    for (int i = 0; mirrors[i].url[0] != '\0'; i++) {
        const char *mirror = mirrors[i].url;
        
        /* Fetch packages.txt */
        packages_list = fetch_packages_list(mirror);
        if (!packages_list) continue;
        
        /* Find the package in the list (NULL version = latest) */
        pkg_info = find_package_in_list(packages_list, pkg_name, NULL);
        
        if (pkg_info) {
            /* Found it! Now download the .capp file */
            printf("[capp] Found version: %s\n", pkg_info->version);
            
            const char *home = getenv(HOME_ENV);
            if (!home) home = ".";
            
#ifdef _WIN32
            snprintf(temp_capp, sizeof(temp_capp), "%s\\.capp\\%s", home, pkg_info->filename);
#else
            snprintf(temp_capp, sizeof(temp_capp), "%s/.capp/%s", home, pkg_info->filename);
#endif
            
            if (download_capp_file(mirror, pkg_info->filename, temp_capp)) {
                printf("[capp] Successfully downloaded from: %s\n\n", mirror);
                success = 1;
                break;
            }
        }
        
        free(packages_list);
        if (pkg_info) {
            free(pkg_info->version);
            free(pkg_info->filename);
            free(pkg_info);
        }
        packages_list = NULL;
        pkg_info = NULL;
    }

    if (!success) {
        fprintf(stderr, "[capp] Package '%s' not found on any mirror.\n", pkg_name);
        return 1;
    }

    /* Now install the downloaded .capp file */
    int ret = cmd_install(temp_capp);
    
    /* Clean up temp file */
    snprintf(cmd, sizeof(cmd), REMOVE_FILE_CMD, temp_capp);
    system(cmd);
    
    free(packages_list);
    if (pkg_info) {
        free(pkg_info->version);
        free(pkg_info->filename);
        free(pkg_info);
    }
    
    return ret;
}

/* ── Subcommand: uninstall ─────────────────────────────────────────────────── */

static int cmd_uninstall(const char *arg) {
    char app_name[MAX_PATH];
    strncpy(app_name, arg, sizeof(app_name) - 1);
    app_name[sizeof(app_name) - 1] = '\0';
    strip_capp_ext(app_name);

    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

    char bundle_path[MAX_PATH];
    snprintf(bundle_path, sizeof(bundle_path), "%s%s%s.capp",
             bundles_dir, PATH_SEP, app_name);

    char extract_dir[MAX_PATH];
    snprintf(extract_dir, sizeof(extract_dir), "%s%s%s_capp_tmp",
             bundles_dir, PATH_SEP, app_name);

    char cmd[MAX_CMD];

    printf("=== CAPP — Uninstall ===\n");
    printf("[capp] App    : %s\n", app_name);
    printf("[capp] Bundle : %s\n\n", bundle_path);

    /* Step 1: Verify app is installed */
    if (!is_app_installed(app_name)) {
        fprintf(stderr, "Error: '%s' is not installed.\n", app_name);
        fprintf(stderr, "       (Not found in installed.txt)\n");
        return 1;
    }

    /* Step 2: Verify bundle file exists */
    FILE *test = fopen(bundle_path, "rb");
    if (!test) {
        fprintf(stderr, "Error: Bundle not found at '%s'.\n", bundle_path);
        fprintf(stderr, "       The app is registered but the bundle file is missing.\n");
        fprintf(stderr, "       You may need to manually clean up installed files.\n");
        return 1;
    }
    fclose(test);

    /* Step 3: Confirm */
    {
        char answer[8];
        printf("[capp] Are you sure you want to uninstall '%s'? [y/N]: ", app_name);
        fflush(stdout);
        if (!fgets(answer, sizeof(answer), stdin)) return 1;
        answer[strcspn(answer, "\r\n")] = '\0';
        if (answer[0] != 'y' && answer[0] != 'Y') {
            printf("[capp] Uninstall cancelled.\n");
            return 0;
        }
    }

    /* Step 4: Extract */
    snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle_path, extract_dir);
    printf("\n[capp] Extracting bundle...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Extraction failed. Is 'unzip' installed?\n");
        return 1;
    }

    /* Step 5: Review uninstall script */
    if (!review_script(extract_dir, UNINSTALL_SCRIPT)) {
        printf("[capp] Uninstallation aborted by user.\n");
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 6: Run uninstall script */
    snprintf(cmd, sizeof(cmd), RUN_UNINSTALL, extract_dir);
    printf("[capp] Running uninstall script...\n");
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[capp] Uninstall script exited with code %d.\n", ret);
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    /* Step 7: Clean up temp dir */
    printf("[capp] Cleaning up...\n");
    snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
    system(cmd);

    /* Step 8: Remove stored bundle */
    snprintf(cmd, sizeof(cmd), REMOVE_FILE_CMD, bundle_path);
    printf("[capp] Removing stored bundle...\n");
    if (system(cmd) != 0)
        fprintf(stderr, "Warning: Could not remove bundle '%s'.\n", bundle_path);

    /* Step 9: Remove from installed.txt */
    remove_from_installed(app_name);

    printf("[capp] '%s' has been uninstalled.\n", app_name);
    return 0;
}

/* ── Subcommand: clear-cache ──────────────────────────────────────────────── */

static int cmd_clear_cache(void) {
    char cache_dir[MAX_PATH];
    char cmd[MAX_CMD];

    if (!get_cache_dir(cache_dir, sizeof(cache_dir))) return 1;

    printf("=== CAPP — Clear Cache ===\n");
    printf("[capp] Cache directory: %s\n\n", cache_dir);

    snprintf(cmd, sizeof(cmd), RMDIR_CMD, cache_dir);
    
    if (system(cmd) != 0) {
        fprintf(stderr, "[capp] Warning: Could not remove cache directory.\n");
        return 1;
    }

    printf("[capp] Cache cleared successfully.\n");
    return 0;
}

/* ── Entry point ───────────────────────────────────────────────────────────── */

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s create                     — bundle a folder into a .capp file\n", prog);
    fprintf(stderr, "  %s install  <App.capp>        — install a .capp bundle\n", prog);
    fprintf(stderr, "  %s install-remote <AppName>   — install latest version from mirror\n", prog);
    fprintf(stderr, "  %s uninstall <AppName>        — uninstall a previously installed app\n", prog);
    fprintf(stderr, "  %s clear-cache                — clear instructions cache\n", prog);
    fprintf(stderr, "\nEnvironment variables:\n");
    fprintf(stderr, "  CAPP_MIRROR=https://...  — override default mirror URL\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "create") == 0) {
        if (argc != 2) {
            fprintf(stderr, "Usage: %s create\n", argv[0]);
            return 1;
        }
        return cmd_create();
    }

    if (strcmp(subcmd, "install") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s install <App.capp>\n", argv[0]);
            return 1;
        }
        return cmd_install(argv[2]);
    }

    if (strcmp(subcmd, "install-remote") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s install-remote <AppName>\n", argv[0]);
            return 1;
        }
        return cmd_install_remote(argv[2]);
    }

    if (strcmp(subcmd, "uninstall") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s uninstall <AppName>\n", argv[0]);
            return 1;
        }
        return cmd_uninstall(argv[2]);
    }

    if (strcmp(subcmd, "clear-cache") == 0) {
        if (argc != 2) {
            fprintf(stderr, "Usage: %s clear-cache\n", argv[0]);
            return 1;
        }
        return cmd_clear_cache();
    }

    fprintf(stderr, "Unknown subcommand: '%s'\n\n", subcmd);
    print_usage(argv[0]);
    return 1;
}

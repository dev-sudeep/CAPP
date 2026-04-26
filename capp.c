/*
 * capp.c - Compact App (CAPP) utility with mirror support
 *
 * Compile:
 *   Linux/macOS : gcc -o capp capp.c
 *   Windows     : gcc -o capp.exe capp.c
 *
 * Usage:
 *   capp create                            — interactively bundle a folder into a .capp
 *   capp install   <App.capp>              — install a bundle from local file
 *   capp install-remote <AppName>          — install latest from mirror
 *   capp uninstall <AppName>               — uninstall a previously installed app
 *   capp update                            — refresh available.txt from all mirrors, show upgrades
 *   capp upgrade [AppName]                 — upgrade one or all installed packages
 *   capp search <query>                    — search available packages
 *   capp show <AppName>                    — show metadata for a package
 *   capp man <AppName>                     — open instructions for an installed app
 *   capp clear-cache                       — remove cached instructions files (keeps metadata)
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
  #define DATA_SUBDIR       "\\.capp\\data"

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
  #define COPY_CMD          "copy /Y \"%s\" \"%s\" > nul"

  /* grep equivalent */
  #define GREP_CMD          "findstr /I /C:\"%s\" \"%s\""

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
  #define DATA_SUBDIR       "/.capp/data"

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
  #define COPY_CMD          "cp '%s' '%s'"

  /* grep */
  #define GREP_CMD          "grep -i '%s' '%s'"

  /* scripts */
  #define INSTALL_SCRIPT    "install.sh"
  #define UNINSTALL_SCRIPT  "uninstall.sh"
  #define RUN_INSTALL       "bash '%s" PATH_SEP INSTALL_SCRIPT "'"
  #define RUN_UNINSTALL     "bash '%s" PATH_SEP UNINSTALL_SCRIPT "'"
#endif

#define MAX_PATH   512
#define MAX_CMD   2048
#define SAMPLE_BYTES 512

/* ── Mirror configuration ──────────────────────────────────────────────────── */

typedef struct {
    char url[MAX_PATH];
} Mirror;

static Mirror DEFAULT_MIRRORS[] = {
    {"https://raw.githubusercontent.com/dev-sudeep/CAPP-mirror/refs/heads/main"},
    {""} /* sentinel */
};

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

static const char *basename_of(const char *path) {
    const char *p;
#ifdef _WIN32
    if ((p = strrchr(path, '\\'))) return p + 1;
#endif
    if ((p = strrchr(path, '/'))) return p + 1;
    return path;
}

static int has_capp_ext(const char *name) {
    size_t len = strlen(name);
    return len > 5 && strcmp(name + len - 5, ".capp") == 0;
}

static void strip_capp_ext(char *name) {
    size_t len = strlen(name);
    if (len > 5 && strcmp(name + len - 5, ".capp") == 0)
        name[len - 5] = '\0';
}

static void make_app_name(const char *bundle, char *out, size_t out_sz) {
    const char *base = basename_of(bundle);
    size_t base_len = strlen(base) - 5;
    size_t copy_len = base_len < out_sz - 1 ? base_len : out_sz - 1;
    strncpy(out, base, copy_len);
    out[copy_len] = '\0';
}

static int get_bundles_dir(char *out, size_t out_sz) {
    const char *home = getenv(HOME_ENV);
    if (!home || strlen(home) == 0) {
        fprintf(stderr, "Error: $" HOME_ENV " is not set.\n");
        return 0;
    }
    snprintf(out, out_sz, "%s%s", home, BUNDLES_SUBDIR);
    return 1;
}

static int get_bin_dir(char *out, size_t out_sz) {
    const char *home = getenv(HOME_ENV);
    if (!home || strlen(home) == 0) {
        fprintf(stderr, "Error: $" HOME_ENV " is not set.\n");
        return 0;
    }
    snprintf(out, out_sz, "%s%s", home, BIN_SUBDIR);
    return 1;
}

static int get_data_dir(char *out, size_t out_sz) {
    const char *home = getenv(HOME_ENV);
    if (!home || strlen(home) == 0) {
        fprintf(stderr, "Error: $" HOME_ENV " is not set.\n");
        return 0;
    }
    snprintf(out, out_sz, "%s%s", home, DATA_SUBDIR);
    return 1;
}

static int get_app_data_dir(const char *app_name, char *out, size_t out_sz) {
    char data_dir[MAX_PATH];
    if (!get_data_dir(data_dir, sizeof(data_dir))) return 0;
    snprintf(out, out_sz, "%s%s%s", data_dir, PATH_SEP, app_name);
    return 1;
}

static void strip_trailing_sep(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '/' || s[len - 1] == '\\'))
        s[--len] = '\0';
}

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
        if (strcmp(line, app_name) == 0) { found = 1; break; }
    }
    fclose(f);
    return found;
}

static void add_to_installed(const char *app_name) {
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return;

    char installed_path[MAX_PATH];
    snprintf(installed_path, sizeof(installed_path), "%s%sinstalled.txt",
             bundles_dir, PATH_SEP);

    FILE *f = fopen(installed_path, "a");
    if (!f) { fprintf(stderr, "[capp] Warning: Could not update installed.txt.\n"); return; }
    fprintf(f, "%s\n", app_name);
    fclose(f);
}

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
    if (!out) { fclose(in); fprintf(stderr, "[capp] Warning: Could not update installed.txt.\n"); return; }

    char line[MAX_PATH];
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, app_name) != 0) fprintf(out, "%s\n", line);
    }
    fclose(in); fclose(out);

    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), MOVE_CMD, temp_path, installed_path);
    if (system(cmd) != 0)
        fprintf(stderr, "[capp] Warning: Could not update installed.txt.\n");
}

/* ── Executable / binary detection ────────────────────────────────────────── */

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

/* ── Version helpers ───────────────────────────────────────────────────────── */

typedef struct { int major, minor, patch; } VersionParts;

static VersionParts parse_version(const char *v) {
    VersionParts p = {0, 0, 0};
    sscanf(v, "%d.%d.%d", &p.major, &p.minor, &p.patch);
    return p;
}

static int compare_versions(const char *v1s, const char *v2s) {
    VersionParts v1 = parse_version(v1s);
    VersionParts v2 = parse_version(v2s);
    if (v1.major != v2.major) return v1.major - v2.major;
    if (v1.minor != v2.minor) return v1.minor - v2.minor;
    return v1.patch - v2.patch;
}

/* ── Metadata helpers ──────────────────────────────────────────────────────── */

/*
 * Write a metadata.json for an app into its data directory.
 * Fields: name, version, author, description.
 * All values are taken from the extract_dir/metadata.json if present,
 * otherwise we populate only what we know (name + version from packages list).
 */
static void save_metadata(const char *app_name, const char *version,
                           const char *extract_dir) {
    char app_data_dir[MAX_PATH];
    if (!get_app_data_dir(app_name, app_data_dir, sizeof(app_data_dir))) return;

    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, app_data_dir);
    system(cmd);

    /* Check if bundle provides its own metadata.json */
    char src_meta[MAX_PATH];
    snprintf(src_meta, sizeof(src_meta), "%s%smetadata.json", extract_dir, PATH_SEP);
    char dst_meta[MAX_PATH];
    snprintf(dst_meta, sizeof(dst_meta), "%s%smetadata.json", app_data_dir, PATH_SEP);

    FILE *f = fopen(src_meta, "r");
    if (f) {
        fclose(f);
        /* Copy provided metadata */
        snprintf(cmd, sizeof(cmd), COPY_CMD, src_meta, dst_meta);
        if (system(cmd) != 0)
            fprintf(stderr, "[capp] Warning: Could not copy metadata.json.\n");
        return;
    }

    /* Generate minimal metadata */
    FILE *out = fopen(dst_meta, "w");
    if (!out) {
        fprintf(stderr, "[capp] Warning: Could not write metadata.json.\n");
        return;
    }
    fprintf(out, "{\n");
    fprintf(out, "  \"name\": \"%s\",\n", app_name);
    fprintf(out, "  \"version\": \"%s\",\n", version ? version : "unknown");
    fprintf(out, "  \"author\": \"unknown\",\n");
    fprintf(out, "  \"description\": \"\"\n");
    fprintf(out, "}\n");
    fclose(out);
}

/*
 * Read a JSON string value for a given key from a simple flat JSON file.
 * Only handles the pattern: "key": "value"
 * out must be at least out_sz bytes. Returns 1 on success.
 */
static int json_get_string(const char *json, const char *key, char *out, size_t out_sz) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) { out[0] = '\0'; return 0; }
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') { out[0] = '\0'; return 0; }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        if (*p == '\\') { p++; if (!*p) break; }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/*
 * Read the installed version of an app from its metadata.json.
 * Returns 1 on success.
 */
static int get_installed_version(const char *app_name, char *ver_out, size_t ver_sz) {
    char app_data_dir[MAX_PATH];
    if (!get_app_data_dir(app_name, app_data_dir, sizeof(app_data_dir))) return 0;

    char meta_path[MAX_PATH];
    snprintf(meta_path, sizeof(meta_path), "%s%smetadata.json", app_data_dir, PATH_SEP);

    FILE *f = fopen(meta_path, "r");
    if (!f) { ver_out[0] = '\0'; return 0; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    fclose(f);
    buf[sz] = '\0';

    int ok = json_get_string(buf, "version", ver_out, ver_sz);
    free(buf);
    return ok;
}

/* ── Instructions file ─────────────────────────────────────────────────────── */

/*
 * Find and open instructions from extract_dir.
 * Saves it (and metadata) into ~/.capp/data/<app_name>/.
 */
static int open_instructions(const char *extract_dir, const char *app_name,
                              const char *version) {
    char found_path[MAX_PATH];
    char app_data_dir[MAX_PATH];
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
        save_metadata(app_name, version, extract_dir);
        return 0;
    }
    snprintf(found_path, sizeof(found_path), "%s\\%s", extract_dir, fd.cFileName);
    FindClose(h);
#else
    DIR *dir = opendir(extract_dir);
    if (!dir) {
        fprintf(stderr, "[capp] Warning: Could not scan bundle directory.\n");
        save_metadata(app_name, version, extract_dir);
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
        save_metadata(app_name, version, extract_dir);
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

    /* Ensure data dir exists */
    if (!get_app_data_dir(app_name, app_data_dir, sizeof(app_data_dir))) return 0;
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, app_data_dir);
    system(cmd);

    /* Save metadata first */
    save_metadata(app_name, version, extract_dir);

    /* Copy instructions */
    snprintf(cached_path, sizeof(cached_path), "%s%s%s",
             app_data_dir, PATH_SEP, basename_of(found_path));
    snprintf(cmd, sizeof(cmd), COPY_CMD, found_path, cached_path);
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

/* ── Package list helpers ──────────────────────────────────────────────────── */

typedef struct {
    char *version;
    char *filename;
} PackageInfo;

static PackageInfo* find_package_in_list(const char *packages_txt,
                                          const char *pkg_name,
                                          const char *pkg_version) {
    char *list_copy = malloc(strlen(packages_txt) + 1);
    strcpy(list_copy, packages_txt);

    PackageInfo *best_match = NULL;
    char *line = strtok(list_copy, "\n");

    while (line) {
        char name[256], version[256], filename[256];
        if (sscanf(line, "%255[^|]|%255[^|]|%255s", name, version, filename) >= 3) {
            if (pkg_version) {
                if (strcmp(name, pkg_name) == 0 && strcmp(version, pkg_version) == 0) {
                    if (best_match) { free(best_match->version); free(best_match->filename); free(best_match); }
                    best_match = malloc(sizeof(PackageInfo));
                    best_match->version  = malloc(strlen(version)  + 1); strcpy(best_match->version,  version);
                    best_match->filename = malloc(strlen(filename)  + 1); strcpy(best_match->filename, filename);
                    break;
                }
            } else {
                if (strcmp(name, pkg_name) == 0) {
                    if (!best_match || compare_versions(version, best_match->version) > 0) {
                        if (best_match) { free(best_match->version); free(best_match->filename); free(best_match); }
                        best_match = malloc(sizeof(PackageInfo));
                        best_match->version  = malloc(strlen(version)  + 1); strcpy(best_match->version,  version);
                        best_match->filename = malloc(strlen(filename)  + 1); strcpy(best_match->filename, filename);
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
 * Returns allocated string (caller must free), or NULL on failure.
 */
static char* fetch_packages_list(const char *mirror_url) {
    char curl_url[MAX_PATH];
    char temp_file[MAX_PATH];
    char temp_dir[MAX_PATH];
    char cmd[MAX_CMD];

    const char *home = getenv(HOME_ENV);
    if (!home) home = ".";

#ifdef _WIN32
    snprintf(temp_dir,  sizeof(temp_dir),  "%s\\.capp", home);
    snprintf(temp_file, sizeof(temp_file), "%s\\.capp\\packages.tmp", home);
#else
    snprintf(temp_dir,  sizeof(temp_dir),  "%s/.capp", home);
    snprintf(temp_file, sizeof(temp_file), "%s/.capp/packages.tmp", home);
#endif

    snprintf(cmd, sizeof(cmd), MKDIR_CMD, temp_dir);
    system(cmd);

    if (system("curl --version > /dev/null 2>&1") != 0) {
        fprintf(stderr, "[capp] Error: curl is not installed.\n");
        return NULL;
    }

    snprintf(curl_url, sizeof(curl_url), "%s/packages.txt", mirror_url);
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "curl -s -o \"%s\" \"%s\"", temp_file, curl_url);
#else
    snprintf(cmd, sizeof(cmd), "curl -s -o '%s' '%s'", temp_file, curl_url);
#endif

    printf("[capp] Fetching package list from: %s\n", mirror_url);

    if (system(cmd) != 0) {
        fprintf(stderr, "[capp] Error: Failed to fetch from mirror.\n");
        return NULL;
    }

    FILE *f = fopen(temp_file, "r");
    if (!f) { fprintf(stderr, "[capp] Error: Could not read downloaded file.\n"); return NULL; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size == 0) {
        fprintf(stderr, "[capp] Error: Downloaded file is empty.\n");
        fclose(f); return NULL;
    }

    char *content = malloc(size + 1);
    size_t bytes_read = fread(content, 1, size, f);
    fclose(f);
    content[bytes_read] = '\0';

    if (strncmp(content, "<!DOCTYPE", 9) == 0 || strncmp(content, "<html", 5) == 0 ||
        strstr(content, "404") || strstr(content, "400") ||
        strstr(content, "403") || strstr(content, "500")) {
        fprintf(stderr, "[capp] Error: HTTP error from mirror.\n");
        free(content); return NULL;
    }

#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "del /f /q \"%s\"", temp_file);
#else
    snprintf(cmd, sizeof(cmd), "rm -f '%s'", temp_file);
#endif
    system(cmd);

    return content;
}

/*
 * Download a .capp file from mirror to dest_path.
 */
static int download_capp_file(const char *mirror_url,
                               const char *filename,
                               const char *dest_path) {
    char curl_url[MAX_PATH];
    char cmd[MAX_CMD];

    snprintf(curl_url, sizeof(curl_url), "%s/packages/%s", mirror_url, filename);
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "curl -s -f -o \"%s\" \"%s\"", dest_path, curl_url);
#else
    snprintf(cmd, sizeof(cmd), "curl -s -f -o '%s' '%s'", dest_path, curl_url);
#endif

    printf("[capp] Downloading: %s\n", filename);
    return system(cmd) == 0;
}

/* ── Subcommand: create ────────────────────────────────────────────────────── */

static int cmd_create(void) {
    char folder[MAX_PATH];
    char app_name[MAX_PATH];
    char out_file[MAX_PATH];
    char cmd[MAX_CMD];

    printf("=== CAPP — Create Bundle ===\n\n");

    printf("Source folder path: ");
    if (!fgets(folder, sizeof(folder), stdin)) { fprintf(stderr, "Error reading input.\n"); return 1; }
    folder[strcspn(folder, "\r\n")] = '\0';
    strip_trailing_sep(folder);
    if (strlen(folder) == 0) { fprintf(stderr, "Error: Folder path cannot be empty.\n"); return 1; }

    printf("Application name (without .capp): ");
    if (!fgets(app_name, sizeof(app_name), stdin)) { fprintf(stderr, "Error reading input.\n"); return 1; }
    app_name[strcspn(app_name, "\r\n")] = '\0';
    if (strlen(app_name) == 0) { fprintf(stderr, "Error: Application name cannot be empty.\n"); return 1; }

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
    if (system(cmd) != 0) { fprintf(stderr, "Error: Compression failed.\n"); return 1; }
    snprintf(cmd, sizeof(cmd), "rename \"%s\" \"%s\"", tmp_zip, out_file);
    system(cmd);
#else
    snprintf(cmd, sizeof(cmd), ZIP_CMD, folder, out_file);
    printf("[capp] Compressing...\n");
    if (system(cmd) != 0) { fprintf(stderr, "Error: Compression failed. Is 'zip' installed?\n"); return 1; }
#endif

    printf("\n[capp] Bundle created: %s\n", out_file);
    printf("[capp] To install, run: capp install %s\n", out_file);
    return 0;
}

/* ── Subcommand: install ───────────────────────────────────────────────────── */

static int cmd_install_with_version(const char *bundle, const char *version) {
    if (!has_capp_ext(bundle)) {
        fprintf(stderr, "Error: '%s' does not have a .capp extension.\n", bundle);
        return 1;
    }

    char app_name[MAX_PATH];
    make_app_name(bundle, app_name, sizeof(app_name));

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

    snprintf(cmd, sizeof(cmd), MKDIR_CMD, bundles_dir);
    if (system(cmd) != 0) { fprintf(stderr, "Error: Could not create bundles directory.\n"); return 1; }
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, bin_dir);
    if (system(cmd) != 0) { fprintf(stderr, "Error: Could not create bin directory.\n"); return 1; }

    snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle, extract_dir);
    printf("[capp] Extracting bundle...\n");
    if (system(cmd) != 0) { fprintf(stderr, "Error: Extraction failed. Is 'unzip' installed?\n"); return 1; }

    if (!review_script(extract_dir, INSTALL_SCRIPT)) {
        printf("[capp] Installation aborted by user.\n");
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    snprintf(cmd, sizeof(cmd), RUN_INSTALL, extract_dir);
    printf("[capp] Running install script...\n");
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[capp] Install script exited with code %d.\n", ret);
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    if (open_instructions(extract_dir, app_name, version) != 0) {
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    char dest_path[MAX_PATH];
    snprintf(dest_path, sizeof(dest_path), "%s%s%s.capp", bundles_dir, PATH_SEP, app_name);
    snprintf(cmd, sizeof(cmd), MOVE_CMD, bundle, dest_path);
    printf("[capp] Storing bundle in: %s\n", dest_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "Warning: Could not move bundle to '%s'.\n", dest_path);
        fprintf(stderr, "         You may need to move '%s' manually.\n", bundle);
    }

    add_to_installed(app_name);

    printf("[capp] Cleaning up...\n");
    snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
    system(cmd);

    printf("[capp] Installation complete!\n");
    printf("[capp] To uninstall, run: capp uninstall %s\n", app_name);
#ifdef _WIN32
    printf("[capp] Add '%s' to PATH in your PowerShell profile ($PROFILE).\n", bin_dir);
#else
    printf("[capp] Add '%s' to PATH in your ~/.bashrc.\n", bin_dir);
#endif
    return 0;
}

static int cmd_install(const char *bundle) {
    return cmd_install_with_version(bundle, NULL);
}

/* ── Subcommand: install-remote ────────────────────────────────────────────── */

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

    for (int i = 0; mirrors[i].url[0] != '\0'; i++) {
        const char *mirror = mirrors[i].url;

        packages_list = fetch_packages_list(mirror);
        if (!packages_list) continue;

        pkg_info = find_package_in_list(packages_list, pkg_name, NULL);

        if (pkg_info) {
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
        if (pkg_info) { free(pkg_info->version); free(pkg_info->filename); free(pkg_info); }
        packages_list = NULL;
        pkg_info = NULL;
    }

    if (!success) {
        fprintf(stderr, "[capp] Package '%s' not found on any mirror.\n", pkg_name);
        return 1;
    }

    char version_copy[256];
    strncpy(version_copy, pkg_info->version, sizeof(version_copy) - 1);
    version_copy[sizeof(version_copy) - 1] = '\0';

    int ret = cmd_install_with_version(temp_capp, version_copy);

    snprintf(cmd, sizeof(cmd), REMOVE_FILE_CMD, temp_capp);
    system(cmd);

    free(packages_list);
    if (pkg_info) { free(pkg_info->version); free(pkg_info->filename); free(pkg_info); }

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

    if (!is_app_installed(app_name)) {
        fprintf(stderr, "Error: '%s' is not installed.\n", app_name);
        return 1;
    }

    FILE *test = fopen(bundle_path, "rb");
    if (!test) {
        fprintf(stderr, "Error: Bundle not found at '%s'.\n", bundle_path);
        return 1;
    }
    fclose(test);

    {
        char answer[8];
        printf("[capp] Are you sure you want to uninstall '%s'? [y/N]: ", app_name);
        fflush(stdout);
        if (!fgets(answer, sizeof(answer), stdin)) return 1;
        answer[strcspn(answer, "\r\n")] = '\0';
        if (answer[0] != 'y' && answer[0] != 'Y') { printf("[capp] Uninstall cancelled.\n"); return 0; }
    }

    snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle_path, extract_dir);
    printf("\n[capp] Extracting bundle...\n");
    if (system(cmd) != 0) { fprintf(stderr, "Error: Extraction failed.\n"); return 1; }

    if (!review_script(extract_dir, UNINSTALL_SCRIPT)) {
        printf("[capp] Uninstallation aborted by user.\n");
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    snprintf(cmd, sizeof(cmd), RUN_UNINSTALL, extract_dir);
    printf("[capp] Running uninstall script...\n");
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[capp] Uninstall script exited with code %d.\n", ret);
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return 1;
    }

    printf("[capp] Cleaning up...\n");
    snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
    system(cmd);

    snprintf(cmd, sizeof(cmd), REMOVE_FILE_CMD, bundle_path);
    printf("[capp] Removing stored bundle...\n");
    if (system(cmd) != 0)
        fprintf(stderr, "Warning: Could not remove bundle.\n");

    remove_from_installed(app_name);
    printf("[capp] '%s' has been uninstalled.\n", app_name);
    return 0;
}

/* ── Subcommand: update ────────────────────────────────────────────────────── */

/*
 * Fetches packages.txt from every mirror, merges them (deduplicating by
 * name+version), writes ~/.capp/bundles/available.txt, then reports which
 * installed packages have a newer version available.
 */
static int cmd_update(void) {
    Mirror *mirrors = get_mirrors();
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

    char available_path[MAX_PATH];
    snprintf(available_path, sizeof(available_path), "%s%savailable.txt",
             bundles_dir, PATH_SEP);

    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), MKDIR_CMD, bundles_dir);
    system(cmd);

    printf("=== CAPP — Update ===\n");

    /* Accumulate all lines across mirrors */
    size_t total_alloc = 4096;
    char *merged = malloc(total_alloc);
    merged[0] = '\0';
    size_t merged_len = 0;

    for (int i = 0; mirrors[i].url[0] != '\0'; i++) {
        char *list = fetch_packages_list(mirrors[i].url);
        if (!list) continue;

        /* Append each line if not already present (name|version duplicate check) */
        char *list_copy = malloc(strlen(list) + 1);
        strcpy(list_copy, list);
        char *line = strtok(list_copy, "\n");

        while (line) {
            /* Skip empty / comment lines */
            if (strlen(line) == 0 || line[0] == '#') { line = strtok(NULL, "\n"); continue; }

            /* Check duplicate: look for exact line already in merged */
            if (!strstr(merged, line)) {
                size_t needed = merged_len + strlen(line) + 2;
                if (needed > total_alloc) {
                    total_alloc = needed * 2;
                    merged = realloc(merged, total_alloc);
                }
                strcpy(merged + merged_len, line);
                merged_len += strlen(line);
                merged[merged_len++] = '\n';
                merged[merged_len]   = '\0';
            }
            line = strtok(NULL, "\n");
        }
        free(list_copy);
        free(list);
    }

    if (merged_len == 0) {
        fprintf(stderr, "[capp] No package data fetched from any mirror.\n");
        free(merged);
        return 1;
    }

    /* Write available.txt */
    FILE *out = fopen(available_path, "w");
    if (!out) {
        fprintf(stderr, "[capp] Error: Could not write available.txt.\n");
        free(merged);
        return 1;
    }
    fwrite(merged, 1, merged_len, out);
    fclose(out);
    printf("[capp] Wrote %s\n", available_path);

    /* Report upgradeable packages */
    char installed_path[MAX_PATH];
    snprintf(installed_path, sizeof(installed_path), "%s%sinstalled.txt",
             bundles_dir, PATH_SEP);

    FILE *inst = fopen(installed_path, "r");
    if (!inst) {
        printf("[capp] No installed packages found.\n");
        free(merged);
        return 0;
    }

    printf("\n[capp] Checking for upgrades...\n\n");
    int upgrades_found = 0;

    char app_name[MAX_PATH];
    while (fgets(app_name, sizeof(app_name), inst)) {
        app_name[strcspn(app_name, "\r\n")] = '\0';
        if (strlen(app_name) == 0) continue;

        char cur_ver[64];
        if (!get_installed_version(app_name, cur_ver, sizeof(cur_ver)))
            strcpy(cur_ver, "unknown");

        PackageInfo *info = find_package_in_list(merged, app_name, NULL);
        if (info) {
            if (strcmp(cur_ver, "unknown") == 0 ||
                compare_versions(info->version, cur_ver) > 0) {
                printf("  %-24s  %s  →  %s\n", app_name, cur_ver, info->version);
                upgrades_found = 1;
            } else {
                printf("  %-24s  %s  (up to date)\n", app_name, cur_ver);
            }
            free(info->version); free(info->filename); free(info);
        } else {
            printf("  %-24s  %s  (not found on any mirror)\n", app_name, cur_ver);
        }
    }
    fclose(inst);

    if (!upgrades_found)
        printf("\n[capp] All packages are up to date.\n");
    else
        printf("\n[capp] Run 'capp upgrade' to upgrade all, or 'capp upgrade <AppName>' for one.\n");

    free(merged);
    return 0;
}

/* ── Subcommand: upgrade ───────────────────────────────────────────────────── */

/*
 * Upgrade a single package by name. Internally: uninstall (skip prompt) + install-remote.
 * Returns 0 on success.
 */
static int upgrade_single(const char *app_name, const char *available_data) {
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

    char cur_ver[64];
    if (!get_installed_version(app_name, cur_ver, sizeof(cur_ver)))
        strcpy(cur_ver, "unknown");

    PackageInfo *info = find_package_in_list(available_data, app_name, NULL);
    if (!info) {
        fprintf(stderr, "[capp] '%s' not found on any mirror.\n", app_name);
        return 1;
    }

    if (strcmp(cur_ver, "unknown") != 0 &&
        compare_versions(info->version, cur_ver) <= 0) {
        printf("[capp] '%s' is already at the latest version (%s).\n", app_name, cur_ver);
        free(info->version); free(info->filename); free(info);
        return 0;
    }

    printf("\n[capp] Upgrading '%s'  %s  →  %s\n\n", app_name, cur_ver, info->version);
    free(info->version); free(info->filename); free(info);

    /* Silently uninstall: re-use cmd_uninstall but we need to bypass the y/N prompt.
       We call the individual steps directly. */
    char bundle_path[MAX_PATH];
    snprintf(bundle_path, sizeof(bundle_path), "%s%s%s.capp",
             bundles_dir, PATH_SEP, app_name);

    char extract_dir[MAX_PATH];
    snprintf(extract_dir, sizeof(extract_dir), "%s%s%s_upg_tmp",
             bundles_dir, PATH_SEP, app_name);

    char cmd[MAX_CMD];

    /* Check bundle exists */
    FILE *t = fopen(bundle_path, "rb");
    if (!t) {
        fprintf(stderr, "[capp] Warning: Stored bundle missing; skipping old uninstall.\n");
    } else {
        fclose(t);
        /* Extract for uninstall */
        snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle_path, extract_dir);
        if (system(cmd) == 0) {
            if (review_script(extract_dir, UNINSTALL_SCRIPT)) {
                snprintf(cmd, sizeof(cmd), RUN_UNINSTALL, extract_dir);
                system(cmd);
            } else {
                printf("[capp] Skipped uninstall script.\n");
            }
        }
        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);

        /* Remove old bundle */
        snprintf(cmd, sizeof(cmd), REMOVE_FILE_CMD, bundle_path);
        system(cmd);
    }

    remove_from_installed(app_name);

    /* Now install latest from mirror */
    return cmd_install_remote(app_name);
}

static int cmd_upgrade(const char *app_name_arg) {
    /* Load available.txt */
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

    char available_path[MAX_PATH];
    snprintf(available_path, sizeof(available_path), "%s%savailable.txt",
             bundles_dir, PATH_SEP);

    FILE *af = fopen(available_path, "r");
    if (!af) {
        fprintf(stderr, "[capp] available.txt not found. Run 'capp update' first.\n");
        return 1;
    }
    fseek(af, 0, SEEK_END);
    long asz = ftell(af);
    fseek(af, 0, SEEK_SET);
    char *available_data = malloc(asz + 1);
    fread(available_data, 1, asz, af);
    fclose(af);
    available_data[asz] = '\0';

    int ret = 0;

    if (app_name_arg) {
        /* Upgrade single package */
        printf("=== CAPP — Upgrade '%s' ===\n", app_name_arg);
        if (!is_app_installed(app_name_arg)) {
            fprintf(stderr, "[capp] Error: '%s' is not installed.\n", app_name_arg);
            free(available_data);
            return 1;
        }
        ret = upgrade_single(app_name_arg, available_data);
    } else {
        /* Upgrade all installed packages */
        printf("=== CAPP — Upgrade All ===\n\n");

        char installed_path[MAX_PATH];
        snprintf(installed_path, sizeof(installed_path), "%s%sinstalled.txt",
                 bundles_dir, PATH_SEP);

        FILE *inst = fopen(installed_path, "r");
        if (!inst) {
            printf("[capp] No installed packages found.\n");
            free(available_data);
            return 0;
        }

        /* Collect names first (file may change as we upgrade) */
        char names[128][MAX_PATH];
        int count = 0;
        char line[MAX_PATH];
        while (fgets(line, sizeof(line), inst) && count < 128) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strlen(line) > 0)
                strncpy(names[count++], line, MAX_PATH - 1);
        }
        fclose(inst);

        for (int i = 0; i < count; i++) {
            int r = upgrade_single(names[i], available_data);
            if (r != 0) {
                fprintf(stderr, "[capp] Failed to upgrade '%s'.\n", names[i]);
                ret = 1;
            }
        }

        printf("\n[capp] Upgrade complete.\n");
    }

    free(available_data);
    return ret;
}

/* ── Subcommand: search ────────────────────────────────────────────────────── */

/*
 * Case-insensitive substring match (portable, no regex needed).
 */
static int icontains(const char *haystack, const char *needle) {
    if (!needle || strlen(needle) == 0) return 1;
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++)
            if (tolower((unsigned char)haystack[i+j]) != tolower((unsigned char)needle[j])) break;
        if (j == nlen) return 1;
    }
    return 0;
}

static int cmd_search(const char *query) {
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

    char available_path[MAX_PATH];
    snprintf(available_path, sizeof(available_path), "%s%savailable.txt",
             bundles_dir, PATH_SEP);

    printf("=== CAPP — Search: \"%s\" ===\n\n", query);

    /* ----- Search available.txt ----- */
    FILE *af = fopen(available_path, "r");
    if (!af) {
        fprintf(stderr, "[capp] available.txt not found. Run 'capp update' first.\n");
    } else {
        char line[MAX_PATH];
        int header_printed = 0;

        /* Collect the latest version of each matching package for display */
        /* We'll just print all matching lines, grouping by name */
        /* Build a deduplicated list of matching package names + best version */
        typedef struct { char name[256]; char version[64]; char filename[256]; } Match;
        Match matches[256];
        int nmatch = 0;

        while (fgets(line, sizeof(line), af)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strlen(line) == 0) continue;

            char name[256], version[256], filename[256];
            if (sscanf(line, "%255[^|]|%255[^|]|%255s", name, version, filename) < 3)
                continue;

            if (!icontains(name, query) && !icontains(filename, query)) continue;

            /* Update or add to matches list, keeping highest version */
            int found = 0;
            for (int i = 0; i < nmatch; i++) {
                if (strcmp(matches[i].name, name) == 0) {
                    if (compare_versions(version, matches[i].version) > 0) {
                        strncpy(matches[i].version,  version,  63);
                        strncpy(matches[i].filename, filename, 255);
                    }
                    found = 1; break;
                }
            }
            if (!found && nmatch < 256) {
                strncpy(matches[nmatch].name,     name,     255);
                strncpy(matches[nmatch].version,  version,  63);
                strncpy(matches[nmatch].filename, filename, 255);
                nmatch++;
            }
        }
        fclose(af);

        if (nmatch > 0) {
            if (!header_printed) { printf("Available packages:\n"); header_printed = 1; }
            for (int i = 0; i < nmatch; i++) {
                char inst_mark = is_app_installed(matches[i].name) ? '*' : ' ';
                printf("  %c %-24s  %-12s  %s\n",
                       inst_mark, matches[i].name, matches[i].version, matches[i].filename);
            }
            printf("\n  (* = installed)\n");
        } else {
            printf("  No packages matching \"%s\" found in available.txt.\n", query);
        }
    }

    /* ----- Search local metadata.json files (installed packages only) ----- */
    char data_dir[MAX_PATH];
    if (!get_data_dir(data_dir, sizeof(data_dir))) return 0;

    printf("\nInstalled packages matching \"%s\":\n", query);
    int local_found = 0;

#ifdef _WIN32
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", data_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

            /* Only consider packages that are in installed.txt */
            if (!is_app_installed(fd.cFileName)) continue;

            char meta_path[MAX_PATH];
            snprintf(meta_path, sizeof(meta_path), "%s\\%s\\metadata.json",
                     data_dir, fd.cFileName);
            FILE *mf = fopen(meta_path, "r");
            if (!mf) continue;
            fseek(mf, 0, SEEK_END); long msz = ftell(mf); fseek(mf, 0, SEEK_SET);
            char *mbuf = malloc(msz + 1);
            fread(mbuf, 1, msz, mf); fclose(mf); mbuf[msz] = '\0';

            if (icontains(mbuf, query)) {
                char name[256] = "", ver[64] = "", desc[512] = "";
                json_get_string(mbuf, "name", name, sizeof(name));
                json_get_string(mbuf, "version", ver, sizeof(ver));
                json_get_string(mbuf, "description", desc, sizeof(desc));
                printf("  %-24s  %-12s  %s\n", name[0] ? name : fd.cFileName, ver, desc);
                local_found = 1;
            }
            free(mbuf);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *dir = opendir(data_dir);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            /* Only consider packages that are in installed.txt */
            if (!is_app_installed(entry->d_name)) continue;

            char meta_path[MAX_PATH];
            snprintf(meta_path, sizeof(meta_path), "%s/%s/metadata.json",
                     data_dir, entry->d_name);
            FILE *mf = fopen(meta_path, "r");
            if (!mf) continue;
            fseek(mf, 0, SEEK_END); long msz = ftell(mf); fseek(mf, 0, SEEK_SET);
            char *mbuf = malloc(msz + 1);
            fread(mbuf, 1, msz, mf); fclose(mf); mbuf[msz] = '\0';

            if (icontains(mbuf, query)) {
                char name[256] = "", ver[64] = "", desc[512] = "";
                json_get_string(mbuf, "name", name, sizeof(name));
                json_get_string(mbuf, "version", ver, sizeof(ver));
                json_get_string(mbuf, "description", desc, sizeof(desc));
                printf("  %-24s  %-12s  %s\n",
                       name[0] ? name : entry->d_name, ver, desc);
                local_found = 1;
            }
            free(mbuf);
        }
        closedir(dir);
    }
#endif

    if (!local_found)
        printf("  (none)\n");

    return 0;
}

/* ── Subcommand: show ────────────────────────────────────────────── */

static int cmd_show(const char *app_name_raw) {
    char app_name[MAX_PATH];
    strncpy(app_name, app_name_raw, sizeof(app_name) - 1);
    app_name[sizeof(app_name) - 1] = '\0';
    strip_capp_ext(app_name);

    printf("=== CAPP \u2014 Show: %s ===\n\n", app_name);

    /* Show metadata if the data directory exists for this package */
    char app_data_dir[MAX_PATH];
    get_app_data_dir(app_name, app_data_dir, sizeof(app_data_dir));
    char meta_path[MAX_PATH];
    snprintf(meta_path, sizeof(meta_path), "%s%smetadata.json", app_data_dir, PATH_SEP);

    FILE *mf = fopen(meta_path, "r");
    if (mf) {
        fseek(mf, 0, SEEK_END); long msz = ftell(mf); fseek(mf, 0, SEEK_SET);
        char *mbuf = malloc(msz + 1);
        fread(mbuf, 1, msz, mf); fclose(mf); mbuf[msz] = '\0';

        char name[256] = "", version[64] = "", author[256] = "", description[1024] = "";
        json_get_string(mbuf, "name",        name,        sizeof(name));
        json_get_string(mbuf, "version",     version,     sizeof(version));
        json_get_string(mbuf, "author",      author,      sizeof(author));
        json_get_string(mbuf, "description", description, sizeof(description));
        free(mbuf);

        printf("  Name        : %s\n", name[0]        ? name        : app_name);
        printf("  Version     : %s\n", version[0]     ? version     : "unknown");
        printf("  Author      : %s\n", author[0]      ? author      : "unknown");
        printf("  Description : %s\n", description[0] ? description : "(none)");
    } else {
        printf("  (No metadata available \u2014 data directory not present)\n");
    }

    /* Installed status comes solely from installed.txt */
    printf("  Installed   : %s\n", is_app_installed(app_name) ? "yes" : "no");

    /* Check available.txt for the latest mirror version */
    char bundles_dir[MAX_PATH];
    get_bundles_dir(bundles_dir, sizeof(bundles_dir));
    char available_path[MAX_PATH];
    snprintf(available_path, sizeof(available_path), "%s%savailable.txt",
             bundles_dir, PATH_SEP);

    FILE *af = fopen(available_path, "r");
    if (af) {
        fseek(af, 0, SEEK_END); long asz = ftell(af); fseek(af, 0, SEEK_SET);
        char *abuf = malloc(asz + 1);
        fread(abuf, 1, asz, af); fclose(af); abuf[asz] = '\0';

        PackageInfo *info = find_package_in_list(abuf, app_name, NULL);
        if (info) {
            printf("  Latest      : %s  (%s)\n", info->version, info->filename);
            free(info->version); free(info->filename); free(info);
        }
        free(abuf);
    }

    printf("\n");
    return 0;
}

/* ── Subcommand: man ───────────────────────────────────────────────────────── */

/*
 * Open the instructions file for an installed app.
 * If it has been cleared from ~/.capp/data/<app>/instructions.*, re-extract
 * from the stored bundle in ~/.capp/bundles/.
 */
static int cmd_man(const char *app_name_raw) {
    char app_name[MAX_PATH];
    strncpy(app_name, app_name_raw, sizeof(app_name) - 1);
    app_name[sizeof(app_name) - 1] = '\0';
    strip_capp_ext(app_name);

    printf("=== CAPP — Manual: %s ===\n\n", app_name);

    if (!is_app_installed(app_name)) {
        fprintf(stderr, "[capp] Error: '%s' is not installed.\n", app_name);
        return 1;
    }

    char app_data_dir[MAX_PATH];
    get_app_data_dir(app_name, app_data_dir, sizeof(app_data_dir));

    /* Look for an existing instructions.* in the data dir */
    char found_path[MAX_PATH];
    found_path[0] = '\0';

#ifdef _WIN32
    {
        char pattern[MAX_PATH];
        snprintf(pattern, sizeof(pattern), "%s\\instructions.*", app_data_dir);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            snprintf(found_path, sizeof(found_path), "%s\\%s", app_data_dir, fd.cFileName);
            FindClose(h);
        }
    }
#else
    {
        DIR *dir = opendir(app_data_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strncmp(entry->d_name, "instructions", 12) == 0 && entry->d_name[12] == '.') {
                    snprintf(found_path, sizeof(found_path), "%s/%s", app_data_dir, entry->d_name);
                    break;
                }
            }
            closedir(dir);
        }
    }
#endif

    /* If not found locally, re-extract from bundle */
    if (found_path[0] == '\0') {
        printf("[capp] Instructions not in data directory. Re-extracting from bundle...\n");

        char bundles_dir[MAX_PATH];
        if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

        char bundle_path[MAX_PATH];
        snprintf(bundle_path, sizeof(bundle_path), "%s%s%s.capp",
                 bundles_dir, PATH_SEP, app_name);

        FILE *bf = fopen(bundle_path, "rb");
        if (!bf) {
            fprintf(stderr, "[capp] Error: Bundle file not found at '%s'.\n", bundle_path);
            return 1;
        }
        fclose(bf);

        char extract_dir[MAX_PATH];
        snprintf(extract_dir, sizeof(extract_dir), "%s%s%s_man_tmp",
                 bundles_dir, PATH_SEP, app_name);

        char cmd[MAX_CMD];
        snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle_path, extract_dir);
        if (system(cmd) != 0) {
            fprintf(stderr, "[capp] Error: Could not extract bundle.\n");
            return 1;
        }

        /* Get current version for metadata */
        char cur_ver[64] = "unknown";
        get_installed_version(app_name, cur_ver, sizeof(cur_ver));

        int rc = open_instructions(extract_dir, app_name, cur_ver);

        snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
        system(cmd);
        return rc;
    }

    /* Open the cached file */
    printf("[capp] Opening: %s\n", found_path);

    if (is_executable_file(found_path)) {
        fprintf(stderr, "[capp] SECURITY ERROR: instructions file is an executable. Aborting.\n");
        return 1;
    }

    char cmd[MAX_CMD];

#if defined(_WIN32) || defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), OPEN_CMD, found_path);
    system(cmd);
#else
    if (system("which xdg-open > /dev/null 2>&1") == 0) {
        snprintf(cmd, sizeof(cmd), OPEN_CMD, found_path);
        system(cmd);
    } else {
        if (!is_text_ext(basename_of(found_path)) || is_binary_file(found_path)) {
            printf("[capp] Instructions file is not displayable as text.\n");
        } else {
            printf("[capp] ---- %s ----\n\n", basename_of(found_path));
            snprintf(cmd, sizeof(cmd), CAT_CMD, found_path);
            system(cmd);
            printf("\n[capp] ---- End ----\n");
        }
    }
#endif

    return 0;
}

/* -- Subcommand: list --------------------------------------------------------- */

/*
 * List all packages in available.txt (one per line, showing the latest
 * version of each unique package name).
 */
static int cmd_list(void) {
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

    char available_path[MAX_PATH];
    snprintf(available_path, sizeof(available_path), "%s%savailable.txt",
             bundles_dir, PATH_SEP);

    printf("=== CAPP -- Available Packages ===\n\n");

    FILE *af = fopen(available_path, "r");
    if (!af) {
        fprintf(stderr, "[capp] available.txt not found. Run 'capp update' first.\n");
        return 1;
    }

    /* Collect latest version of each unique package name */
    typedef struct { char name[256]; char version[64]; } Entry;
    Entry entries[1024];
    int nentries = 0;

    char line[MAX_PATH];
    while (fgets(line, sizeof(line), af)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0 || line[0] == '#') continue;

        char name[256], version[64], filename[256];
        if (sscanf(line, "%255[^|]|%63[^|]|%255s", name, version, filename) < 3) continue;

        int found = 0;
        for (int i = 0; i < nentries; i++) {
            if (strcmp(entries[i].name, name) == 0) {
                if (compare_versions(version, entries[i].version) > 0)
                    strncpy(entries[i].version, version, 63);
                found = 1;
                break;
            }
        }
        if (!found && nentries < 1024) {
            strncpy(entries[nentries].name,    name,    255);
            strncpy(entries[nentries].version, version, 63);
            nentries++;
        }
    }
    fclose(af);

    if (nentries == 0) {
        printf("  (no packages listed)\n");
        return 0;
    }

    for (int i = 0; i < nentries; i++)
        printf("  %-24s  %s\n", entries[i].name, entries[i].version);

    printf("\n  %d package(s) available.\n", nentries);
    return 0;
}

/* -- Subcommand: list-installed ----------------------------------------------- */

/*
 * List all packages recorded in installed.txt.
 */
static int cmd_list_installed(void) {
    char bundles_dir[MAX_PATH];
    if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;

    char installed_path[MAX_PATH];
    snprintf(installed_path, sizeof(installed_path), "%s%sinstalled.txt",
             bundles_dir, PATH_SEP);

    printf("=== CAPP -- Installed Packages ===\n\n");

    FILE *f = fopen(installed_path, "r");
    if (!f) {
        printf("  (no packages installed)\n");
        return 0;
    }

    int count = 0;
    char line[MAX_PATH];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;
        printf("  %s\n", line);
        count++;
    }
    fclose(f);

    if (count == 0)
        printf("  (no packages installed)\n");
    else
        printf("\n  %d package(s) installed.\n", count);

    return 0;
}

/* ── Subcommand: clear-cache ──────────────────────────────────────────────── */

/*
 * Remove only instructions.* files from every app's data directory.
 * Metadata (metadata.json) is preserved.
 */
static int cmd_clear_cache(void) {
    char data_dir[MAX_PATH];
    if (!get_data_dir(data_dir, sizeof(data_dir))) return 1;

    printf("=== CAPP — Clear Cache ===\n");
    printf("[capp] Removing cached instructions from: %s\n\n", data_dir);

    int removed = 0;

#ifdef _WIN32
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", data_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[capp] No cached data found.\n");
        return 0;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char app_dir[MAX_PATH];
        snprintf(app_dir, sizeof(app_dir), "%s\\%s", data_dir, fd.cFileName);

        char sub_pattern[MAX_PATH];
        snprintf(sub_pattern, sizeof(sub_pattern), "%s\\instructions.*", app_dir);
        WIN32_FIND_DATAA sfd;
        HANDLE sh = FindFirstFileA(sub_pattern, &sfd);
        if (sh != INVALID_HANDLE_VALUE) {
            do {
                char instr_path[MAX_PATH];
                snprintf(instr_path, sizeof(instr_path), "%s\\%s", app_dir, sfd.cFileName);
                char cmd[MAX_CMD];
                snprintf(cmd, sizeof(cmd), REMOVE_FILE_CMD, instr_path);
                if (system(cmd) == 0) {
                    printf("[capp] Removed: %s\\%s\n", fd.cFileName, sfd.cFileName);
                    removed++;
                }
            } while (FindNextFileA(sh, &sfd));
            FindClose(sh);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(data_dir);
    if (!dir) {
        printf("[capp] No cached data found.\n");
        return 0;
    }
    struct dirent *app_entry;
    while ((app_entry = readdir(dir)) != NULL) {
        if (app_entry->d_name[0] == '.') continue;

        char app_dir[MAX_PATH];
        snprintf(app_dir, sizeof(app_dir), "%s/%s", data_dir, app_entry->d_name);

        struct stat st;
        if (stat(app_dir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        DIR *sub = opendir(app_dir);
        if (!sub) continue;
        struct dirent *fe;
        while ((fe = readdir(sub)) != NULL) {
            if (strncmp(fe->d_name, "instructions", 12) == 0 && fe->d_name[12] == '.') {
                char instr_path[MAX_PATH];
                snprintf(instr_path, sizeof(instr_path), "%s/%s", app_dir, fe->d_name);
                char cmd[MAX_CMD];
                snprintf(cmd, sizeof(cmd), REMOVE_FILE_CMD, instr_path);
                if (system(cmd) == 0) {
                    printf("[capp] Removed: %s/%s\n", app_entry->d_name, fe->d_name);
                    removed++;
                }
            }
        }
        closedir(sub);
    }
    closedir(dir);
#endif

    if (removed == 0)
        printf("[capp] Nothing to clear.\n");
    else
        printf("\n[capp] Removed %d instructions file(s).\n", removed);

    return 0;
}

/* ── Entry point ───────────────────────────────────────────────────────────── */

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s create                       — bundle a folder into a .capp file\n", prog);
    fprintf(stderr, "  %s install  <App.capp>          — install a .capp bundle\n", prog);
    fprintf(stderr, "  %s install-remote <AppName>     — install latest version from mirror\n", prog);
    fprintf(stderr, "  %s uninstall <AppName>          — uninstall a previously installed app\n", prog);
    fprintf(stderr, "  %s update                       — refresh available.txt from mirrors\n", prog);
    fprintf(stderr, "  %s upgrade [AppName]            — upgrade one or all packages\n", prog);
    fprintf(stderr, "  %s search <query>               — search available and installed packages\n", prog);
    fprintf(stderr, "  %s show <AppName>               — show package metadata\n", prog);
    fprintf(stderr, "  %s man <AppName>                — open instructions for an installed app\n", prog);
    fprintf(stderr, "  %s clear-cache                  — remove cached instructions files\n", prog);
    fprintf(stderr, "\nEnvironment variables:\n");
    fprintf(stderr, "  CAPP_MIRROR=https://...  — override default mirror URL\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "create") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: %s create\n", argv[0]); return 1; }
        return cmd_create();
    }

    if (strcmp(subcmd, "install") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: %s install <App.capp>\n", argv[0]); return 1; }
        return cmd_install(argv[2]);
    }

    if (strcmp(subcmd, "install-remote") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: %s install-remote <AppName>\n", argv[0]); return 1; }
        return cmd_install_remote(argv[2]);
    }

    if (strcmp(subcmd, "uninstall") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: %s uninstall <AppName>\n", argv[0]); return 1; }
        return cmd_uninstall(argv[2]);
    }

    if (strcmp(subcmd, "update") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: %s update\n", argv[0]); return 1; }
        return cmd_update();
    }

    if (strcmp(subcmd, "upgrade") == 0) {
        if (argc > 3) { fprintf(stderr, "Usage: %s upgrade [AppName]\n", argv[0]); return 1; }
        return cmd_upgrade(argc == 3 ? argv[2] : NULL);
    }

    if (strcmp(subcmd, "search") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: %s search <query>\n", argv[0]); return 1; }
        return cmd_search(argv[2]);
    }

    if (strcmp(subcmd, "show") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: %s show <AppName>\n", argv[0]); return 1; }
        return cmd_show(argv[2]);
    }

    if (strcmp(subcmd, "man") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: %s man <AppName>\n", argv[0]); return 1; }
        return cmd_man(argv[2]);
    }

    if (strcmp(subcmd, "list") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: %s list\n", argv[0]); return 1; }
        return cmd_list();
    }

    if (strcmp(subcmd, "list-installed") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: %s list-installed\n", argv[0]); return 1; }
        return cmd_list_installed();
    }

    if (strcmp(subcmd, "clear-cache") == 0) {
        if (argc != 2) { fprintf(stderr, "Usage: %s clear-cache\n", argv[0]); return 1; }
        return cmd_clear_cache();
    }

    fprintf(stderr, "Unknown subcommand: '%s'\n\n", subcmd);
    print_usage(argv[0]);
    return 1;
}

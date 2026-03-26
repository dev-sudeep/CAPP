 /*
  * uninstall.c - CAPP Bundle Uninstaller
  * Looks up <AppName>.capp in ~/.capp/bundles/, extracts it, runs its
  * uninstall script, then removes the stored bundle.
  *
  * Compile:
  *   Linux/macOS: gcc -o capp-uninstall uninstall.c
  *   Windows (MinGW): gcc -o capp-uninstall.exe uninstall.c
  *
  * Usage: capp-uninstall <AppName>   (no .capp extension needed)
  */
 
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 
 #ifdef _WIN32
   #define PATH_SEP "\\"
   #define UNZIP_CMD        "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\""
   #define UNINSTALL_SCRIPT "uninstall.bat"
   #define RUN_UNINSTALL    "cmd /c \"%s" PATH_SEP UNINSTALL_SCRIPT "\""
   #define RMDIR_CMD        "rmdir /s /q \"%s\""
   #define REMOVE_FILE_CMD  "del /f /q \"%s\""
   #define CAT_CMD          "type \"%s\""
   #define HOME_ENV         "USERPROFILE"
   #define BUNDLES_SUBDIR   "\\.capp\\bundles"
 #else
   #define PATH_SEP "/"
   #define UNZIP_CMD        "unzip -o '%s' -d '%s'"
   #define UNINSTALL_SCRIPT "uninstall.sh"
   #define RUN_UNINSTALL    "bash '%s" PATH_SEP UNINSTALL_SCRIPT "'"
   #define RMDIR_CMD        "rm -rf '%s'"
   #define REMOVE_FILE_CMD  "rm -f '%s'"
   #define CAT_CMD          "cat '%s'"
   #define HOME_ENV         "HOME"
   #define BUNDLES_SUBDIR   "/.capp/bundles"
 #endif
 
 #define MAX_PATH 512
 #define MAX_CMD  1024
 
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
 
 /* Strip a trailing .capp extension from the app name if the user included it */
 static void strip_capp_ext(char *name) {
     size_t len = strlen(name);
     if (len > 5 && strcmp(name + len - 5, ".capp") == 0)
         name[len - 5] = '\0';
 }
 
 /* Prompt the user for confirmation before proceeding */
 static int confirm_uninstall(const char *app_name) {
     char answer[8];
     printf("[uninstall] Are you sure you want to uninstall '%s'? [y/N]: ", app_name);
     fflush(stdout);
     if (!fgets(answer, sizeof(answer), stdin)) return 0;
     answer[strcspn(answer, "\r\n")] = '\0';
     return (answer[0] == 'y' || answer[0] == 'Y');
 }
 
 /*
  * Print the contents of the uninstall script and ask the user to confirm
  * they have reviewed it and wish to proceed.
  * Returns 1 if the user confirms, 0 if they abort.
  */
 static int review_script(const char *extract_dir) {
     char script_path[MAX_PATH];
     char cmd[MAX_CMD];
     char answer[8];
 
     snprintf(script_path, sizeof(script_path),
              "%s" PATH_SEP UNINSTALL_SCRIPT, extract_dir);
 
     /* Check the script actually exists in the bundle */
     FILE *f = fopen(script_path, "r");
     if (!f) {
         fprintf(stderr, "[uninstall] Warning: " UNINSTALL_SCRIPT " not found in bundle.\n");
         return 0;
     }
     fclose(f);
 
     printf("\n[uninstall] ---- Contents of " UNINSTALL_SCRIPT " ----\n\n");
     snprintf(cmd, sizeof(cmd), CAT_CMD, script_path);
     system(cmd);
     printf("\n[uninstall] ---- End of " UNINSTALL_SCRIPT " ----\n\n");
 
     printf("[uninstall] Please review the script above for anything suspicious.\n");
     printf("[uninstall] Proceed with uninstallation? [y/N]: ");
     fflush(stdout);
 
     if (!fgets(answer, sizeof(answer), stdin)) return 0;
     answer[strcspn(answer, "\r\n")] = '\0';
     return (answer[0] == 'y' || answer[0] == 'Y');
 }
 
 int main(int argc, char *argv[]) {
     if (argc != 2) {
         fprintf(stderr, "Usage: %s <AppName>\n", argv[0]);
         fprintf(stderr, "  AppName is the name used during installation (no .capp needed).\n");
         return 1;
     }
 
     /* Accept both "MyApp" and "MyApp.capp" */
     char app_name[MAX_PATH];
     strncpy(app_name, argv[1], sizeof(app_name) - 1);
     app_name[sizeof(app_name) - 1] = '\0';
     strip_capp_ext(app_name);
 
     /* Resolve bundle path: ~/.capp/bundles/<AppName>.capp */
     char bundles_dir[MAX_PATH];
     if (!get_bundles_dir(bundles_dir, sizeof(bundles_dir))) return 1;
 
     char bundle_path[MAX_PATH];
     snprintf(bundle_path, sizeof(bundle_path), "%s%s%s.capp",
              bundles_dir, PATH_SEP, app_name);
 
     /* Temporary extraction directory */
     char extract_dir[MAX_PATH];
     snprintf(extract_dir, sizeof(extract_dir), "%s%s%s_capp_tmp",
              bundles_dir, PATH_SEP, app_name);
 
     char cmd[MAX_CMD];
 
     printf("=== CAPP Uninstaller ===\n");
     printf("[uninstall] App    : %s\n", app_name);
     printf("[uninstall] Bundle : %s\n\n", bundle_path);
 
     /* Step 1: Confirm */
     if (!confirm_uninstall(app_name)) {
         printf("[uninstall] Uninstall cancelled.\n");
         return 0;
     }
 
     /* Step 2: Check bundle exists */
     FILE *test = fopen(bundle_path, "rb");
     if (!test) {
         fprintf(stderr, "\nError: Bundle not found at '%s'.\n", bundle_path);
         fprintf(stderr, "       Is '%s' installed?\n", app_name);
         return 1;
     }
     fclose(test);
 
     /* Step 3: Extract bundle to temp dir */
     snprintf(cmd, sizeof(cmd), UNZIP_CMD, bundle_path, extract_dir);
     printf("\n[uninstall] Extracting bundle...\n");
     if (system(cmd) != 0) {
         fprintf(stderr, "Error: Extraction failed. Is 'unzip' installed?\n");
         return 1;
     }
 
     /* Step 4: Review uninstall script before running */
     if (!review_script(extract_dir)) {
         printf("[uninstall] Uninstallation aborted by user.\n");
         snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
         system(cmd);
         return 1;
     }
 
     /* Step 5: Run uninstall script */
     snprintf(cmd, sizeof(cmd), RUN_UNINSTALL, extract_dir);
     printf("[uninstall] Running uninstall script...\n");
 
     int ret = system(cmd);
     if (ret != 0) {
         fprintf(stderr, "\n[uninstall] Uninstall script exited with code %d.\n", ret);
         snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
         system(cmd);
         return 1;
     }
 
     /* Step 6: Clean up temp extraction folder */
     printf("[uninstall] Cleaning up temporary files...\n");
     snprintf(cmd, sizeof(cmd), RMDIR_CMD, extract_dir);
     system(cmd);
 
     /* Step 7: Remove the stored bundle from ~/.capp/bundles/ */
     snprintf(cmd, sizeof(cmd), REMOVE_FILE_CMD, bundle_path);
     printf("[uninstall] Removing stored bundle...\n");
     if (system(cmd) != 0) {
         fprintf(stderr, "Warning: Could not remove bundle '%s'.\n", bundle_path);
     }
 
     printf("[uninstall] '%s' has been uninstalled.\n", app_name);
     return 0;
 }

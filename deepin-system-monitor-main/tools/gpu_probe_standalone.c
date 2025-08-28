#define _GNU_SOURCE
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int read_first_line(const char *path, char *buf, size_t buflen) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)buflen, f)) { fclose(f); return -1; }
    fclose(f);
    size_t n = strlen(buf);
    if (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[n-1] = '\0';
    return 0;
}

static int read_ll(const char *path, long long *out) {
    char s[128];
    if (read_first_line(path, s, sizeof(s)) != 0) return -1;
    char *end = NULL;
    long long v = strtoll(s, &end, 0);
    if (end == s) return -1;
    *out = v;
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *drm = "/sys/class/drm";
    DIR *dir = opendir(drm);
    if (!dir) { perror("opendir drm"); return 1; }
    struct dirent *de;
    int count = 0;
    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "card", 4) != 0) continue;
        char cardPath[512];
        snprintf(cardPath, sizeof(cardPath), "%s/%s", drm, de->d_name);
        char devPath[512];
        snprintf(devPath, sizeof(devPath), "%s/device", cardPath);
        if (access(devPath, F_OK) != 0) continue;
        count++;
        printf("- %s\n", cardPath);
        char vendor[32] = {0};
        if (read_first_line("/proc/self/mountinfo", vendor, sizeof(vendor)) == 0) { (void)vendor; }
        char venPath[512]; snprintf(venPath, sizeof(venPath), "%s/vendor", devPath);
        char ven[32] = {0}; if (read_first_line(venPath, ven, sizeof(ven)) != 0) strcpy(ven, "unknown");
        printf("  vendor: %s\n", ven);
        char uevent[512]; snprintf(uevent, sizeof(uevent), "%s/uevent", devPath);
        FILE *f = fopen(uevent, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "PCI_SLOT_NAME=", 14) == 0) {
                    char *val = strchr(line, '=');
                    if (val) { val++; size_t n = strlen(val); if (n && (val[n-1]=='\n')) val[n-1]='\0'; printf("  pci: %s\n", val); }
                }
            }
            fclose(f);
        }
        long long util = -1;
        char utilPath[512]; snprintf(utilPath, sizeof(utilPath), "%s/gpu_busy_percent", devPath);
        if (read_ll(utilPath, &util) == 0) printf("  util%%: %lld\n", util);
        else printf("  util%%: n/a\n");
        long long used=0,total=0;
        char usedPath[512]; snprintf(usedPath, sizeof(usedPath), "%s/mem_info_vram_used", devPath);
        char totalPath[512]; snprintf(totalPath, sizeof(totalPath), "%s/mem_info_vram_total", devPath);
        if (read_ll(usedPath, &used) == 0) printf("  vram_used: %lld\n", used);
        if (read_ll(totalPath, &total) == 0) printf("  vram_total: %lld\n", total);
        // hwmon temp
        char hwmon[512]; snprintf(hwmon, sizeof(hwmon), "%s/hwmon", devPath);
        DIR *h = opendir(hwmon);
        if (h) {
            struct dirent *he;
            while ((he = readdir(h))) {
                if (he->d_name[0]=='.') continue;
                char dirpath[512]; snprintf(dirpath, sizeof(dirpath), "%s/%s", hwmon, he->d_name);
                DIR *t = opendir(dirpath);
                if (!t) continue;
                struct dirent *te;
                while ((te = readdir(t))) {
                    if (strncmp(te->d_name, "temp", 4) == 0 && strstr(te->d_name, "_input")) {
                        char path[512]; snprintf(path, sizeof(path), "%s/%s", dirpath, te->d_name);
                        long long mc=0; if (read_ll(path, &mc) == 0) { printf("  tempC: %lld\n", mc/1000); break; }
                    }
                }
                closedir(t);
            }
            closedir(h);
        }
        printf("\n");
        fflush(stdout);
    }
    closedir(dir);
    printf("Found %d card(s)\n", count);
    return 0;
}

/**
 * This file is used as a wrapper for the ISO9960 generator and parser.
 * It will act as the driver that will call functions from parser.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "parser.h"

static void print_entries(const char *path, dir_entry_t *entries, int count) {
    printf("\n--- %s (%d entries) ---\n", path, count);
    for (int i = 0; i < count; i++) {
        printf("  %s%-20s  %10u bytes   LBA %u\n",
               (entries[i].flags & 0x02) ? "[DIR]  " : "[FILE] ",
               entries[i].name,
               entries[i].data_length,
               entries[i].extent_lba);
    }
}

static void rhelp() {
    printf("Available commands:\n");
}

static void ropen(char *input, char *image, char *path) {
    char *cmd = strsep(&input, " ");
    char *iso_path = strsep(&input, " ");
    if (fs_open(iso_path) < 0) {
        printf("Error: Unable to open specified ISO9960 image\n");
        return;
    }

    parse_pvd();
    if (parse_pvd() < 0) {
        fs_close();
        printf("Error: Unable to parse Primary Volume Descriptor\n");
        return;
    }

    strcpy(path, "/");
    strcpy(image, iso_path);
    printf("Opened image: %s\n", iso_path);
}

static void rls(dir_entry_t *entries, char *path) {
    int n = list_dir(path, entries, MAX_DIR_ENTRIES);
    if (n >= 0) print_entries(path, entries, n);
}

int main() {
    system("clear");
    printf("CMPSC473 Honors Project: ISO9960 FS Runner | By Jonathan Chen and Binay Kumar\n");

    char input[100];
    char path[100] = "";
    char image[100] = "";
    dir_entry_t entries[MAX_DIR_ENTRIES];

    while (1) {
        if (strcmp(image, "") == 0) printf("\n > ");
        else printf("\n(%s: %s) > ", image, path);
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) break;
        else if (strcmp(input, "help") == 0) rhelp();
        else if (strstr(input, "open") != NULL) ropen(input, image, path);
        else if (strcmp(input, "ls") == 0) rls(entries, path);
        else if (strlen(input) > 0) printf("Unknown command: %s\n", input);
    }

    fs_close();
    printf("Goodbye!\n");
    return 0;
}
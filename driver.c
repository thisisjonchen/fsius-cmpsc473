/**
 * This file is used as a wrapper for the ISO9960 generator and parser.
 * It will act as the driver that will call functions from parser.
 */

#include <stdio.h>

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

int main(int argc, char *argv[]) {
    const char *iso_path = (argc > 1) ? argv[1] : "fs.iso";

    if (fs_open(iso_path) < 0)
        return 1;

    if (parse_pvd() < 0) {
        fs_close();
        return 1;
    }

    /* List root directory */
    dir_entry_t entries[MAX_DIR_ENTRIES];
    int n = list_dir("/", entries, MAX_DIR_ENTRIES);
    if (n >= 0)
        print_entries("/", entries, n);

    /* List a subdirectory (example: /documents) */
    n = list_dir("/documents", entries, MAX_DIR_ENTRIES);
    if (n >= 0)
        print_entries("/documents", entries, n);

    /* List another subdirectory (example: /programs) */
    n = list_dir("/programs", entries, MAX_DIR_ENTRIES);
    if (n >= 0)
        print_entries("/programs", entries, n);

    fs_close();
    return 0;
}
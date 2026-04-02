/**
 * This file is used as a wrapper for the ISO9960 generator and parser.
 * It will act as the driver that will call functions from parser.
 */

#include <stdio.h>
#include <string.h>

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

    /* Resolve a file path into a directory record */
    dir_entry_t record;
    if (resolve_path("/documents/todo-list.txt", &record) == 0) {
        printf("\nresolve_path('/documents/todo-list.txt') -> ");
        printf("name=%s size=%u lba=%u flags=0x%02x\n",
               record.name, record.data_length, record.extent_lba, record.flags);
    }

    /* Read the first bytes of a file */
    char file_buf[128];
    int bytes = read_file("/documents/todo-list.txt", file_buf, sizeof(file_buf) - 1);
    if (bytes >= 0) {
        file_buf[bytes] = '\0';
        printf("read_file('/documents/todo-list.txt') -> %d bytes\n", bytes);
        printf("preview: %.80s\n", file_buf);
    }

    /* Parse one raw directory record from the root directory sector */
    uint8_t root_sector[SECTOR_SIZE];
    if (read_sector(pvd.root_record.extent_lba, 1, root_sector) > 0) {
        uint32_t offset = 0;
        int skipped = 0;

        while (offset < SECTOR_SIZE && skipped < 2 && root_sector[offset] != 0) {
            offset += root_sector[offset];
            skipped++;
        }

        if (offset < SECTOR_SIZE && root_sector[offset] != 0 &&
            parse_dir_record(root_sector, offset, &record) == 0) {
            printf("parse_dir_record(root, offset=%u) -> name=%s size=%u lba=%u\n",
                   offset, record.name, record.data_length, record.extent_lba);
        }
    }

    fs_close();
    return 0;
}
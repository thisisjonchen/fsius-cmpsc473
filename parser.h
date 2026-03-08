#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>

#define SECTOR_SIZE 2048
#define PVD_SECTOR  16
#define MAX_DIR_ENTRIES 256
#define MAX_NAME_LEN 256

/* Parsed directory entry from an ISO 9660 directory record */
typedef struct {
    uint8_t  length;                /* Length of the directory record */
    uint32_t extent_lba;            /* LBA of the file/directory extent */
    uint32_t data_length;           /* Size of file/directory data in bytes */
    uint8_t  flags;                 /* File flags (bit 1 = directory) */
    uint8_t  name_len;             /* Length of file identifier */
    char     name[MAX_NAME_LEN];   /* Null-terminated file/directory name */
} dir_entry_t;

/* Primary Volume Descriptor */
typedef struct {
    char     system_id[33];
    char     volume_id[33];
    uint32_t volume_space_size;     /* Total sectors in the volume */
    uint16_t logical_block_size;    /* Bytes per logical block */
    uint32_t path_table_size;
    dir_entry_t root_record;        /* Root directory record */
} pvd_t;

extern int   fd;
extern pvd_t pvd;

int fs_open(const char *path);
int fs_close(void);
int read_sector(uint32_t sector, uint32_t count, void *buf);
int parse_pvd(void);
int list_dir(const char *path, dir_entry_t *entries, int max_entries);

#endif
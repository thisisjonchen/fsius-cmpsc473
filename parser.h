#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdint.h>

#define SECTOR_SIZE 2048
#define PVD_SECTOR 16
#define MAX_DIR_ENTRIES 256
#define MAX_NAME_LEN 256

/* Parsed directory entry from an ISO 9660 directory record */
typedef struct {
    uint8_t  length;                /* Length of the directory record */
    uint32_t extent_lba;            /* LBA of the file/directory extent */
    uint32_t data_length;           /* Size of file/directory data in bytes */
    uint8_t  flags;                 /* File flags (bit 1 = directory) */
    uint8_t  name_len;              /* Length of file identifier */
    char     name[MAX_NAME_LEN];    /* Null-terminated file/directory name */

    /* Rock Ridge "PX" (POSIX file attributes). has_px is 1 when a PX
     * System Use entry was found and these fields are valid; 0 means the
     * image does not carry POSIX attributes for this entry. */
    uint8_t  has_px;
    uint32_t px_mode;               /* st_mode (file type + permission bits) */
    uint32_t px_nlink;              /* st_nlink                              */
    uint32_t px_uid;                /* st_uid                                */
    uint32_t px_gid;                /* st_gid                                */
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
int parse_dir_record(const uint8_t *data, uint32_t offset, dir_entry_t *out_record);
int resolve_path(const char *path, dir_entry_t *out_record);
int list_dir(const char *path, dir_entry_t *entries, int max_entries);
int read_file(const char *path, void *buf, uint32_t size);

/* --------------------------- Naming mode ---------------------------- */

/* Which directory tree / extension the parser uses when reading an
 * already-mounted image. AUTO is only meaningful as an argument to
 * iso_set_mode and is never returned by iso_get_mode. */
typedef enum {
    ISO_MODE_AUTO = 0,
    ISO_MODE_ROCK_RIDGE,
    ISO_MODE_JOLIET,
    ISO_MODE_ISO9660,
} iso_mode_t;

/* Extension presence flags, populated by parse_pvd. */
extern int iso_has_rr;
extern int iso_has_joliet;
extern int iso_joliet_level;   /* 1, 2, 3, or 0 if Joliet absent */

iso_mode_t  iso_get_mode(void);

/* Switch the active naming mode. ISO_MODE_AUTO picks the richest
 * available: RR > Joliet > ISO. Returns 0 on success, -1 if the
 * requested mode is not available on the current image. */
int iso_set_mode(iso_mode_t mode);

const char *iso_mode_name(iso_mode_t mode);

/* --------------------------- Misc. helpers --------------------------- */

/* Resolve `path` to a directory entry (convenience wrapper around
 * resolve_path that also normalizes the input first). */
int fs_stat(const char *path, dir_entry_t *out_record);

/* Join a working-directory path `cwd` with a user-supplied `arg`.
 * Absolute args (starting with '/') are copied verbatim; relative args
 * are appended to cwd with a separating '/'. Output is NUL-terminated. */
void iso_join_path(const char *cwd, const char *arg, char *out, size_t outsize);

/* Normalize an absolute ISO path: collapse "." and ".." segments and
 * deduplicate '/' separators. An input that resolves above root becomes
 * "/". The output has no trailing slash (except for root itself). */
void iso_normalize_path(const char *in, char *out, size_t outsize);

/* Compose iso_join_path + iso_normalize_path: produce an absolute,
 * canonical ISO path from `cwd` + `arg`. Safe when `out` aliases `arg`
 * only if `arg` is absolute; otherwise `cwd` and `arg` must be
 * distinct from `out`. */
void iso_canonicalize_path(const char *cwd, const char *arg, char *out, size_t outsize);

#endif
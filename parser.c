/**
 * This file is used to contain the parser functions that will be called by the driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "parser.h"

/* Name equality used by resolve_path. ISO9660 short names are always
 * uppercased in the directory record; Rock Ridge "NM" entries preserve
 * natural case. */
static int iso_name_eq(const char *a, const char *b) {
    return strcasecmp(a, b) == 0;
}

 // #define DEBUG

int fd = -1;
pvd_t pvd;

/* Extension state populated by parse_pvd. */
int iso_has_rr       = 0;
int iso_has_joliet   = 0;
int iso_joliet_level = 0;

/* Cached roots for each tree. cached_root_iso is always populated when
 * an image is mounted; cached_root_joliet only when a Joliet SVD is
 * detected. pvd.root_record is the *active* root used by resolve_path
 * and list_dir, and is overwritten whenever iso_set_mode succeeds. */
static dir_entry_t cached_root_iso;
static dir_entry_t cached_root_joliet;
static iso_mode_t  current_mode = ISO_MODE_ISO9660;

static int decode_joliet_name(const uint8_t *src, uint8_t len, char *out, size_t outsize) {
    if (out == NULL || outsize == 0) return -1;

    size_t oi = 0; // output index

    // Joliet File Identifier is UCS-2, a subset of UTF-16 (and Big Endian)
    for (uint8_t i = 0; i + 1 < len; i += 2) {
        uint16_t ch = ((uint16_t)src[i] << 8) | src[i + 1];

        if (ch == 0) break;

        // ascii character: 0x00 - 0x7F
        if (ch < 0x80) {
            if (oi + 1 >= outsize) break; // check if enough room for char and final null term
            out[oi++] = (char)ch; // write char and inc output index
        // 2-byte UTF-8 char
        } else if (ch < 0x800) {
            if (oi + 2 >= outsize) break;
            // byte format: 110xxxxx 10xxxxxx, 110 -> 0xC0 and 10 -> 0x80
            out[oi++] = (char)(0xC0 | (ch >> 6)); // take upper bits of char and attach to 110xxxxx pattern 
            out[oi++] = (char)(0x80 | (ch & 0x3F)); // keep lower 6 bits
        // 3-byte UTF-8 char
        } else {
            if (oi + 3 >= outsize) break;
            // byte format: 1110xxxx 10xxxxxx 10xxxxxx
            out[oi++] = (char)(0xE0 | (ch >> 12)); // extract top 4 bits, write first byte
            out[oi++] = (char)(0x80 | ((ch >> 6) & 0x3F)); // extract middle 6 bits, write 2nd byte
            out[oi++] = (char)(0x80 | (ch & 0x3F)); // keep lowest 6 bits
        }
    }

    out[oi] = '\0';
    return 0;
}

/**
 * Note: We use open() and close() here instead of fopen() and fclose() to avoid
 * unnecessary complexity with offsets using the file ptr.
 * Further, buffering can be an issue there.
 */

// Opens an existing ISO image in read-only mode for parsing.
int fs_open(const char *path) {
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    #ifdef DEBUG
        printf("Opened FS: %d (FD)\n", fd);
    #endif
    return 0;
}

// Closes the current fs if it was previously opened.
int fs_close(void) {
    if (fd < 0 || close(fd)) {
        perror("close");
        return -1;
    }

    fd = -1;

    #ifdef DEBUG
        printf("Closed FS\n");
    #endif
    return 0;
}

//  read_sector – read `count` sectors starting at `sector` into buf
int read_sector(uint32_t sector, uint32_t count, void *buf) {
    if (fd < 0) {
        fprintf(stderr, "read_sector: no file open\n");
        return -1;
    }

    off_t offset = (off_t)sector * SECTOR_SIZE;
    size_t size  = (size_t)count * SECTOR_SIZE;

    if (lseek(fd, offset, SEEK_SET) < 0) {
        perror("read_sector: lseek");
        return -1;
    }

    ssize_t bytes_read = read(fd, buf, size);
    if (bytes_read < 0) {
        perror("read_sector: read");
        return -1;
    }

    #ifdef DEBUG
        printf("read_sector: sector %u, count %u, got %zd bytes\n",
               sector, count, bytes_read);
    #endif

    return (int)bytes_read;
}

/* Detect Rock Ridge by walking the SUSP area of the root self-record at
 * `root_lba`. Any SP/PX/NM/ER/RR signature is treated as conclusive. */
static int detect_rock_ridge(uint32_t root_lba) {
    uint8_t buf[SECTOR_SIZE];
    if (read_sector(root_lba, 1, buf) < SECTOR_SIZE) return 0;

    uint8_t rec_len = buf[0];
    if (rec_len < 34) return 0;

    uint8_t fid_len = buf[32];
    if ((uint32_t)(33 + fid_len) > rec_len) return 0;

    int su_start = 33 + fid_len;
    if (fid_len % 2 == 0) su_start++;   /* padding byte */

    int su_off = su_start;
    while (su_off + 4 <= rec_len) {
        uint8_t s1 = buf[su_off];
        uint8_t s2 = buf[su_off + 1];
        uint8_t sl = buf[su_off + 2];
        if (sl < 4 || su_off + sl > rec_len) break;

        if ((s1 == 'S' && s2 == 'P') ||
            (s1 == 'P' && s2 == 'X') ||
            (s1 == 'N' && s2 == 'M') ||
            (s1 == 'E' && s2 == 'R') ||
            (s1 == 'R' && s2 == 'R')) {
            return 1;
        }
        su_off += sl;
    }
    return 0;
}

// parse_pvd – read and validate the Primary Volume Descriptor
int parse_pvd() {
    uint8_t buf[SECTOR_SIZE];

    int found_pvd = 0;

    iso_has_rr = 0;
    iso_has_joliet = 0;
    iso_joliet_level = 0;
    memset(&cached_root_iso, 0, sizeof(cached_root_iso));
    memset(&cached_root_joliet, 0, sizeof(cached_root_joliet));

    /*
     * ISO9660 volume descriptors begin at sector 16. PVD has type 1. Joliet if present is stored as a Supplementary
     * Volume Descriptor with type 2, so we need to check for it. The descriptor list ends with type 255.
     */
    for (uint32_t sector = PVD_SECTOR; ; sector++) {
        if (read_sector(sector, 1, buf) < SECTOR_SIZE) {
            fprintf(stderr, "parse_pvd: could not read volume descriptor sector %u\n",
                    sector);
            return -1;
        }

        /* Standard identifier must be "CD001" */
        if (memcmp(buf + 1, "CD001", 5) != 0) {
            fprintf(stderr, "parse_pvd: bad standard identifier\n");
            return -1;
        }

        /* Type code 255 marks the end of the volume descriptor set */
        if (buf[0] == 255) {
            break;
        }

        /* Type code must be 1 (Primary Volume Descriptor) */
        if (buf[0] == 1) {
            /* System Identifier (bytes 8-39, 32 chars) */
            memcpy(pvd.system_id, buf + 8, 32);
            pvd.system_id[32] = '\0';

            /* Volume Identifier (bytes 40-71, 32 chars) */
            memcpy(pvd.volume_id, buf + 40, 32);
            pvd.volume_id[32] = '\0';

            /* Volume Space Size – little-endian at bytes 80-83 */
            memcpy(&pvd.volume_space_size, buf + 80, 4);

            /* Logical Block Size – little-endian at bytes 128-129 */
            memcpy(&pvd.logical_block_size, buf + 128, 2);

            /* Path Table Size – little-endian at bytes 132-135 */
            memcpy(&pvd.path_table_size, buf + 132, 4);

            /* Root Directory Record (34 bytes starting at offset 156) */
            const uint8_t *root = buf + 156;
            cached_root_iso.length = root[0];
            memcpy(&cached_root_iso.extent_lba,  root + 2,  4);
            memcpy(&cached_root_iso.data_length, root + 10, 4);
            cached_root_iso.flags    = root[25];
            cached_root_iso.name_len = root[32];
            strcpy(cached_root_iso.name, "/");

            found_pvd = 1;
        }

        /* Type code 2 is a Supplementary Volume Descriptor. Joliet is
         * indicated by escape sequences "%/@", "%/C", or "%/E" at
         * bytes 88-90, meaning UCS-2 levels 1, 2, or 3. */
        else if (buf[0] == 2) {
            if (buf[88] == '%' && buf[89] == '/' &&
                (buf[90] == '@' || buf[90] == 'C' || buf[90] == 'E')) {

                const uint8_t *root = buf + 156;
                cached_root_joliet.length = root[0];
                memcpy(&cached_root_joliet.extent_lba,  root + 2,  4);
                memcpy(&cached_root_joliet.data_length, root + 10, 4);
                cached_root_joliet.flags    = root[25];
                cached_root_joliet.name_len = root[32];
                strcpy(cached_root_joliet.name, "/");

                iso_has_joliet = 1;
                iso_joliet_level = (buf[90] == '@') ? 1 :
                                   (buf[90] == 'C') ? 2 : 3;
            }
        }
    }

    if (!found_pvd) {
        fprintf(stderr, "parse_pvd: no Primary Volume Descriptor found\n");
        return -1;
    }

    /* Probe the PVD root for Rock Ridge SUSP signatures. Done after the
     * VDS walk because we need a valid root LBA. */
    iso_has_rr = detect_rock_ridge(cached_root_iso.extent_lba);

    /* Auto-pick the richest naming mode available. */
    iso_set_mode(ISO_MODE_AUTO);

    return 0;
}

//  parse_dir_record – decode one directory record from raw bytes
int parse_dir_record(const uint8_t *data, uint32_t offset,
                     dir_entry_t *out_record) {
    if (data == NULL || out_record == NULL) {
        return -1;
    }

    const uint8_t *rec = data + offset;

    uint8_t rec_len = rec[0];
    if (rec_len == 0) {
        return -1;  /* zero-length marks end of used space in this sector */
    }
    if (rec_len < 34) {
        return -1;  /* shortest valid ISO 9660 directory record */
    }

    uint8_t file_id_len = rec[32];
    if ((uint32_t)(33 + file_id_len) > rec_len) {
        return -1;
    }

    out_record->length = rec_len;

    memcpy(&out_record->extent_lba,  rec + 2,  4);   /* LE */
    memcpy(&out_record->data_length, rec + 10, 4);   /* LE */
    out_record->flags    = rec[25];
    out_record->name_len = file_id_len;

    /* Rock Ridge attributes default to "absent". Filled in below if a
     * PX System Use entry is found. */
    out_record->has_px   = 0;
    out_record->px_mode  = 0;
    out_record->px_nlink = 0;
    out_record->px_uid   = 0;
    out_record->px_gid   = 0;

    /* Joliet records are UCS-2BE; RR/ISO records are ASCII. The "."/".."
     * self-records (length-1 name 0x00/0x01) are always raw bytes. */
    int is_dotdot = (file_id_len == 1 && (rec[33] == 0x00 || rec[33] == 0x01));

    if (current_mode == ISO_MODE_JOLIET && !is_dotdot) {
        decode_joliet_name(rec + 33, file_id_len, out_record->name, sizeof(out_record->name));
        out_record->name_len = (uint8_t)strlen(out_record->name);
    } else {
        size_t copied_name_len = file_id_len;
        if (copied_name_len >= MAX_NAME_LEN) copied_name_len = MAX_NAME_LEN - 1;

        memcpy(out_record->name, rec + 33, copied_name_len);
        out_record->name[copied_name_len] = '\0';
        out_record->name_len = (uint8_t) copied_name_len;

        /* SUSP / Rock Ridge entries are only honored in RR mode. In
         * ISO mode the user explicitly asked for plain ISO9660, so we
         * skip the walk and let the version-suffix strip below run. */
        if (current_mode != ISO_MODE_ROCK_RIDGE) goto strip_iso_suffix;

        /*
        * Walk the Rock Ridge / SUSP "System Use" area for known entries.
        * The System Use area starts after the file identifier + an
        * optional padding byte (padding is present when name_len is even
        * so that the fixed fields end on an even offset).
        *
        * Currently recognized:
        *   NM - Alternate (natural-case) filename
        *   PX - POSIX file attributes (mode, nlink, uid, gid)
        *
        * Both may appear in any order, so we walk the whole area instead
        * of breaking on the first match.
        */
        int su_start = 33 + file_id_len;
        if (file_id_len % 2 == 0)
            su_start++;   /* padding byte */

        int su_off = su_start;
        int nm_found = 0;
        while (su_off + 4 <= rec_len) {
            uint8_t sig1   = rec[su_off];
            uint8_t sig2   = rec[su_off + 1];
            uint8_t su_len = rec[su_off + 2];

            if (su_len < 4 || su_off + su_len > rec_len)
                break;

            /* NM entry: signature "NM", version at +3, flags at +4, name at +5 */
            if (!nm_found && sig1 == 'N' && sig2 == 'M') {
                uint8_t nm_flags = rec[su_off + 4];
                if (nm_flags == 0 && su_len > 5) {
                    int nm_name_len = su_len - 5;
                    if (nm_name_len >= MAX_NAME_LEN) {
                        nm_name_len = MAX_NAME_LEN - 1;
                    }
                    memcpy(out_record->name, rec + su_off + 5, nm_name_len);
                    out_record->name[nm_name_len] = '\0';
                    out_record->name_len = (uint8_t)nm_name_len;
                }
                nm_found = 1;
            }

            /* PX entry: signature "PX", LEN_PX is 36 (basic) or 44 (with
            * file_serial_number). Each numeric field is 8 bytes: 4 LE then
            * 4 BE recording the same value. We read the LE half. */
            else if (sig1 == 'P' && sig2 == 'X' && su_len >= 36) {
                memcpy(&out_record->px_mode,  rec + su_off + 4,  4);
                memcpy(&out_record->px_nlink, rec + su_off + 12, 4);
                memcpy(&out_record->px_uid,   rec + su_off + 20, 4);
                memcpy(&out_record->px_gid,   rec + su_off + 28, 4);
                out_record->has_px = 1;
            }

            su_off += su_len;
        }

    strip_iso_suffix: ;
    }

    /*
     * If no Rock Ridge name was found, clean up the plain ISO 9660
     * identifier: strip the ";1" version suffix and any trailing dot.
     */
    if (!(out_record->name_len == 1 &&
          (out_record->name[0] == '\0' || out_record->name[0] == '\x01'))) {
        int nlen = (int)strlen(out_record->name);
        if (nlen >= 2 && out_record->name[nlen - 1] >= '0' && out_record->name[nlen - 1] <= '9'
                       && out_record->name[nlen - 2] == ';') {
            out_record->name[nlen - 2] = '\0';
            nlen -= 2;
        }
        if (nlen > 0 && out_record->name[nlen - 1] == '.') {
            out_record->name[nlen - 1] = '\0';
        }
        out_record->name_len = (uint8_t)strlen(out_record->name);
    } else {
        out_record->name_len = 1;
    }

    return 0;
}

// read_dir_entries – read directory at (lba, data_len) into array
static int read_dir_entries(uint32_t lba, uint32_t data_len,
                            dir_entry_t *entries, int max_entries) {
    uint8_t *buf = malloc(data_len);
    if (!buf) {
        perror("malloc");
        return -1;
    }

    uint32_t sectors = (data_len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (read_sector(lba, sectors, buf) < 0) {
        free(buf);
        return -1;
    }

    int      count  = 0;
    uint32_t offset = 0;

    while (offset < data_len && count < max_entries) {
        /* A zero byte means the rest of this sector is unused – skip ahead */
        if (buf[offset] == 0) {
            uint32_t next = ((offset / SECTOR_SIZE) + 1) * SECTOR_SIZE;
            if (next >= data_len)
                break;
            offset = next;
            continue;
        }

        dir_entry_t entry;
        if (parse_dir_record(buf, offset, &entry) < 0)
            break;

        /* Skip the "." (0x00) and ".." (0x01) self/parent entries */
        if (entry.name_len == 1 &&
            (entry.name[0] == '\0' || entry.name[0] == '\x01')) {
            offset += entry.length;
            continue;
        }

        entries[count++] = entry;
        offset += entry.length;
    }

    free(buf);
    return count;
}

//  resolve_path – resolve file or directory path to its dir record
int resolve_path(const char *path, dir_entry_t *out_record) {
    if (out_record == NULL) {
        return -1;
    }

    dir_entry_t current = pvd.root_record;

    if (path == NULL || path[0] == '\0' ||
        (path[0] == '/' && path[1] == '\0')) {
        *out_record = current;
        return 0;
    }

    char pathcopy[1024];
    strncpy(pathcopy, path, sizeof(pathcopy) - 1);
    pathcopy[sizeof(pathcopy) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(pathcopy, "/", &saveptr);
    if (token == NULL) {
        *out_record = current;
        return 0;
    }

    while (token != NULL) {
        dir_entry_t tmp[MAX_DIR_ENTRIES];
        int n = read_dir_entries(current.extent_lba, current.data_length, tmp, MAX_DIR_ENTRIES);
        if (n < 0) {
            return -1;
        }

        int found = 0;
        dir_entry_t matched = {0};
        for (int i = 0; i < n; i++) {
            if (iso_name_eq(tmp[i].name, token)) {
                matched = tmp[i];
                found = 1;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "resolve_path: '%s' not found\n", token);
            return -1;
        }

        token = strtok_r(NULL, "/", &saveptr);
        if (token != NULL && !(matched.flags & 0x02)) {
            fprintf(stderr, "resolve_path: '%s' is not a directory\n", matched.name);
            return -1;
        }

        current = matched;
    }

    *out_record = current;
    return 0;
}

//  list_dir – list entries in the directory identified by `path`
//  Returns the number of entries written to `entries`, or -1
int list_dir(const char *path, dir_entry_t *entries, int max_entries) {
    if (entries == NULL || max_entries <= 0) {
        return -1;
    }

    /* Root directory shortcut */
    if (path == NULL || path[0] == '\0' ||
        (path[0] == '/' && path[1] == '\0')) {
        return read_dir_entries(pvd.root_record.extent_lba,
                                pvd.root_record.data_length,
                                entries,
                                max_entries);
    }

    dir_entry_t target;
    if (resolve_path(path, &target) < 0) {
        return -1;
    }

    if (!(target.flags & 0x02)) {
        fprintf(stderr, "list_dir: '%s' is not a directory\n", path);
        return -1;
    }

    return read_dir_entries(target.extent_lba,
                            target.data_length,
                            entries,
                            max_entries);
}

//  read_file – read file bytes from ISO image into caller buffer
//  Returns bytes copied, or -1 on error
int read_file(const char *path, void *buf, uint32_t size) {
    if (path == NULL || buf == NULL) {
        return -1;
    }

    if (size == 0) {
        return 0;
    }

    dir_entry_t file_record;
    if (resolve_path(path, &file_record) < 0) {
        return -1;
    }

    if (file_record.flags & 0x02) {
        fprintf(stderr, "read_file: '%s' is a directory\n", path);
        return -1;
    }

    uint32_t bytes_to_copy = size;
    if (bytes_to_copy > file_record.data_length) {
        bytes_to_copy = file_record.data_length;
    }

    if (bytes_to_copy == 0) {
        return 0;
    }

    uint32_t sectors = (file_record.data_length + SECTOR_SIZE - 1) / SECTOR_SIZE;
    size_t raw_size = (size_t)sectors * SECTOR_SIZE;
    uint8_t *raw = malloc(raw_size);
    if (raw == NULL) {
        perror("malloc");
        return -1;
    }

    if (read_sector(file_record.extent_lba, sectors, raw) < 0) {
        free(raw);
        return -1;
    }

    memcpy(buf, raw, bytes_to_copy);
    free(raw);

    return (int)bytes_to_copy;
}

/* ------------------------------------------------------------------ */
/*  Misc. helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * fs_stat - convenience public wrapper.
 *
 * Normalizes `path` (collapses "."/".." and duplicate slashes) and then
 * resolves it to a directory record. Callers that want the raw
 * un-normalized behavior can still call resolve_path directly.
 */
int fs_stat(const char *path, dir_entry_t *out_record) {
    if (out_record == NULL) return -1;

    char normalized[1024];
    if (path == NULL || path[0] == '\0') {
        strcpy(normalized, "/");
    } else {
        iso_normalize_path(path, normalized, sizeof(normalized));
    }
    return resolve_path(normalized, out_record);
}

void iso_join_path(const char *cwd, const char *arg,
                   char *out, size_t outsize) {
    if (out == NULL || outsize == 0) return;

    if (arg == NULL || arg[0] == '\0') {
        snprintf(out, outsize, "%s",
                 (cwd && cwd[0]) ? cwd : "/");
        return;
    }

    if (arg[0] == '/') {
        snprintf(out, outsize, "%s", arg);
        return;
    }

    if (cwd == NULL || cwd[0] == '\0' ||
        (cwd[0] == '/' && cwd[1] == '\0')) {
        snprintf(out, outsize, "/%s", arg);
    } else {
        snprintf(out, outsize, "%s/%s", cwd, arg);
    }
}

/*
 * iso_normalize_path - collapse "." / ".." / "//" in an absolute path.
 *
 * Implementation: tokenize on '/', push non-dot segments onto a stack,
 * pop on "..", drop ".". Rebuild with leading '/'. Relative inputs are
 * treated as if they had an implicit leading '/'.
 */
void iso_normalize_path(const char *in, char *out, size_t outsize) {
    if (out == NULL || outsize == 0) return;

    if (in == NULL || in[0] == '\0') {
        snprintf(out, outsize, "/");
        return;
    }

    /* Working buffer that strtok_r can mutate. */
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", in);

    const char *segs[128];
    int nseg = 0;

    char *save = NULL;
    char *tok  = strtok_r(buf, "/", &save);
    while (tok != NULL) {
        if (strcmp(tok, ".") == 0) {
            /* drop */
        } else if (strcmp(tok, "..") == 0) {
            if (nseg > 0) nseg--;
        } else if (nseg < (int)(sizeof(segs) / sizeof(segs[0]))) {
            segs[nseg++] = tok;
        }
        tok = strtok_r(NULL, "/", &save);
    }

    if (nseg == 0) {
        snprintf(out, outsize, "/");
        return;
    }

    size_t off = 0;
    for (int i = 0; i < nseg; i++) {
        int w = snprintf(out + off, (off < outsize) ? outsize - off : 0, "/%s", segs[i]);
        if (w < 0) break;
        off += (size_t)w;
        if (off >= outsize) {
            out[outsize - 1] = '\0';
            return;
        }
    }
}

void iso_canonicalize_path(const char *cwd, const char *arg, char *out, size_t outsize) {
    char joined[1024];
    iso_join_path(cwd, arg, joined, sizeof(joined));
    iso_normalize_path(joined, out, outsize);
}

/* ------------------------------------------------------------------ */
/*  Naming-mode API                                                   */
/* ------------------------------------------------------------------ */

iso_mode_t iso_get_mode(void) {
    return current_mode;
}

const char *iso_mode_name(iso_mode_t mode) {
    switch (mode) {
        case ISO_MODE_AUTO:       return "auto";
        case ISO_MODE_ROCK_RIDGE: return "rock-ridge";
        case ISO_MODE_JOLIET:     return "joliet";
        case ISO_MODE_ISO9660:    return "iso9660";
    }
    return "unknown";
}

int iso_set_mode(iso_mode_t mode) {
    if (mode == ISO_MODE_AUTO) {
        if (iso_has_rr) mode = ISO_MODE_ROCK_RIDGE;
        else if (iso_has_joliet) mode = ISO_MODE_JOLIET;
        else mode = ISO_MODE_ISO9660;
    }

    /* Reject modes that aren't actually present on the current image. */
    if (mode == ISO_MODE_ROCK_RIDGE && !iso_has_rr) return -1;
    if (mode == ISO_MODE_JOLIET && !iso_has_joliet) return -1;

    /* Joliet swaps to the SVD root; RR and ISO both use the PVD root.
     * Rock Ridge mode "decorates" PVD records via the SUSP walk, it
     * doesn't have its own directory tree. */
    pvd.root_record = (mode == ISO_MODE_JOLIET) ? cached_root_joliet : cached_root_iso;
    current_mode = mode;
    return 0;
}

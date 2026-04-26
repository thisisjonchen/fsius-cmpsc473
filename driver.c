/**
 * This file is used as a wrapper for the ISO9960 generator and parser.
 * It will act as the driver that will call functions from parser.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>

#include "parser.h"
#include "generator.h"

#define INPUT_SIZE 256
#define PATH_SIZE 256
#define IMAGE_SIZE 256
#define MAX_ARGS 16

static int is_open(const char *image) {
    return image[0] != '\0';
}

/* Format a POSIX st_mode as a 10-char "drwxr-xr-x" string.
 * `is_dir_fallback` is consulted only when `mode` looks like it has no
 * file-type bits set (i.e. PX was absent). */
static void mode_string(uint32_t mode, int is_dir_fallback, char out[11]) {
    uint32_t type = mode & 0170000;          /* S_IFMT */
    char t = '-';
    switch (type) {
        case 0040000: t = 'd'; break;        /* S_IFDIR  */
        case 0120000: t = 'l'; break;        /* S_IFLNK  */
        case 0020000: t = 'c'; break;        /* S_IFCHR  */
        case 0060000: t = 'b'; break;        /* S_IFBLK  */
        case 0010000: t = 'p'; break;        /* S_IFIFO  */
        case 0140000: t = 's'; break;        /* S_IFSOCK */
        case 0100000: t = '-'; break;        /* S_IFREG  */
        default:      t = is_dir_fallback ? 'd' : '-'; break;
    }
    out[0] = t;
    out[1] = (mode & 0400) ? 'r' : '-';
    out[2] = (mode & 0200) ? 'w' : '-';
    out[3] = (mode & 0100) ? 'x' : '-';
    out[4] = (mode & 0040) ? 'r' : '-';
    out[5] = (mode & 0020) ? 'w' : '-';
    out[6] = (mode & 0010) ? 'x' : '-';
    out[7] = (mode & 0004) ? 'r' : '-';
    out[8] = (mode & 0002) ? 'w' : '-';
    out[9] = (mode & 0001) ? 'x' : '-';
    out[10] = '\0';
}

/* Look up a username for the given uid. Falls back to the numeric id
 * (formatted as a decimal string) when the system has no entry — common
 * because the ISO was authored on a different machine. */
static void user_name(uint32_t uid, char *out, size_t outsize) {
    struct passwd *pw = getpwuid((uid_t)uid);
    if (pw && pw->pw_name) snprintf(out, outsize, "%s", pw->pw_name);
    else                   snprintf(out, outsize, "%u", uid);
}

static void group_name(uint32_t gid, char *out, size_t outsize) {
    struct group *gr = getgrgid((gid_t)gid);
    if (gr && gr->gr_name) snprintf(out, outsize, "%s", gr->gr_name);
    else                   snprintf(out, outsize, "%u", gid);
}

static void print_entries(const char *path, dir_entry_t *entries, int count) {
    printf("\n--- %s (%d entries) ---\n", path, count);
    for (int i = 0; i < count; i++) {
        const dir_entry_t *e = &entries[i];
        int is_dir = (e->flags & 0x02) != 0;

        if (e->has_px) {
            char mode[11];
            char user[64];
            char grp[64];
            mode_string(e->px_mode, is_dir, mode);
            user_name(e->px_uid, user, sizeof(user));
            group_name(e->px_gid, grp, sizeof(grp));

            printf("  %s  %-12s %-12s %10u  LBA %-6u  %s\n",
                   mode, user, grp, e->data_length, e->extent_lba, e->name);
        } else {
            /* Fallback when Rock Ridge PX is absent: keep the original
             * compact format so non-RR images still render cleanly. */
            printf("  %s%-20s  %10u bytes   LBA %u\n",
                   is_dir ? "[DIR]  " : "[FILE] ",
                   e->name, e->data_length, e->extent_lba);
        }
    }
}

static void rhelp(void) {
    printf("Available commands:\n");
    printf("  help              Show this help message\n");
    printf("  mount <iso>       Mount an ISO9660 image\n");
    printf("  close             Close the currently mounted image\n");
    printf("  pwd               Print the current directory path\n");
    printf("  ls                List entries in the current directory\n");
    printf("  cd <name|..|/>    Change directory\n");
    printf("  cat <file>        Print the contents of a file\n");
    printf("  stat <path>       Show full metadata for a file or directory\n");
    printf("  mkiso <src> <out> [volid]\n");
    printf("                    Generate an ISO9660 image from <src> into <out>\n");
    printf("  exit | quit       Exit the program\n");
}

// Mounts the provided image and updates the current path and image (for display)
static void rmount(int argc, char **argv, char *image, char *path) {
    if (argc < 2) {
        printf("Usage: mount <iso>\n");
        return;
    }

    if (is_open(image)) {
        printf("Error: Image already mounted. Use 'unmount' first.\n");
        return;
    }

    if (fs_open(argv[1]) < 0) {
        printf("Error: Unable to mount '%s'\n", argv[1]);
        return;
    }

    if (parse_pvd() < 0) {
        fs_close();
        printf("Error: Unable to parse Primary Volume Descriptor\n");
        return;
    }

    strncpy(image, argv[1], IMAGE_SIZE - 1);
    image[IMAGE_SIZE - 1] = '\0';
    strcpy(path, "/");
    printf("Mounted image: %s\n", argv[1]);
}

// Unmounts the image, if an image is mounted
static void runmount(char *image, char *path) {
    if (!is_open(image)) {
        printf("No image is mounted\n");
        return;
    }
    fs_close();
    image[0] = '\0';
    path[0]  = '\0';
    printf("Unmounted image\n");
}

// Prints the current working directory path
static void rpwd(const char *image, const char *path) {
    if (!is_open(image)) {
        printf("No image is mount\n");
        return;
    }
    printf("%s\n", path);
}

// Prints the list of subdirectories on the current path
static void rls(const char *image, const char *path) {
    if (!is_open(image)) {
        printf("No image is mounted. Use 'mount <iso>' first.\n");
        return;
    }
    dir_entry_t entries[MAX_DIR_ENTRIES];
    int n = list_dir(path, entries, MAX_DIR_ENTRIES);
    if (n >= 0) print_entries(path, entries, n);
}

// Changes directories given the path
static void rcd(int argc, char **argv, const char *image, char *path) {
    if (!is_open(image)) {
        printf("No image is mounted. Use 'mount <iso>' first.\n");
        return;
    }
    if (argc < 2) {
        printf("Usage: cd <name|..|/>\n");
        return;
    }

    char candidate[PATH_SIZE];
    iso_canonicalize_path(path, argv[1], candidate, sizeof(candidate));

    /* Note: Canonicalization already collapses '..' past root to "/", which is
     * always a valid directory, so no parser lookup is needed there. */
    if (strcmp(candidate, "/") != 0) {
        dir_entry_t rec;
        if (fs_stat(candidate, &rec) < 0) {
            printf("Error: '%s' not found\n", argv[1]);
            return;
        }
        if (!(rec.flags & 0x02)) {
            printf("Error: '%s' is not a directory\n", argv[1]);
            return;
        }
    }

    snprintf(path, PATH_SIZE, "%s", candidate);
}

// Prints the contents of a provided file name
static void rcat(int argc, char **argv, const char *image, const char *path) {
    if (!is_open(image)) {
        printf("No image is mounted. Use 'mount <iso>' first.\n");
        return;
    }
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return;
    }

    char full[PATH_SIZE];
    iso_canonicalize_path(path, argv[1], full, sizeof(full));

    dir_entry_t rec;
    if (fs_stat(full, &rec) < 0) {
        printf("Error: '%s' not found\n", argv[1]);
        return;
    }
    if (rec.flags & 0x02) {
        printf("Error: '%s' is a directory\n", argv[1]);
        return;
    }

    uint32_t size = rec.data_length;
    if (size == 0) {
        printf("\n");
        return;
    }

    char *buf = malloc(size + 1);
    if (!buf) {
        perror("malloc");
        return;
    }

    int n = read_file(full, buf, size);
    if (n < 0) {
        free(buf);
        printf("Error: read_file failed\n");
        return;
    }

    fwrite(buf, 1, (size_t)n, stdout);
    if (n > 0 && buf[n - 1] != '\n') putchar('\n');
    free(buf);
}

// Makes the iso image given the src dir and iso name
// Prints full metadata (Rock Ridge PX + ISO fields) for a path
static void rstat(int argc, char **argv, const char *image, const char *path) {
    if (!is_open(image)) {
        printf("No image is mounted. Use 'mount <iso>' first.\n");
        return;
    }
    if (argc < 2) {
        printf("Usage: stat <path>\n");
        return;
    }

    char target[PATH_SIZE];
    iso_canonicalize_path(path, argv[1], target, sizeof(target));

    dir_entry_t rec;
    if (fs_stat(target, &rec) < 0) {
        printf("Error: '%s' not found\n", argv[1]);
        return;
    }

    int is_dir = (rec.flags & 0x02) != 0;
    const char *kind = is_dir ? "directory" : "file";

    printf("  Path:        %s\n", target);
    printf("  Name:        %s\n", rec.name);
    printf("  Type:        %s\n", kind);
    printf("  Size:        %u bytes\n", rec.data_length);
    printf("  Extent LBA:  %u\n", rec.extent_lba);
    printf("  ISO flags:   0x%02x\n", rec.flags);

    if (rec.has_px) {
        char mode[11];
        char user[64];
        char grp[64];
        mode_string(rec.px_mode, is_dir, mode);
        user_name(rec.px_uid, user, sizeof(user));
        group_name(rec.px_gid, grp, sizeof(grp));

        printf("  Mode:        %s  (0%o)\n", mode, rec.px_mode & 07777);
        printf("  Links:       %u\n", rec.px_nlink);
        printf("  Owner:       %s (uid %u)\n", user, rec.px_uid);
        printf("  Group:       %s (gid %u)\n", grp, rec.px_gid);
    } else {
        printf("  Mode:        (no Rock Ridge PX entry)\n");
    }
}

static void rmkiso(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: mkiso <srcdir> <out.iso> [volume_id]\n");
        return;
    }
    const char *src = argv[1];
    const char *out = argv[2];
    const char *volid = (argc >= 4) ? argv[3] : "TEST_FS";

    int flags = ISO_GEN_ROCK_RIDGE | ISO_GEN_JOLIET;
    if (iso_generate(src, out, volid, flags) < 0) {
        printf("Error: ISO generation failed\n");
        return;
    }
    printf("Generated ISO: %s (from %s, volume '%s')\n", out, src, volid);
}

/* Split `input` in place on whitespace; fill argv[] and return argc. */
static int tokenize(char *input, char **argv, int max) {
    int argc = 0;
    char *tok = strtok(input, " \t");
    while (tok != NULL && argc < max) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t");
    }
    return argc;
}

int main(void) {
    // ANSI clear screen + scrollback.
    fputs("\033[H\033[2J\033[3J", stdout);
    fflush(stdout);

    printf("CMPSC473 Honors Project: ISO9660 FS CLI | By Jonathan Chen and Binay Dalai\n");
    printf("Type 'help' for a list of commands.\n");

    char input[INPUT_SIZE];
    char path[PATH_SIZE] = "";
    char image[IMAGE_SIZE] = "";

    while (1) {
        if (!is_open(image)) printf("\n > ");
        else printf("\n(%s) %s > ", image, path);

        if (fgets(input, sizeof(input), stdin) == NULL) break;
        // Strip trailing CR and/or LF so CRLF-terminated input scripts
        input[strcspn(input, "\r\n")] = 0;

        char line[INPUT_SIZE];
        strncpy(line, input, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';

        char *argv[MAX_ARGS];
        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc == 0) continue;

        const char *cmd = argv[0];

        if (!strcmp(cmd, "exit") || !strcmp(cmd, "quit")) break;
        else if (!strcmp(cmd, "help")) rhelp();
        else if (!strcmp(cmd, "mount")) rmount(argc, argv, image, path);
        else if (!strcmp(cmd, "unmount")) runmount(image, path);
        else if (!strcmp(cmd, "pwd")) rpwd(image, path);
        else if (!strcmp(cmd, "ls")) rls(image, path);
        else if (!strcmp(cmd, "cd")) rcd(argc, argv, image, path);
        else if (!strcmp(cmd, "cat")) rcat(argc, argv, image, path);
        else if (!strcmp(cmd, "stat")) rstat(argc, argv, image, path);
        else if (!strcmp(cmd, "mkiso")) rmkiso(argc, argv);
        else printf("Unknown command: %s (type 'help')\n", cmd);
    }

    if (is_open(image)) fs_close();
    printf("Goodbye!\n");
    return 0;
}

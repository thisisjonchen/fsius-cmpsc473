/**
 * This file is used as a wrapper for the ISO9960 generator and parser.
 * It will act as the driver that will call functions from parser.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "parser.h"
#include "generator.h"

#define INPUT_SIZE  256
#define PATH_SIZE   256
#define IMAGE_SIZE  256
#define MAX_ARGS    16

static int is_open(const char *image) {
    return image[0] != '\0';
}

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

static void rhelp(void) {
    printf("Available commands:\n");
    printf("  help              Show this help message\n");
    printf("  mount <iso>       Mount an ISO9660 image\n");
    printf("  close             Close the currently mounted image\n");
    printf("  pwd               Print the current directory path\n");
    printf("  ls                List entries in the current directory\n");
    printf("  cd <name|..|/>    Change directory\n");
    printf("  cat <file>        Print the contents of a file\n");
    printf("  mkiso <src> <out> [volid]\n");
    printf("                    Generate an ISO9660 image from <src> into <out>\n");
    printf("  exit | quit       Exit the program\n");
}

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
    printf("Opened image: %s\n", argv[1]);
}

static void rclose(char *image, char *path) {
    if (!is_open(image)) {
        printf("No image is mount\n");
        return;
    }
    fs_close();
    image[0] = '\0';
    path[0]  = '\0';
    printf("Closed image\n");
}

static void rpwd(const char *image, const char *path) {
    if (!is_open(image)) {
        printf("No image is mount\n");
        return;
    }
    printf("%s\n", path);
}

static void rls(const char *image, const char *path) {
    if (!is_open(image)) {
        printf("No image is mount. Use 'mount <iso>' first.\n");
        return;
    }
    dir_entry_t entries[MAX_DIR_ENTRIES];
    int n = list_dir(path, entries, MAX_DIR_ENTRIES);
    if (n >= 0) print_entries(path, entries, n);
}

static void rcd(int argc, char **argv, const char *image, char *path) {
    if (!is_open(image)) {
        printf("No image is mount. Use 'mount <iso>' first.\n");
        return;
    }
    if (argc < 2) {
        printf("Usage: cd <name|..|/>\n");
        return;
    }

    char candidate[PATH_SIZE];
    iso_canonicalize_path(path, argv[1], candidate, sizeof(candidate));

    /* Canonicalization already collapses '..' past root to "/", which is
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

static void rcat(int argc, char **argv, const char *image, const char *path) {
    if (!is_open(image)) {
        printf("No image is mount. Use 'mount <iso>' first.\n");
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

static void rmkiso(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: mkiso <srcdir> <out.iso> [volume_id]\n");
        return;
    }
    const char *src    = argv[1];
    const char *out    = argv[2];
    const char *volid  = (argc >= 4) ? argv[3] : "TEST_FS";

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
    /* ANSI clear screen + scrollback. Avoids forking a shell for
     * `clear` and works on any ANSI-capable terminal (Linux, WSL,
     * macOS, modern Windows Terminal / VS Code integrated terminal). */
    fputs("\033[H\033[2J\033[3J", stdout);
    fflush(stdout);

    printf("CMPSC473 Honors Project: ISO9960 FS CLI | By Jonathan Chen and Binay Dalai\n");
    printf("Type 'help' for a list of commands.\n");

    char input[INPUT_SIZE];
    char path[PATH_SIZE]   = "";
    char image[IMAGE_SIZE] = "";

    while (1) {
        if (!is_open(image)) printf("\n > ");
        else                 printf("\n(%s) %s > ", image, path);

        if (fgets(input, sizeof(input), stdin) == NULL) break;
        /* Strip trailing CR and/or LF so CRLF-terminated input scripts
         * (common when piping a file authored on Windows) work too. */
        input[strcspn(input, "\r\n")] = 0;

        char line[INPUT_SIZE];
        strncpy(line, input, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';

        char *argv[MAX_ARGS];
        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc == 0) continue;

        const char *cmd = argv[0];

        if      (!strcmp(cmd, "exit") || !strcmp(cmd, "quit")) break;
        else if (!strcmp(cmd, "help"))  rhelp();
        else if (!strcmp(cmd, "mount")) rmount(argc, argv, image, path);
        else if (!strcmp(cmd, "close")) rclose(image, path);
        else if (!strcmp(cmd, "pwd"))   rpwd(image, path);
        else if (!strcmp(cmd, "ls"))    rls(image, path);
        else if (!strcmp(cmd, "cd"))    rcd(argc, argv, image, path);
        else if (!strcmp(cmd, "cat"))   rcat(argc, argv, image, path);
        else if (!strcmp(cmd, "mkiso")) rmkiso(argc, argv);
        else printf("Unknown command: %s (type 'help')\n", cmd);
    }

    if (is_open(image)) fs_close();
    printf("Goodbye!\n");
    return 0;
}

/*
 * Generator - thin wrapper around genisoimage / mkisofs.
 *
 * We use fork() + execvp() instead of system() so that the arguments
 * (notably paths and the volume label) are never subject to shell
 * interpretation. This keeps us safe from quoting/injection issues.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "generator.h"

#define MAX_ARGV 16

static void try_exec(char *const argv[]) {
    /* Prefer genisoimage (GNU), fall back to mkisofs (cdrtools). */
    execvp("genisoimage", argv);
    execvp("mkisofs",      argv);
}

int iso_generate(const char *srcdir,
                 const char *out_iso,
                 const char *volume_id,
                 int flags)
{
    if (srcdir == NULL || out_iso == NULL) {
        fprintf(stderr, "iso_generate: srcdir and out_iso are required\n");
        return -1;
    }

    /*
     * Build argv in a local array. argv[0] is ignored by execvp in the
     * sense that the name comes from the first argument to execvp itself,
     * but genisoimage/mkisofs do inspect argv[0] for usage messages, so
     * we set it to the tool name the child actually runs.
     */
    char *argv[MAX_ARGV];
    int i = 0;

    argv[i++] = (char *)"genisoimage";

    if (flags & ISO_GEN_ROCK_RIDGE) argv[i++] = (char *)"-r";
    if (flags & ISO_GEN_JOLIET)     argv[i++] = (char *)"-J";

    if (volume_id != NULL && volume_id[0] != '\0') {
        argv[i++] = (char *)"-V";
        argv[i++] = (char *)volume_id;
    }

    argv[i++] = (char *)"-o";
    argv[i++] = (char *)out_iso;
    argv[i++] = (char *)srcdir;
    argv[i]   = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        perror("iso_generate: fork");
        return -1;
    }

    if (pid == 0) {
        /* Child: exec the tool. If we return from try_exec, both binaries
         * were missing from PATH. */
        try_exec(argv);
        fprintf(stderr,
                "iso_generate: neither 'genisoimage' nor 'mkisofs' found on PATH (%s)\n",
                strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("iso_generate: waitpid");
        return -1;
    }

    if (!WIFEXITED(status)) {
        fprintf(stderr, "iso_generate: child terminated abnormally\n");
        return -1;
    }

    int code = WEXITSTATUS(status);
    if (code != 0) {
        fprintf(stderr, "iso_generate: tool exited with status %d\n", code);
        return -1;
    }

    return 0;
}

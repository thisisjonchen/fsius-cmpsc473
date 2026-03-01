/**
 * This file is used to contain the parser functions that will be called by the driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "parser.h"

#define DEBUG

int fd = -1;

/**
 * Note: We use open() and close() here instead of fopen() and fclose() to avoid unnecessary complexity with offsets using the file ptr.
 * Further, buffering can be an issue there.
 */

// Opens and returns a file descriptor for fs image with permissions RD/WR for all, but not execute.
int fs_open(char* path) {
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
       perror("open");
       exit(1);
    }
    #ifdef DEBUG 
        printf("Opened FS: %d (FD)\n", fd);
    #endif
}

// Closes the current fs if it was previously opened.
int fs_close() {
    if (fd < 0 || close(fd)) {
        perror("close");
        exit(1);
    }

    fd = -1; // Set fd to -1 after closing
    
    #ifdef DEBUG 
        printf("Closed FS\n");
    #endif
}





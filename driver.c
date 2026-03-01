/**
 * This file is used as a wrapper for the ISO9960 generator and parser.
 * It will act as the driver that will call functions from parser.
 * 
 */

 #include <stdio.h>

 #include "parser.h"

 int main() {
    fs_open("fs.iso");
    fs_close();
 }
#ifndef _PHASH_H_
#define _PHASH_H_
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <mysql.h>
#include <pthread.h>
#include <debugp.h>
#include <math.h>
#include "libphash_vptree.h"

void parse_args( int argc, char *argv[] );
int command_line_function(void);
int optimize(void);

#endif

#ifndef _DEBUGP_H_
#define _DEBUGP_H_

#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <pthread.h>

#define DEBUGP_STDERR 1
#define DEBUGP_SYSLOG 2

int debugp( int debug_level, char* format_string, ... );

void change_debug_level_by( int by );

void set_debug_level( int level );

int get_debug_level(void);

void change_debug_facility( int new_facility );

void setup_debugp_syslog( char *ident );

void debugp_cleanup(void);
#endif

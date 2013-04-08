#include "debugp.h"

static int __debug_facility = DEBUGP_STDERR;
static int __global_debug_level = 0;

static pthread_mutex_t debugp_mutex = PTHREAD_MUTEX_INITIALIZER;

void setup_debugp_syslog( char *ident )
{

	/* Init syslog */
	openlog( ident, LOG_PID, LOG_DAEMON );

	return;
}

void change_debug_facility( int new_facility )
{
	__debug_facility = new_facility;


	return;
}

void set_debug_level( int level )
{
	__global_debug_level = level;
	return;
}
void change_debug_level_by( int by )
{
	__global_debug_level += by;
	return;
}
int get_debug_level(void)
{
	return __global_debug_level;
}

int debugp( int debug_level, char* format_string, ... )
{
	int length = 0;

	pthread_mutex_lock( &debugp_mutex );
	if( debug_level <= __global_debug_level ) // global flag_verbose
	{

		va_list arg_ptr;
		va_start( arg_ptr, format_string );

		if( __debug_facility == DEBUGP_STDERR )
		{
			length = vfprintf( stderr, format_string, arg_ptr );
		}
		else if( __debug_facility == DEBUGP_SYSLOG )
		{
			vsyslog( LOG_DEBUG, format_string, arg_ptr );
		}

		va_end( arg_ptr );
	}
	pthread_mutex_unlock( &debugp_mutex );

	return length;
}

void debugp_cleanup(void)
{
	closelog();
	return;
}

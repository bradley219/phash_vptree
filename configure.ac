AC_PREREQ([2.67])
AC_INIT( [phash_vptree], [0.1.0], [snyder.bradleyj@gmail.com])
AM_INIT_AUTOMAKE(phash_vptree, 0.1.0)
AC_OUTPUT(Makefile src/Makefile)

AC_CONFIG_HEADERS([config.h])
#AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([m], [floor], [], [AC_MSG_ERROR([Math library not found)])] )
AC_CHECK_LIB([pthread], [pthread_create], [], [AC_MSG_ERROR([pthread library not found)])] )
AC_CHECK_LIB([mysqlclient], [mysql_init], [], [AC_MSG_ERROR([mysqlclient library not found)])] )

# Checks for header files.
#AC_CHECK_HEADERS([fcntl.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_OUTPUT

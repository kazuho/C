# $Id: configure.ac,v 1.5 2006/05/19 05:18:41 labskazuho Exp $

AC_PREREQ(2.59)
AC_INIT(C, 0.06, http://labs.cybozu.co.jp/blog/kazuhoatwork/)
AC_CONFIG_SRCDIR([C.c])
AM_INIT_AUTOMAKE(C, 0.06)
AC_PREFIX_DEFAULT(/usr)
AC_PROG_CC
AC_PATH_PROG(HELP2MAN, help2man, false // No help2man //)

# Checks for libraries.

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([stdlib.h string.h sys/param.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_PID_T
AC_TYPE_UID_T

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_FORK
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_REALLOC
AC_FUNC_STAT
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([mkdir rmdir strerror])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

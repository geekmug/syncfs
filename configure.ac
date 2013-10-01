AC_PREREQ(2.57)
AC_INIT([syncfs], [1.0], [scodial@cs.indiana.edu])
AC_CANONICAL_TARGET
AC_CONFIG_SRCDIR([src/syncfs.c])
AC_CONFIG_HEADER([src/include/config.h])
AM_INIT_AUTOMAKE()

AC_DISABLE_SHARED

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_LN_S
AC_PROG_RANLIB

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([limits.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([string.h])
AC_CHECK_HEADERS([sys/mount.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([sys/time.h])
AC_CHECK_HEADERS([unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_PID_T
AC_TYPE_SIGNAL
AC_TYPE_UID_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_C_CONST
AC_C_INLINE
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_GETGROUPS
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_REALLOC
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([dup2])
AC_CHECK_FUNCS([getgrouplist])
AC_CHECK_FUNCS([getpagesize])
AC_CHECK_FUNCS([gettimeofday])
AC_CHECK_FUNCS([memmove])
AC_CHECK_FUNCS([memset])
AC_CHECK_FUNCS([mlockall])
AC_CHECK_FUNCS([socket])
AC_CHECK_FUNCS([strdup])
AC_CHECK_FUNCS([strerror])
AC_CHECK_FUNCS([strtol])
AC_SEARCH_LIBS(clock_gettime, rt)
AC_CHECK_FUNCS([clock_gettime])

ACX_PTHREAD

CFLAGS="${CFLAGS=}"
LDFLAGS="${LDFLAGS=}"

AC_ARG_ENABLE([debug], [  --enable-debug          turn on debugging],,
		[enable_debug=no])
if test "x$enable_debug" = "xyes"; then
	CFLAGS="$CFLAGS -g -O0 -DDEBUG"
fi
AC_ARG_ENABLE([profiling], [  --enable-profiling      turn on profiling],,
		[enable_profiling=no])
if test "x$enable_profiling" = "xyes"; then
	CFLAGS="$CFLAGS -pg"
	LDFLAGS="$LDFLAGS -pg"
fi

AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)

case "$target_os" in
	*cygwin* )
		enable_tcmalloc=no;;
	* )
		enable_tcmalloc=yes;;
esac
AC_ARG_ENABLE([tcmalloc],
		[  --disable-tcmalloc  use system malloc instead of tcmalloc],,
		[])
AM_CONDITIONAL([USE_TCMALLOC], [test x$enable_tcmalloc = xyes])
if test "x$enable_tcmalloc" = "xyes"; then
	cd gperftools
        ./autogen.sh
        cd ..
        AC_CONFIG_SUBDIRS(gperftools)
fi

AC_CONFIG_FILES([Makefile src/Makefile src/libnpfs/Makefile tests/Makefile])
AC_OUTPUT

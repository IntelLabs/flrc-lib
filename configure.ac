#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([iflc-lib], [0.1], [BUG-REPORT-ADDRESS])
AM_INIT_AUTOMAKE([0.1 foreign])
AC_CONFIG_SRCDIR([pillar2c/src/pillar2c_crt_begin.c])

# Checks for programs.
AC_PROG_YACC
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LEX
AC_PROG_RANLIB

# Checks for libraries.
# FIXME: Replace `main' with a function in `-ldl':
AC_CHECK_LIB([dl], [main])
# FIXME: Replace `main' with a function in `-lpthread':
AC_CHECK_LIB([pthread], [main])

# Checks for header files.
AC_FUNC_ALLOCA
AC_CHECK_HEADERS([inttypes.h libintl.h limits.h malloc.h stddef.h stdint.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([atexit memset strdup])

# Check for NASM
AC_PATH_PROG(NASM, nasm, no)
AC_SUBST(NASM)
if test x$NASM = xno; then
  AC_MSG_WARN([Could not find nasm])
  HAVE_NASM="no"
else AC_DEFINE(HAVE_NASM, 1, [Define if NASM, the netwide assembler, is available])
  HAVE_NASM="yes"
fi
AM_CONDITIONAL(HAVE_NASM, test "x$HAVE_NASM" = "xyes")

AC_CANONICAL_HOST

case "$host" in
    *-mingw*|*-*-cygwin*)
        os=win32
        ;;
    *-*-linux*)
        os=linux
        ;;
    *)
        AC_MSG_ERROR([Unsupported host: $host])
        ;;
esac

AM_CONDITIONAL([OS_WIN32], [test "x$os" = "xwin32"])
AM_CONDITIONAL([OS_LINUX], [test "x$os" = "xlinux"])

AC_CONFIG_FILES([Makefile pgc/Makefile pgc/src/Makefile tgc/Makefile tgc/src/Makefile prt/Makefile prt/src/Makefile pillar2c/Makefile pillar2c/src/Makefile toolkit/Makefile toolkit/future/Makefile])
AC_OUTPUT

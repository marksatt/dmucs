#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)
AC_INIT(dmucs, 0.4, vic.norman@gmail.com)
AC_CONFIG_SRCDIR([dmucs_pkt.h])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([nsl], [gethostbyname])
AC_CHECK_LIB([nsl], [gethostbyaddr_r])
AC_CHECK_LIB([socket], [socket])
AC_CHECK_LIB([pthread], [pthread_mutex_lock])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE

# Checks for library functions.
AC_FUNC_FORK
AC_HEADER_STDC
AC_CHECK_FUNCS([gethostbyaddr gethostbyaddr_r gethostname inet_ntoa putenv strerror])

dnl =================================================================
dnl Support for thread-safe versions of gethostbyaddr, gethostbyname,
dnl gmtime and localtime
dnl =================================================================

dnl Next line needed to find the gethost*_r functions on Solaris
AC_CHECK_LIB(nsl, gethostbyname)

AC_CHECK_FUNC(gethostbyaddr_r, [
  AC_MSG_CHECKING([signature of gethostbyaddr_r])
  AC_TRY_COMPILE([
#   include <netdb.h>
  ], [
    struct hostent *h, *hp;
    char *a, *b;
    int l, bl, t, e;
    (void) gethostbyaddr_r(a, l, t, h, b, bl, &hp, &e)
  ], [
    AC_DEFINE(HAVE_GETHOSTBYADDR_R_8_ARGS,1,
      [Define if gethostbyaddr_r() exists and takes 8 arguments])
    AC_MSG_RESULT([8 args])
  ], [
    AC_TRY_COMPILE([
#     include <netdb.h>
    ], [
      struct hostent *h;
      char *a, *b;
      int l, bl, t, e;
      (void) gethostbyaddr_r(a, l, t, h, b, bl, &e)
    ], [
      AC_DEFINE(HAVE_GETHOSTBYADDR_R_7_ARGS,1,
      [Define if gethostbyaddr_r() exists and takes 7 arguments])
      AC_MSG_RESULT([7 args])
    ], [
      AC_TRY_COMPILE([
#       include <netdb.h>
      ], [
        struct hostent_data *d;
        struct hostent *h;
        char a,
        int l, t;
        (void) gethostbyaddr_r(a, l, t, h, d)
      ], [
        AC_DEFINE(HAVE_GETHOSTBYADDR_R_5_ARGS,1,
      [Define if gethostbyaddr_r() exists and takes 5 arguments])
        AC_MSG_RESULT([5 args])
      ], [
        AC_MSG_RESULT(unrecognised)
      ])
    ])
  ])
], [
  AC_MSG_RESULT(no)
])


AC_CONFIG_FILES([Makefile])
AC_OUTPUT

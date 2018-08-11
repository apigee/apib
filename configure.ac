# Copyright 2013 Apigee Corp.
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([apib],[0.1],[greg@brail.org])
AC_CONFIG_SRCDIR([src/apib_oauth.c])
AC_CONFIG_HEADERS([src/config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_CC_C99
AC_USE_SYSTEM_EXTENSIONS

[if [ $GCC ]; then]
AC_SUBST(CC, ["$CC -pedantic -Wall -Wno-deprecated-declarations"])
[fi]

# Checks for libraries.
AC_SEARCH_LIBS([sqrt], m)
AC_SEARCH_LIBS([CRYPTO_num_locks], crypto)
AC_SEARCH_LIBS([pthread_create], pthread)
AC_CHECK_LIB([ssl], [SSL_read], [],
             [AC_MSG_ERROR([OpenSSL library missing. Install "openssl-devel" or the equivalent])])
AC_CHECK_LIB([crypto], [EVP_PKEY_id], [],
             [AC_MSG_ERROR([OpenSSL library missing. Install "openssl-devel" or the equivalent])])
AC_CHECK_LIB([apr-1], [apr_initialize], [],
             [AC_MSG_ERROR([APR library missing. Install "apr-devel" or the equivalent])])
AC_CHECK_LIB([aprutil-1], [apr_uri_parse], [],
             [AC_MSG_ERROR([APR Utilities library missing. Install "apr-util-devel" or the equivalent])])

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h sys/time.h unistd.h pthread.h])

AC_CHECK_HEADER([/usr/include/apr-1/apr_general.h], [APR_LOC="/usr/include/apr-1"])
AC_CHECK_HEADER([/usr/include/apr-1.0/apr_general.h],[APR_LOC="/usr/include/apr-1.0"])
AC_SUBST(APR_LOC, [$APR_LOC])

# Homebrew on OS X puts the OpenSSL headers in a variety of different places,
# and this is harder if you install Homebrew in a non-standard location.
# This finds it.
AC_CHECK_FILE([/usr/include/openssl/ssl.h], [SSL_LOC="/usr/include"])
AC_CHECK_FILE([/opt/openssl/include/openssl/ssl.h], [SSL_LOC="/opt/openssl/include"])
AC_CHECK_FILE([$HOME/homebrew/opt/openssl/include/openssl/ssl.h], [SSL_LOC="$HOME/homebrew/opt/openssl/include"])
AC_SUBST(SSL_LOC, [$SSL_LOC])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([memmove pow sqrt strcasecmp strtol \
               pthread_create pthread_rwlock_init \
               random_r random rand_r rand lrand48_r \
							 EVP_sha1])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT

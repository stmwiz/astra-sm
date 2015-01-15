#!/bin/sh

usage()
{
    cat <<EOF
Usage: $0 [OPTIONS]
    --help

    --bin=PATH                  - path to install binary file.
                                  default value is /usr/bin/astra
    --scripts=PATH              - path to install scripts.
                                  default value is /etc/astra/scripts

    --with-modules=PATH[:PATH]  - list of modules (by default: *)
                                  * - include all modules from ./modules dir.
                                  For example, to append custom module, use:
                                  --with-modules=*:path/to/custom/module

    --with-libdvbcsa            - build with libdvbcsa

    --cc=GCC                    - custom C compiler (cross-compile)
    --static                    - build static binary

    --debug                     - build version for debug

    --cflags="..."              - custom compiler flags
    --ldflags="..."             - custom linker flags
EOF
    exit 0
}

SRCDIR=`dirname $0`

MAKEFILE="Makefile"
CONFFILE="config.h"

APP="astra"
APP_C="gcc"
APP_STRIP=":"

ARG_CC=0
ARG_BPATH="/usr/bin/$APP"
ARG_SPATH="/etc/astra/scripts"
ARG_MODULES="*"
ARG_BUILD_STATIC=0
ARG_CFLAGS=""
ARG_LDFLAGS=""
ARG_LIBDVBCSA=0
ARG_DEBUG=0

set_cc()
{
    ARG_CC=1
    APP_C="$1"
    APP_STRIP=`echo $1 | sed 's/gcc$/strip/'`
}

while [ $# -ne 0 ] ; do
    OPT="$1"
    shift

    case "$OPT" in
        "--help")
            usage
            ;;
        "--bin="*)
            ARG_BPATH=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--scripts="*)
            ARG_SPATH=`echo $OPT | sed -e 's/^[a-z-]*=//' -e 's/\/$//'`
            ;;
        "--with-modules="*)
            ARG_MODULES=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--with-libdvbcsa")
            ARG_LIBDVBCSA=1
            ;;
        "--cc="*)
            set_cc `echo $OPT | sed 's/^--cc=//'`
            ;;
        "--static")
            ARG_BUILD_STATIC=1
            ;;
        "--build-static")
            ARG_BUILD_STATIC=1
            ;;
        "--cflags="*)
            ARG_CFLAGS=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--ldflags="*)
            ARG_LDFLAGS=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--debug")
            ARG_DEBUG=1
            ;;
        "CFLAGS="*)
            ARG_CFLAGS=`echo $OPT | sed -e 's/^[A-Z]*=//'`
            ;;
        "LDFLAGS="*)
            ARG_LDFLAGS=`echo $OPT | sed -e 's/^[A-Z]*=//'`
            ;;
        *)
            echo "Unknown option: $OPT"
            echo "For more information see: $0 --help"
            exit 1
            ;;
    esac
done

if ! which $APP_C >/dev/null ; then
    echo "C Compiler is not found :$APP_C"
    exit 1
fi

if test -f $MAKEFILE ; then
    echo "Cleaning previous build..." >&2
    make clean
    echo >&2
fi

CFLAGS_DEBUG="-fomit-frame-pointer"
if [ $ARG_DEBUG -ne 0 ] ; then
    CFLAGS_DEBUG="-g"
    APP_STRIP=":"
fi

CFLAGS_WARN="-pedantic-errors -Waggregate-return -Wall -Wbad-function-cast -Wcast-align -Werror -Wextra -Wfloat-equal -Wformat=2 -Winit-self -Winline -Wjump-misses-init -Wlogical-op -Wmissing-declarations -Wmissing-include-dirs -Wmissing-prototypes -Wnested-externs -Wold-style-definition -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wstack-protector -Wstrict-aliasing -Wstrict-overflow=4 -Wstrict-prototypes -Wundef -Wuninitialized -Wunreachable-code -Wunused -Wwrite-strings"
CFLAGS="$CFLAGS_DEBUG -I$SRCDIR $CFLAGS_WARN -fno-builtin -fstrict-aliasing -fstrict-overflow"

if [ -n "$ARG_CFLAGS" ] ; then
    CFLAGS="$CFLAGS $ARG_CFLAGS"
fi

MACHINE=`$APP_C -dumpmachine`

case "$MACHINE" in
*"linux"*)
    OS="linux"
    CFLAGS="$CFLAGS -pthread"
    if $APP_C $CFLAGS -dM -E -xc /dev/null | grep -q "__i386__" ; then
        CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64"
    fi
    LDFLAGS="-ldl -lm -lpthread"
    ;;
*"freebsd"*)
    OS="freebsd"
    CFLAGS="$CFLAGS -pthread"
    LDFLAGS="-lm -lpthread"
    ;;
*"darwin"*)
    OS="darwin"
    CFLAGS="$CFLAGS -pthread -Wno-deprecated-declarations"
    LDFLAGS=""
    ;;
*"mingw"*)
    OS="mingw"
    APP="$APP.exe"
    WS32=`$APP_C -print-file-name=libws2_32.a`
    LDFLAGS="$WS32"
    ;;
*)
    echo "Unknown machine type \"$MACHINE\""
    exit 1
    ;;
esac

if [ $ARG_BUILD_STATIC -eq 1 ] ; then
    LDFLAGS="$LDFLAGS -static"
fi

if [ -n "$ARG_LDFLAGS" ] ; then
    LDFLAGS="$LDFLAGS $ARG_LDFLAGS"
fi

# libdvbcsa

LIBDVBCSA=0

libdvbcsa_test_c()
{
    cat <<EOF
#include <stdio.h>
#include <dvbcsa/dvbcsa.h>
int main(void) {
    struct dvbcsa_key_s *key = dvbcsa_key_alloc();
    dvbcsa_key_free(key);
    return 0;
}
EOF
}

check_libdvbcsa()
{
    libdvbcsa_test_c | $APP_C -Werror $1 -c -o .link-test.o -x c - >/dev/null 2>&1
    if [ $? -eq 0 ] ; then
        $APP_C .link-test.o -o .link-test $2 >/dev/null 2>&1
        if [ $? -eq 0 ] ; then
            rm -f .link-test.o .link-test
            return 0
        else
            rm -f .link-test.o
            return 1
        fi
    else
        return 1
    fi
}

build_libdvbcsa()
{
    $APP_C -dM -E -x c /dev/null | grep -q "__x86_64__"
    if [ $? -eq 0 ] ; then
        echo "Build libdvbcsa with UINT64"
        $SRCDIR/contrib/libdvbcsa.sh UINT64 $APP_C
        return $?
    else
        echo "Build libdvbcsa with UINT32"
        $SRCDIR/contrib/libdvbcsa.sh UINT32 $APP_C
        return $?
    fi
}

check_libdvbcsa_all()
{
    if check_libdvbcsa "$CFLAGS" "$LDFLAGS" ; then
        LIBDVBCSA=1
        return 0
    fi

    LIBDVBCSA_CFLAGS="-I$SRCDIR/contrib/build/libdvbcsa/src"
    LIBDVBCSA_LDFLAGS="$SRCDIR/contrib/build/libdvbcsa/libdvbcsa.a"
    if check_libdvbcsa "$LIBDVBCSA_CFLAGS" "$LIBDVBCSA_LDFLAGS" ; then
        LIBDVBCSA=1
        CFLAGS="$CFLAGS $LIBDVBCSA_CFLAGS"
        LDFLAGS="$LDFLAGS $LIBDVBCSA_LDFLAGS"
        return 0
    fi

    LIBDVBCSA_LDFLAGS="-ldvbcsa"
    if check_libdvbcsa "" "$LIBDVBCSA_LDFLAGS" ; then
        LIBDVBCSA=1
        LDFLAGS="$LDFLAGS $LIBDVBCSA_LDFLAGS"
        return 0
    fi

    if ! build_libdvbcsa ; then
        return 1
    fi

    LIBDVBCSA_CFLAGS="-I$SRCDIR/contrib/build/libdvbcsa/src"
    LIBDVBCSA_LDFLAGS="$SRCDIR/contrib/build/libdvbcsa/libdvbcsa.a"
    if check_libdvbcsa "$LIBDVBCSA_CFLAGS" "$LIBDVBCSA_LDFLAGS" ; then
        LIBDVBCSA=1
        CFLAGS="$CFLAGS $LIBDVBCSA_CFLAGS"
        LDFLAGS="$LDFLAGS $LIBDVBCSA_LDFLAGS"
        return 0
    fi

    return 1
}

if [ $ARG_LIBDVBCSA -eq 1 ] ; then
    check_libdvbcsa_all
fi

# APP flags

APP_CFLAGS="$CFLAGS -std=iso9899:1999 -D_GNU_SOURCE"
APP_LDFLAGS="$LDFLAGS"

# temporary file

TMP_MODULE_MK="/tmp"
if [ ! -d "/tmp" ] ; then
    TMP_MODULE_MK="."
fi
TMP_MODULE_MK="$TMP_MODULE_MK/$APP_module.mk-$RANDOM"
touch $TMP_MODULE_MK 2>/dev/null
if [ $? -ne 0 ] ; then
    echo "ERROR: failed to build tmp file ($TMP_MODULE_MK)"
    exit 1
fi
rm -f $TMP_MODULE_MK

#

cat >&2 <<EOF
Compiler Flags:
  TARGET: $MACHINE
      CC: $APP_C
  CFLAGS: $APP_CFLAGS

EOF

# makefile

rm -f $MAKEFILE
exec 5>$MAKEFILE

cat >&5 <<EOF
# generated by configure.sh

MAKEFLAGS = -rR --no-print-directory

APP         = $APP
CC          = $APP_C
CFLAGS      = $APP_CFLAGS
OS          = $OS

CORE_OBJS   =
MODS_OBJS   =

.PHONY: all clean distclean
all: \$(APP)

clean: \$(APP)-clean
	@rm -f Makefile config.h modules/inscript/inscript.h

distclean: clean
EOF

echo "Check modules:" >&2

# main app

APP_SOURCE="$SRCDIR/main.c"
APP_OBJS=""

__check_main_app()
{
    APP_OBJS="main.o"
    $APP_C $APP_CFLAGS -MT $APP_OBJS -MM $APP_SOURCE 2>$TMP_MODULE_MK
    if [ $? -ne 0 ] ; then
        return 1
    fi
    cat <<EOF
	@echo "   CC: \$@"
	@\$(CC) \$(CFLAGS) -DASC_SPATH=\"$ARG_SPATH\" -o \$@ -c \$<
EOF

    return 0
}

touch $CONFFILE
__check_main_app >&5
if [ $? -ne 0 ] ; then
    echo "  ERROR: $APP_SOURCE" >&2
    if [ -f $TMP_MODULE_MK ] ; then
        cat $TMP_MODULE_MK >&2
        rm -f $TMP_MODULE_MK
    fi
    exec 5>&-
    rm -f $MAKEFILE
    exec 6>&-
    rm -f $CONFFILE
    exit 1
else
    echo "     OK: $APP_SOURCE"
fi
echo "" >&5

#

select_modules()
{
    echo "$ARG_MODULES" | tr ':' '\n' | while read M ; do
        if [ -z "$M" ] ; then
            :
        elif [ "$M" = "*" ] ; then
            ls -d $SRCDIR/modules/* | while read M ; do
                if [ -f "$M/module.mk" ] ; then
                    echo "$M"
                fi
            done
        else
            echo "$M" | sed 's/\/$//'
        fi
    done
}

APP_MODULES_LIST=`select_modules`

# modules checking

APP_MODULES_CONF=""

__check_module()
{
    MODULE="$1"
    OGROUP="$2"

    SOURCES=""
    MODULES=""
    CFLAGS=""
    LDFLAGS=""
    ERROR=""

    OBJECTS=""

    . $MODULE/module.mk

    if [ -n "$ERROR" ] ; then
        echo "$MODULE: error: $ERROR" >$TMP_MODULE_MK
        return 1
    fi

    if [ -n "$LDFLAGS" ] ; then
        APP_LDFLAGS="$APP_LDFLAGS $LDFLAGS"
    fi

    if [ -z "$SOURCES" ] ; then
        echo "$MODULE: SOURCES is not defined" >$TMP_MODULE_MK
        return 1
    fi

    echo "${MODULE}_CFLAGS = $CFLAGS"
    echo ""

    for S in $SOURCES ; do
        O=`echo $S | sed -e 's/.c$/.o/'`
        OBJECTS="$OBJECTS $MODULE/$O"
        $APP_C $APP_CFLAGS $CFLAGS -MT $MODULE/$O -MM $MODULE/$S 2>$TMP_MODULE_MK
        if [ $? -ne 0 ] ; then
            return 1
        fi
        cat <<EOF
	@echo "   CC: \$@"
	@\$(CC) \$(CFLAGS) \$(${MODULE}_CFLAGS) -o \$@ -c \$<
EOF
    done

    cat <<EOF

${MODULE}_OBJECTS = $OBJECTS
${OGROUP} += \$(${MODULE}_OBJECTS)

EOF

    if [ -n "MODULES" ] ; then
        APP_MODULES_CONF="$APP_MODULES_CONF $MODULES"
    fi

    return 0
}

check_module()
{
    MODULE="$1"
    OGROUP="$2"

    __check_module $MODULE $OGROUP >&5
    if [ $? -eq 0 ] ; then
        echo "     OK: $MODULE" >&2
    else
        echo "   SKIP: $MODULE" >&2
    fi
    if [ -f $TMP_MODULE_MK ] ; then
        cat $TMP_MODULE_MK >&2
        rm -f $TMP_MODULE_MK
    fi
}

# CORE

check_module $SRCDIR/core "CORE_OBJS"
check_module $SRCDIR/lua "CORE_OBJS"
check_module $SRCDIR/mpegts "CORE_OBJS"

# MODULES

for M in $APP_MODULES_LIST ; do
    check_module $M "MODS_OBJS"
done

# config.h

rm -f $CONFFILE
exec 6>$CONFFILE

cat >&6 <<EOF
/* generated by configure.sh */
#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <lua/lua.h>

EOF

for M in $APP_MODULES_CONF ; do
    echo "LUA_API int luaopen_$M(lua_State *);" >&6
done

cat >&6 <<EOF

#define ASTRA_MODULES \\
    { \\
EOF

for M in $APP_MODULES_CONF ; do
    echo "        luaopen_$M, \\" >&6
done

cat >&6 <<EOF
        NULL \\
    }

#endif /* _CONFIG_H_ */
EOF

exec 6>&-

# MAKEFILE LINKER

VERSION_MAJOR=`sed -n 's/.*ASTRA_VERSION_MAJOR \([0-9]*\).*/\1/p' version.h`
VERSION_MINOR=`sed -n 's/.*ASTRA_VERSION_MINOR \([0-9]*\).*/\1/p' version.h`
VERSION_PATCH=`sed -n 's/.*ASTRA_VERSION_PATCH \([0-9]*\).*/\1/p' version.h`
VERSION="$VERSION_MAJOR.$VERSION_MINOR.$VERSION_PATCH"

cat >&2 <<EOF

Linker Flags:
 VERSION: $VERSION
     OUT: $APP
 LDFLAGS: $APP_LDFLAGS

Install Path:
  BINARY: $ARG_BPATH
 SCRIPTS: $ARG_SPATH
EOF

cat >&5 <<EOF
LD          = $APP_C
LDFLAGS     = $APP_LDFLAGS
STRIP       = $APP_STRIP
VERSION     = $VERSION
BPATH       = $ARG_BPATH
SPATH       = $ARG_SPATH

\$(APP): $APP_OBJS \$(CORE_OBJS) \$(MODS_OBJS)
	@echo "BUILD: \$@"
	@\$(LD) \$^ -o \$@ \$(LDFLAGS)
	@\$(STRIP) \$@

install: \$(APP)
	@echo "INSTALL: \$(BPATH)"
	@rm -f \$(BPATH)
	@cp \$(APP) \$(BPATH)
	@mkdir -p \$(SPATH)
EOF

cat >&5 <<EOF

uninstall:
	@echo "UNINSTALL: \$(APP)"
	@rm -f \$(BPATH)

\$(APP)-clean:
	@echo "CLEAN: \$(APP)"
	@rm -f \$(APP) $APP_OBJS
	@rm -f \$(MODS_OBJS)
	@rm -f \$(CORE_OBJS)
EOF

exec 5>&-

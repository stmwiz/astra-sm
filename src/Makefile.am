noinst_LTLIBRARIES =
bin_PROGRAMS =

#
# Astra library
#
noinst_LTLIBRARIES += libastra.la
libastra_la_CFLAGS = $(LUA_CFLAGS)
libastra_la_LDFLAGS = -module -static
libastra_la_LIBADD = $(LIBM) $(LIBRT) $(LUA_LIBS)
libastra_la_SOURCES =

# core/
libastra_la_SOURCES += \
    core/clock.c \
    core/event.c \
    core/list.c \
    core/log.c \
    core/loopctl.c \
    core/option.c \
    core/socket.c \
    core/strbuffer.c \
    core/stream.c \
    core/thread.c \
    core/timer.c

# mpegts/
libastra_la_SOURCES += \
    mpegts/pcr.c \
    mpegts/psi.c \
    mpegts/pes.c \
    mpegts/types.c

# utils/
libastra_la_SOURCES += \
    utils/astra.c \
    utils/base64.c \
    utils/crc32b.c \
    utils/iso8859.c \
    utils/json.c \
    utils/log.c \
    utils/md5.c \
    utils/sha1.c \
    utils/strhex.c \
    utils/timer.c \
    utils/utils.c

if !HAVE_WIN32
libastra_la_SOURCES += utils/pidfile.c
endif

#
# Module library
#
noinst_LTLIBRARIES += libmodules.la
libmodules_la_LDFLAGS = -module -static
libmodules_la_SOURCES =

# inscript
# if HAVE_INSCRIPT
# libmodules_la_SOURCES += \
#    modules/inscript/inscript.c
# endif

#
# Main binary
#
#bin_PROGRAMS += astra
#astra_SOURCES = main.c
#astra_LDADD = libastra.la

bin_PROGRAMS += test
test_SOURCES = test.c
test_LDADD = libastra.la
SUBDIRS = libnpfs

bin_PROGRAMS = syncfs

syncfs_SOURCES = syncfs.c
syncfs_LDADD = $(PTHREAD_LIBS) libnpfs/libnpfs.la
if USE_TCMALLOC
syncfs_LDADD += @top_srcdir@/gperftools/libtcmalloc.la
endif

AM_CPPFLAGS = -Wall $(PTHREAD_CFLAGS)
AM_LDFLAGS = $(PTHREAD_CFLAGS) -static-libtool-libs
CC = $(PTHREAD_CC)

if USE_TCMALLOC
CCLD = $(CXX)
endif

@top_srcdir@/gperftools/libtcmalloc.la:
	cd @top_srcdir@/gperftools; make libtcmalloc.la

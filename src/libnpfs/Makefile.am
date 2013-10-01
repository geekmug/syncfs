lib_LTLIBRARIES = libnpfs.la

libnpfs_la_SOURCES = \
	conn.c \
	error.c \
	fdtrans.c \
	fidpool.c \
	file.c \
	fmt.c \
	mount.c \
	np.c \
	pipesrv.c \
	socksrv.c \
	srv.c \
	trans.c \
	user.c

AM_CPPFLAGS = $(PTHREAD_CFLAGS)
AM_LDFLAGS = $(PTHREAD_CFLAGS)
LIBS = $(PTHREAD_FLAGS)
CC = $(PTHREAD_CC)

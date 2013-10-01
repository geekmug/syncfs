bin_PROGRAMS = clockstat concurio concurio_fork files

clockstat_SOURCES = clockstat.c
clockstat_LDADD = $(PTHREAD_LIBS)

concurio_SOURCES = concurio.c
concurio_LDADD = $(PTHREAD_LIBS)

concurio_fork_SOURCES = concurio_fork.c
concurio_fork_LDADD = $(PTHREAD_LIBS)

files_SOURCES = files.c

AM_CPPFLAGS = -Wall $(PTHREAD_CFLAGS)
AM_LDFLAGS = $(PTHREAD_CFLAGS)
CC = $(PTHREAD_CC)

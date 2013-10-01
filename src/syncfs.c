/* Copyright (C) 2008 by Scott Dial <scott@scottdial.com>
 *
 * Portions of syncfs are based on ramfs from npfs:
 *
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <config.h>

#define _XOPEN_SOURCE 500
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include <npfs.h>

static int syncfs_read(Npfilefid *fid, u64 offset, u32 count, u8 *data,
                       Npreq *req);
static int syncfs_write(Npfilefid *fid, u64 offset, u32 count, u8 *data,
                        Npreq *req);
static int syncfs_wstat(Npfile * file, Npstat *stat);
static void syncfs_destroy(Npfile *file);
static Npfile *syncfs_create(Npfile *dir, char *name, u32 perm, Npuser *uid,
                             Npgroup *gid, char *extension);
static Npfile *syncfs_first(Npfile *dir);
static Npfile *syncfs_next(Npfile *dir, Npfile *prevchild);
static int syncfs_remove(Npfile *dir, Npfile *file);
static Npfcall *syncfs_stat(Npfid *fid);

struct FileRev {
    u64 refs;
    u64 length;
    u64 datasize;
    u8 *data;
};
typedef struct FileRev FileRev;

struct File {
    pthread_mutex_t lock;
    pthread_mutex_t wlock;
    FileRev *fr_read;
    FileRev *fr_write;
};
typedef struct File File;

static Npsrv *srv;
static Npfile *root;
static Npfile *clkfile;
static u64 qidpath;
static int blksize;
static long int clkperiod;

struct NpfileList {
    Npfile *file;
    struct NpfileList *next;
};
typedef struct NpfileList NpfileList;

static NpfileList *filecommits = NULL;
static pthread_mutex_t filecommits_lock = PTHREAD_MUTEX_INITIALIZER;

struct LockList {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct LockList *next;
};
typedef struct LockList LockList;

static LockList *clockstaters = NULL;
static pthread_mutex_t clockstaters_lock = PTHREAD_MUTEX_INITIALIZER;

static char *Enospace = "no space left";

static Npdirops dirops = {
    .create = syncfs_create,
    .first = syncfs_first,
    .next = syncfs_next,
    .wstat = syncfs_wstat,
    .remove = syncfs_remove,
    .destroy = syncfs_destroy,
};

static Npfileops fileops = {
    .read = syncfs_read,
    .write = syncfs_write,
    .wstat = syncfs_wstat,
    .destroy = syncfs_destroy,
};

Npstr *npstr_of_str(char *str) {
    Npstr *nps;

    nps = malloc(sizeof(Npstr));
    if(nps == NULL)
        return NULL;
    nps->len = strlen(str);
    nps->str = strdup(str);

    return nps;
}

static void filecommits_add(Npfile *f) {
    NpfileList *first;

    first = malloc(sizeof(NpfileList));

    pthread_mutex_lock(&filecommits_lock);
    first->file = f;
    first->next = filecommits;
    filecommits = first;
    pthread_mutex_unlock(&filecommits_lock);
}

static Npfile *filecommits_pop(void) {
    Npfile *f;
    NpfileList *next;

    if(filecommits == NULL)
        return NULL;

    f = filecommits->file;
    next = filecommits->next;
    free(filecommits);
    filecommits = next;

    return f;
}

static FileRev *filerev_alloc(void) {
    FileRev *fr;

    fr = malloc(sizeof(FileRev));
    fr->refs = 1;
    fr->length = 0;
    fr->datasize = 0;
    fr->data = NULL;

    return fr;
}

static inline void filerev_ref(FileRev *fr) {
    fr->refs++;
}

static inline FileRev *filerev_unref(FileRev *fr) {
    fr->refs--;
    if(fr->refs <= 0) {
        if(fr->data)
            free(fr->data);
        free(fr);
        return NULL;
    }
    return fr;
}

static int filerev_truncate(FileRev *fr, u64 size) {
    int n;
    u8 *buf;

    if(fr->datasize >= size)
        return 0;

    n = (size / blksize + (size % blksize ? 1 : 0)) * blksize;
    buf = realloc(fr->data, n);
    if(!buf)
        return -1;

    fr->datasize = n;
    fr->data = buf;
    return 0;
}

static File *file_alloc(void) {
    File *f;

    f = malloc(sizeof(File));

    pthread_mutex_init(&f->lock, NULL);
    pthread_mutex_init(&f->wlock, NULL);

    f->fr_read = filerev_alloc();
    f->fr_write = NULL;

    return f;
}

static FileRev *file_readcopy(File *f) {
    pthread_mutex_lock(&f->lock);
    FileRev *fr = f->fr_read;
    filerev_ref(fr);
    pthread_mutex_unlock(&f->lock);

    FileRev *frw = filerev_alloc();
    frw->length = fr->length;
    filerev_truncate(frw, fr->length);
    memcpy(frw->data, fr->data, fr->length);

    pthread_mutex_lock(&f->lock);
    filerev_unref(fr);
    pthread_mutex_unlock(&f->lock);

    return frw;
}

static void syncfs_connclose(Npconn *conn) {
    /* Do nothing */
}

static int syncfs_read(Npfilefid *fid, u64 offset, u32 count, u8 *data,
                       Npreq *req) {
    int n;
    Npfile *file;
    File *f;

    file = fid->file;
    f = file->aux;
    n = count;

    pthread_mutex_lock(&f->lock);
    FileRev *fr = f->fr_read;
    filerev_ref(fr);
    pthread_mutex_unlock(&f->lock);

    if(fr->length < offset + count)
        n = fr->length - offset;

    if(n < 0)
        n = 0;
    memmove(data, fr->data + offset, n);

    pthread_mutex_lock(&f->lock);
    filerev_unref(fr);
    pthread_mutex_unlock(&f->lock);

    return n;
}

static int syncfs_write(Npfilefid *fid, u64 offset, u32 count, u8 *data,
                        Npreq *req) {
    int n;
    Npfile *file;
    File *f;

    file = fid->file;
    f = file->aux;

//    FileRev *fr = file_readcopy(f);
    FileRev *fr = filerev_alloc();

//    if(fid ->omode & Oappend)
//        offset = fr->length;
    offset = 0;

    n = count;
    if(fr->length < offset + count) {
        if(filerev_truncate(fr, offset + count)) {
            np_werror(Enospace, ENOSPC);
            return 0;
        }

        if(offset + count > fr->datasize) {
            if(fr->datasize - offset > 0)
                n = fr->datasize - offset;
            else
                n = 0;
        }

        if(n) {
            if(fr->length < offset)
                memset(fr->data + fr->length, 0, offset - fr->length);
            fr->length = offset + count;
        }
    }

    if(n)
        memmove(fr->data + offset, data, n);

    pthread_mutex_lock(&f->wlock);
    if(f->fr_write)
        filerev_unref(f->fr_write);
    f->fr_write = fr;
    pthread_mutex_unlock(&f->wlock);

    filecommits_add(file);

    return n;
}

static int syncfs_wstat(Npfile *file, Npstat *stat) {
    File *f;
    Npfile *nfile;
    char *sname, *oldname;
    int lockparent;
    u64 length, oldlength;
    u32 oldperm;
    u32 oldmtime;

    f = file->aux;
    oldlength = ~0;
    oldperm = ~0;
    oldmtime = ~0;
    oldname = NULL;

    FileRev *fr = file_readcopy(f);

    lockparent = stat->name.len != 0;
    if(lockparent)
        pthread_mutex_lock(&file->parent->lock);

    if(stat->name.len != 0) {
        sname = np_strdup(&stat->name);
        nfile = npfile_find(file->parent, sname);
        if(nfile) {
            free(sname);
            np_werror(Eexist, EEXIST);
            goto error;
        } else
            npfile_decref(nfile);

        oldname = file->name;
        file->name = sname;
    }

    if(stat->length != (u64) ~0) {
        oldlength = fr->length;
        length = stat->length;
        if(filerev_truncate(fr, length)) {
            np_werror(Enospace, ENOSPC);
            goto error;
        }

        if(length > fr->datasize)
            length = fr->datasize;

        if(fr->length < length)
            memset(fr->data + fr->length, 0,
                   length - fr->length);

        fr->length = length;
    }

    if(stat->mode != (u32) ~0) {
        oldperm = file->mode;
        file->mode = stat->mode;
    }

    if(stat->mtime != (u32) ~0) {
        oldmtime = file->mtime;
        file->mtime = stat->mtime;
    }

    free(oldname);
    if(lockparent)
        pthread_mutex_unlock(&file->parent->lock);

    if(stat->length != (u64) ~0) {
        pthread_mutex_lock(&f->wlock);
        if(f->fr_write)
            filerev_unref(f->fr_write);
        f->fr_write = fr;
        pthread_mutex_unlock(&f->wlock);

        filecommits_add(file);
    }

    return 1;

error:
    if(oldname) {
        free(file->name);
        file->name = oldname;
    }

    if(oldperm != ~0)
        file->mode = oldperm;

    if(oldmtime != ~0)
        file->mtime = oldmtime;

    if(oldlength != ~0)
        filerev_unref(fr);

    if(lockparent)
        pthread_mutex_unlock(&file->parent->lock);

    return 0;
}

static void syncfs_destroy(Npfile *file) {
    File *f;

    f = file->aux;
    pthread_mutex_lock(&f->wlock);
    pthread_mutex_lock(&f->lock);
    filerev_unref(f->fr_read);
    f->fr_read = NULL;
    if(f->fr_write) {
        filerev_unref(f->fr_write);
        f->fr_write = NULL;
    }
    pthread_mutex_unlock(&f->lock);
    pthread_mutex_unlock(&f->wlock);
    free(f);
}

static Npfile *syncfs_create(Npfile *dir, char *name, u32 perm, Npuser *uid,
                             Npgroup *gid, char *extension) {
    Npfile *file;
    File *d, *f;
    void *ops;

    if(perm & Dmlink) {
        np_werror(Eperm, EPERM);
        return NULL;
    }

    d = dir->aux;
    f = file_alloc();
    if(perm & Dmdir)
        ops = &dirops;
    else
        ops = &fileops;

    file = npfile_alloc(dir, name, perm, qidpath++, ops, f);
    file->uid = uid;
    file->gid = gid;
    file->muid = uid;
    npfile_incref(file);
    npfile_incref(file);

    if(dir->dirlast) {
        dir->dirlast->next = file;
        file->prev = dir->dirlast;
    } else
        dir->dirfirst = file;

    dir->dirlast = file;
    file->extension = strdup(extension);

    return file;
}

static Npfile *syncfs_first(Npfile *dir) {
    npfile_incref(dir->dirfirst);
    return dir->dirfirst;
}

static Npfile *syncfs_next(Npfile *dir, Npfile *prevchild) {
    npfile_incref(prevchild->next);
    return prevchild->next;
}

static int syncfs_remove(Npfile *dir, Npfile *file) {
    if(dir->dirfirst == file)
        dir->dirfirst = file->next;
    else
        file->prev->next = file->next;

    if(file->next)
        file->next->prev = file->prev;

    if(file == dir->dirlast)
        dir->dirlast = file->prev;

    file->prev = NULL;
    file->next = NULL;
    file->parent = NULL;

    return 1;
}

static void syncfs_commit(void) {
    time_t now;

    now = time(NULL);

    pthread_mutex_lock(&filecommits_lock);

    Npfile *file;
    while((file = filecommits_pop()) != NULL) {
        File *f = file->aux;

        pthread_mutex_lock(&f->wlock);
        if(f->fr_write) {
            pthread_mutex_lock(&f->lock);

            FileRev *fr = f->fr_read;
            f->fr_read = f->fr_write;
            f->fr_write = NULL;
            filerev_unref(fr);

            pthread_mutex_lock(&file->lock);
            file->length = f->fr_read->length;
            file->mtime = now;
            pthread_mutex_unlock(&file->lock);

            pthread_mutex_unlock(&f->lock);
        }
        pthread_mutex_unlock(&f->wlock);
    }

    pthread_mutex_unlock(&filecommits_lock);
}

static Npfcall *syncfs_stat(Npfid *fid) {
    Npfilefid *f;
    Npfile *file;
    Npwstat wstat;

    f = fid->aux;
    file = f->file;

    if(file == clkfile) {
        LockList *locklist = malloc(sizeof(LockList));
        pthread_mutex_init(&locklist->lock, NULL);
        pthread_cond_init(&locklist->cond, NULL);

        pthread_mutex_lock(&locklist->lock);

        pthread_mutex_lock(&clockstaters_lock);
        locklist->next = clockstaters;
        clockstaters = locklist;
        pthread_mutex_unlock(&clockstaters_lock);

        pthread_cond_wait(&locklist->cond, &locklist->lock);

        pthread_mutex_unlock(&locklist->lock);

        pthread_cond_destroy(&locklist->cond);
        pthread_mutex_destroy(&locklist->lock);
        free(locklist);
    }

    pthread_mutex_lock(&file->lock);
    wstat.size = 0;
    wstat.type = 0;
    wstat.dev = 0;
    wstat.qid = file->qid;
    wstat.mode = file->mode;
    wstat.atime = file->atime;
    wstat.mtime = file->mtime;
    wstat.length = file->length;
    wstat.name = file->name;
    wstat.uid = file->uid->uname;
    wstat.gid = file->gid->gname;
    wstat.muid = file->muid->uname;
    wstat.extension = file->extension;
    wstat.n_uid = file->uid->uid;
    wstat.n_gid = file->gid->gid;
    wstat.n_muid = file->muid->uid;
    pthread_mutex_unlock(&file->lock);

    return np_create_rstat(&wstat, fid->conn->dotu);
}

static void syncfs_release_clockstaters(void) {
    LockList *locklist;

    pthread_mutex_lock(&clockstaters_lock);

    locklist = clockstaters;
    while(locklist != NULL) {
        LockList *locklistnext = locklist->next;
        pthread_mutex_lock(&locklist->lock);
        pthread_cond_signal(&locklist->cond);
        pthread_mutex_unlock(&locklist->lock);
        locklist = locklistnext;
    }
    clockstaters = NULL;

    pthread_mutex_unlock(&clockstaters_lock);
}

static int timespec_subtract(struct timespec *result, const struct timespec *x,
                             struct timespec *y) {
    if(x->tv_nsec < y->tv_nsec) {
        int nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
        y->tv_nsec -= 1000000000 * nsec;
        y->tv_sec += nsec;
    }
    if(x->tv_nsec - y->tv_nsec > 1000000000) {
        int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
        y->tv_nsec += 1000000000 * nsec;
        y->tv_sec -= nsec;
    }

    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_nsec = x->tv_nsec - y->tv_nsec;

    return x->tv_sec < y->tv_sec;
}

static void pthread_nanosleep(const struct timespec *req,
                              struct timespec *rem) {
    int result;
    pthread_cond_t timercond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t timerlock = PTHREAD_MUTEX_INITIALIZER;
    struct timespec timerexpires;

    clock_gettime(CLOCK_REALTIME, &timerexpires);
    timerexpires.tv_nsec += req->tv_nsec;
    timerexpires.tv_sec += req->tv_sec;
    if(timerexpires.tv_nsec >= 1000000000) {
        int nsec = timerexpires.tv_nsec / 1000000000;
        timerexpires.tv_nsec -= nsec * 1000000000;
        timerexpires.tv_sec += nsec;
    }

    pthread_mutex_lock(&timerlock);
    result = ~ETIMEDOUT;
    while(result != ETIMEDOUT)
        result = pthread_cond_timedwait(&timercond, &timerlock, &timerexpires);
    pthread_mutex_unlock(&timerlock);
}

static RETSIGTYPE sigint(int signnum) {
    exit(EXIT_FAILURE);
    return (RETSIGTYPE) 0;
}

static void
usage()
{
    fprintf(stderr, "syncfs: -n -d -m -w nthreads -b blocksize -p port "
                    "-c clkperiod\n");
    exit(-1);
}

int
main(int argc, char **argv)
{
    int c, debuglevel, nwthreads, nodetach, port, fd;
    pid_t pid;
    Npuser *user;
    char *logfile, *s;

    nodetach = 0;
    debuglevel = 0;
    blksize = getpagesize();
    nwthreads = 128;
    port = 10000;
    clkperiod = 100000000;
    logfile = "/tmp/syncfs.log";
    user = np_uname2user("nobody");
    while ((c = getopt(argc, argv, "ndmw:b:p:l:c:")) != -1) {
        switch (c) {
        case 'n':
            nodetach = 1;
            break;

        case 'd':
            debuglevel = 1;
            break;

        case 'm':
#ifdef HAVE_MLOCKALL
            mlockall(MCL_CURRENT | MCL_FUTURE);
#else
            fprintf(stderr, "warning: mlockall() is not available.\n");
#endif
            break;

        case 'b':
            blksize = strtol(optarg, &s, 10);
            if(*s != '\0')
                usage();
            break;

        case 'w':
            nwthreads = strtol(optarg, &s, 10);
            if (*s != '\0')
                usage();
            break;

        case 'p':
            port = strtol(optarg, &s, 10);
            if(*s != '\0')
                usage();
            break;

        case 'c':
            clkperiod = strtol(optarg, &s, 10) * 1000000;
            if(*s != '\0')
                usage();
            break;

        case 'l':
            logfile = optarg;
            break;

        default:
            fprintf(stderr, "invalid option\n");
        }
    }

//    if(optind >= argc)
//        usage();

    if(!user) {
        fprintf(stderr, "invalid user\n");
        return -1;
    }

    printf("syncfs server starting\n");

    if(!nodetach) {
        fd = open(logfile, O_WRONLY | O_APPEND | O_CREAT, 0666);
        if(fd < 0) {
            fprintf(stderr, "cannot open log file %s: %d", logfile, errno);
            return -1;
        }

        close(0);
        close(1);
        close(2);
        if(dup2(fd, 2) < 0) {
            fprintf(stderr, "dup failed: %d\n", errno);
            return -1;
        }

        pid = fork();
        if (pid < 0)
            return -1;
        else if (pid != 0)
            return 0;

        setsid();
        chdir("/");
    }

    struct sched_param sp;
    sp.sched_priority = 50;
    sched_setscheduler(0, SCHED_FIFO, &sp); // SCHED_RR

    signal(SIGINT, sigint);

    qidpath = 0;
    root = npfile_alloc(NULL, strdup(""), 0755|Dmdir, qidpath++, &dirops,
                        file_alloc());
    npfile_incref(root);
    root->parent = root;
    npfile_incref(root);
    root->atime = time(NULL);
    root->mtime = root->atime;
    root->uid = user;
    root->gid = user->dfltgroup;
    root->muid = user;

    Npfid dummyfid;
    dummyfid.aux = root;
    dummyfid.user = user;
    npfile_create(&dummyfid, npstr_of_str("clock"), 0666, 0, npstr_of_str(""));

    srv = np_socksrv_create_tcp(nwthreads, &port);
    if(!srv)
        return -1;

    srv->debuglevel = debuglevel;
    srv->connclose = syncfs_connclose;
    npfile_init_srv(srv, root);
    srv->stat = syncfs_stat;

    np_srv_start(srv);

    struct timespec last;
    clock_gettime(CLOCK_REALTIME, &last);

    clkfile = npfile_find(root, "clock");
    File *f = clkfile->aux;
    u64 clkval = 0;
    for(;;) {
        struct timespec start;
        clock_gettime(CLOCK_REALTIME, &start);

        char clkstr[64], clkstrlen;
        sprintf(clkstr, "{\"clock\":%ld,\"interval\":%ld}\n",
                        (long int) clkval, clkperiod);
        clkstrlen = strlen(clkstr);

        FileRev *fr = filerev_alloc();
        filerev_truncate(fr, clkstrlen);
        memcpy(fr->data, clkstr, clkstrlen);
        fr->length = clkstrlen;

        pthread_mutex_lock(&f->wlock);
        if(f->fr_write)
            filerev_unref(f->fr_write);
        f->fr_write = fr;
        pthread_mutex_unlock(&f->wlock);

        filecommits_add(clkfile);

        syncfs_commit();

        syncfs_release_clockstaters();

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        struct timespec diff1;
        timespec_subtract(&diff1, &now, &last);

        memcpy(&last, &now, sizeof(struct timespec));

        struct timespec diff2;
        timespec_subtract(&diff2, &now, &start);

//        printf("%ld\n", diff1.tv_nsec);
//        printf("%ld\n", diff2.tv_nsec);

        struct timespec wait;
        wait.tv_sec = 0;
        wait.tv_nsec = clkperiod - diff2.tv_nsec;
        pthread_nanosleep(&wait, NULL);

        clkval++;
    }

    return 0;
}

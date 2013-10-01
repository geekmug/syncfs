#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

//#define USE_SLEEP 1

static char *root;
static int workers;
static int trials;
static int record_absolute;
static int do_read;
static int record_total_delay;
static int block_size;

static struct timeval asbolute_start;
static int workers_alive_wfd;
static int *worker_rfd;

int timeval_subtract(struct timeval *result, struct timeval *x,
                     struct timeval *y) {
    if(x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if(x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    return x->tv_sec < y->tv_sec;
}

#ifdef USE_SLEEP
static void pthread_usleep(unsigned int usecs) {
    int result;
    pthread_cond_t timercond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t timerlock = PTHREAD_MUTEX_INITIALIZER;
    struct timespec timerexpires;

    clock_gettime(CLOCK_REALTIME, &timerexpires);
    timerexpires.tv_nsec += usecs * 1000;
    if(timerexpires.tv_nsec >= 1000000000) {
        timerexpires.tv_sec += timerexpires.tv_nsec / 1000000000;
        timerexpires.tv_nsec = timerexpires.tv_nsec % 1000000000;
    }

    pthread_mutex_lock(&timerlock);
    result = ~ETIMEDOUT;
    while(result != ETIMEDOUT)
        result = pthread_cond_timedwait(&timercond, &timerlock, &timerexpires);
    pthread_mutex_unlock(&timerlock);
}
#endif

int worker_run(int id) {
    int result;

    char clkpath[256];
    sprintf(clkpath, "%s/clock", root);

    int clkfd = open(clkpath, O_RDONLY);
    if(clkfd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    char testpath[256];
    sprintf(testpath, "%s/%d", root, id);

    int fd = open(testpath, O_RDWR | O_CREAT, 0777);
    if(fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    int *rfd;
    if(do_read) {
        write(workers_alive_wfd, "x", 1);
        char rbuf[1];
        result = read(worker_rfd[id], rbuf, 1);
        if(result < 0) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        rfd = malloc(sizeof(int) * workers);

        int i;
        for(i = 0; i < workers; i++) {
            if(i == id)
                continue;
            sprintf(testpath, "%s/%d", root, id);
        
            rfd[i] = open(testpath, O_RDONLY);
            if(fd < 0) {
                perror("open");
                exit(EXIT_FAILURE);
            }
        }
    }

    char *buf = malloc(block_size);
    if(buf == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memset(buf, 'x', sizeof(buf));
    ((int *) buf)[0] = id;

    char *rbuf;
    if(do_read) {
        rbuf = malloc(block_size);
        if(rbuf == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
    } else {
        rbuf = malloc(1);
        if(rbuf == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
    }

    uint64_t *deltas = malloc(sizeof(uint64_t) * trials * 2);

    write(workers_alive_wfd, "x", 1);
    result = read(worker_rfd[id], rbuf, 1);
    if(result < 0) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    struct stat statbuf;
    if(fstat(clkfd, &statbuf) < 0) {
        perror("fstat");
        exit(EXIT_FAILURE);
    }

    int t;
    for(t = 0; t < trials; t++) {
        struct timeval before;
        if(record_total_delay)
            gettimeofday(&before, NULL);

        if(do_read) {
            int i;
            for(i = 0; i < workers; i++) {
                if(i == id)
                    continue;
                if(lseek(rfd[i], 0, SEEK_SET) < 0) {
                    perror("lseek");
                    exit(EXIT_FAILURE);
                }
                if(read(rfd[i], rbuf, block_size) < 0) {
                    perror("write");
                    exit(EXIT_FAILURE);
                }
            }
        }

        if(!record_total_delay)
            gettimeofday(&before, NULL);

        if(lseek(fd, 0, SEEK_SET) < 0) {
            perror("lseek");
            exit(EXIT_FAILURE);
        }
        if(write(fd, buf, block_size) < 0) {
            perror("write");
            exit(EXIT_FAILURE);
        }

        struct timeval after;
        gettimeofday(&after, NULL);

        struct timeval diff;
        if(record_absolute)
            timeval_subtract(&diff, &after, &asbolute_start);
        else
            timeval_subtract(&diff, &after, &before);

        deltas[t] = (diff.tv_sec * 1000000) + diff.tv_usec;

#ifdef USE_SLEEP
        pthread_usleep(49000);
#else
        if(fstat(clkfd, &statbuf) < 0) {
            perror("fstat");
            exit(EXIT_FAILURE);
        }
#endif
    }

    write(workers_alive_wfd, "x", 1);
    result = read(worker_rfd[id], rbuf, 1);
    if(result < 0) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    for(t = 0; t < trials; t++)
        printf("%d: %llu\n", id, deltas[t]);
    fflush(stdout);

    write(workers_alive_wfd, "x", 1);

    if(do_read)
        free(rfd);
    free(deltas);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    int result;

    if(argc < 4) {
        printf("Usage: concurio_fork [mount-point] [workers] [trials] [-a]\n");
        exit(EXIT_FAILURE);
    }

    root = argv[1];
    workers = strtol(argv[2], NULL, 10);
    trials = strtol(argv[3], NULL, 10);
    record_absolute = 0;
    do_read = 0;
    record_total_delay = 0;
    block_size = 1024;

    int opt;
    while((opt = getopt(argc - 3, &argv[3], "artb:")) != -1) {
        switch(opt) {
        case 'a':
            record_absolute = 1;
            break;
        case 'r':
            do_read = 1;
            break;
        case 't':
            record_total_delay = 1;
            break;
        case 'b':
            block_size = atoi(optarg);
            if(block_size < 1) {
                printf("Bad block size: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        default:
            printf("Unknown option: %c\n", opt);
            printf("Usage: concurio_fork [mount-point] [workers] [trials] [-a] [-r] [-t] [-b size]\n");
            exit(EXIT_FAILURE);
        }
    }

    gettimeofday(&asbolute_start, NULL);

    worker_rfd = malloc(sizeof(int) * workers);
    int *worker_wfd = malloc(sizeof(int) * workers);

    int w;
    for(w = 0; w < workers; w++) {
        int pfd[2];

        result = pipe(pfd);
        if(result < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        worker_rfd[w] = pfd[0];
        worker_wfd[w] = pfd[1];
    }

    int pfd[2];

    result = pipe(pfd);
    if(result < 0) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    int workers_alive_rfd = pfd[0];
    workers_alive_wfd = pfd[1];

    pid_t pid;
    for(w = 0; w < workers; w++) {
        pid = fork();
        if(pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if(pid == 0) {
            exit(worker_run(w));
        }
    }

    if(do_read) {
        /* Wait for other workers to create their target file */
        int workers_alive = 0;
        while(workers_alive < workers) {
            char buf[256];
            result = read(workers_alive_rfd, buf, 256);
            if(result < 0) {
                perror("read");
                exit(EXIT_FAILURE);
            }
            workers_alive += result;
        }
    
        /* Tell the workers to open (for read) all of the files */
        for(w = 0; w < workers; w++)
            write(worker_wfd[w], "x", 1);
    }

    /* Wait for other workers to start */
    int workers_alive = 0;
    while(workers_alive < workers) {
        char buf[256];
        result = read(workers_alive_rfd, buf, 256);
        if(result < 0) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        workers_alive += result;
    }

    /* Tell the workers to run */
    for(w = 0; w < workers; w++)
        write(worker_wfd[w], "x", 1);

    /* Wait for other workers to finish */
    while(workers_alive > 0) {
        char buf[256];
        result = read(workers_alive_rfd, buf, 256);
        if(result < 0) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        workers_alive -= result;
    }

    if(record_absolute)
        printf("absolute\n");
    else
        printf("write-time\n");
    fflush(stdout);

    /* Tell the workers to print the deltas */
    for(w = 0; w < workers; w++) {
        write(worker_wfd[w], "x", 1);
        do {
            char buf[1];
            result = read(workers_alive_rfd, buf, 1);
        } while(result < 1);
    }

    exit(EXIT_SUCCESS);
}

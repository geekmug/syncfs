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

static char *root;
static int workers;
static int trials;
static int record_absolute;
static struct timeval asbolute_start;

static pthread_mutex_t worker_sync_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_sync_cond = PTHREAD_COND_INITIALIZER;
static volatile int worker_sync_t = -1;
static volatile int workers_alive = 0;

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

void *worker_run(void *data) {
    int id = (int) data;

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

    char buf[1024];
    memset(buf, 'x', sizeof(buf));
    ((int *) buf)[0] = id;

    uint64_t *deltas = malloc(sizeof(uint64_t) * trials * 2);

    pthread_mutex_lock(&worker_sync_lock);
    workers_alive++;
    pthread_mutex_unlock(&worker_sync_lock);

    /* Wait for other workers to start */
    if(id == 0) {
        while(workers_alive < workers)
            pthread_usleep(100000);

        struct stat statbuf;
        if(fstat(clkfd, &statbuf) < 0) {
            perror("fstat");
            exit(EXIT_FAILURE);
        }
    }

    int t;
    for(t = 0; ; t++) {
        if(id == 0) {
            if(t >= trials && workers_alive == 1)
                break;
        } else {
            if(t >= trials)
                break;

            pthread_mutex_lock(&worker_sync_lock);
            while(worker_sync_t < t)
                pthread_cond_wait(&worker_sync_cond, &worker_sync_lock);
            pthread_mutex_unlock(&worker_sync_lock);
        }

        struct timeval before;
        gettimeofday(&before, NULL);

        if(lseek(fd, 0, SEEK_SET) < 0) {
            perror("lseek");
            exit(EXIT_FAILURE);
        }
        if(write(fd, buf, sizeof(buf)) < 0) {
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

        if(id == 0) {
            pthread_mutex_lock(&worker_sync_lock);
            worker_sync_t = t;
            pthread_cond_broadcast(&worker_sync_cond);
            pthread_mutex_unlock(&worker_sync_lock);

            pthread_usleep(49000);
        }
    }

    pthread_mutex_lock(&worker_sync_lock);
    workers_alive--;
    pthread_mutex_unlock(&worker_sync_lock);

    return deltas;
}

int main(int argc, char *argv[]) {
    if(argc < 4 || argc > 5) {
        printf("Usage: concurio [mount-point] [workers] [trials] [-a]\n");
        exit(EXIT_FAILURE);
    }

    root = argv[1];
    workers = strtol(argv[2], NULL, 10);
    trials = strtol(argv[3], NULL, 10);
    if(argc == 5 && strcmp(argv[4], "-a") == 0)
        record_absolute = 1;
    else
        record_absolute = 0;

    gettimeofday(&asbolute_start, NULL);

    pthread_t *worker_threads = malloc(sizeof(pthread_t) * workers);

    int w;
    for(w = 0; w < workers; w++)
        pthread_create(&worker_threads[w], NULL, worker_run, (void *) w);

    uint64_t **worker_deltas = malloc(sizeof(uint64_t *) * workers);
    for(w = 0; w < workers; w++)
        pthread_join(worker_threads[w], (void **) &worker_deltas[w]);

    if(record_absolute)
        printf("absolute\n");
    else
        printf("write-time\n");

    int t;
    for(w = 0; w < workers; w++) {
        for(t = 0; t < trials; t++)
            printf("%d: %llu\n", w, worker_deltas[w][t]);
        free(worker_deltas[w]);
    }

    exit(EXIT_SUCCESS);
}

#include <config.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

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

int main(int argc, char *argv[]) {
    if(argc != 3) {
        printf("Usage: clockstat [mount-point] [trials]\n");
        exit(EXIT_FAILURE);
    }

    char *root = argv[1];
    int trials = strtol(argv[2], NULL, 10);

    char clkpath[256];
    strcpy(clkpath, root);
    strcat(clkpath, "/clock");

    int clkfd = open(clkpath, O_RDONLY);
    if(clkfd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    struct stat buf;
    if(fstat(clkfd, &buf) < 0) {
        perror("fstat");
        exit(EXIT_FAILURE);
    }

    struct timeval last;
    gettimeofday(&last, NULL);

    int t;
    for(t = 0; t < trials; t++) {
        if(fstat(clkfd, &buf) < 0) {
            perror("fstat");
            exit(EXIT_FAILURE);
        }

        struct timeval now;
        gettimeofday(&now, NULL);

        struct timeval diff;
        timeval_subtract(&diff, &now, &last);

        printf("%u\n", (unsigned int) diff.tv_usec);

        memcpy(&last, &now, sizeof(struct timeval));
    }

    return EXIT_SUCCESS;
}

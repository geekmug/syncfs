#include <config.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_FILE_SIZE 1024

int randomly_fill(char *buf) {
    int size = (random() % MAX_FILE_SIZE) + 1;

    int i;
    for(i = 0; i < size; i++) {
        int r = random();
        buf[i] += (r & 0xFF000000);
        buf[i] += (r & 0x00FF0000);
        buf[i] += (r & 0x0000FF00);
        buf[i] += (r & 0x000000FF);
    }

    return size;
}

int main(int argc, char *argv[]) {
    int result;

    if(argc < 3) {
        printf("Usage: files [mount-point] [trials]\n");
        exit(EXIT_FAILURE);
    }

    char *root = argv[1];
    int trials = strtol(argv[2], NULL, 10);

    char clkpath[256];
    sprintf(clkpath, "%s/clock", root);

    int clkfd = open(clkpath, O_RDONLY);
    if(clkfd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    char testpath[256];
    sprintf(testpath, "%s/files_test", root);

    int fd = open(testpath, O_RDWR | O_CREAT, 0777);
    if(fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    char *buf = malloc(MAX_FILE_SIZE);
    char *rbuf = malloc(MAX_FILE_SIZE);

    int buf_size = randomly_fill(buf);

    if(lseek(fd, 0, SEEK_SET) < 0) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }
    if(write(fd, buf, buf_size) < 0) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    int i;
    for(i = 0; i < trials; i++) {
        struct stat statbuf;
        if(fstat(clkfd, &statbuf) < 0) {
            perror("fstat");
            exit(EXIT_FAILURE);
        }

        if(lseek(fd, 0, SEEK_SET) < 0) {
            perror("lseek");
            exit(EXIT_FAILURE);
        }
        result = read(fd, rbuf, MAX_FILE_SIZE);
        if(result < 0) {
            perror("write");
            exit(EXIT_FAILURE);
        }

        if(result != buf_size) {
            fprintf(stderr, "File size does not match! (%d != %d)\n",
                    buf_size, result);
            exit(EXIT_FAILURE);
        }
        if(memcmp(buf, rbuf, buf_size) != 0) {
            fprintf(stderr, "File contents do not match!\n");
            exit(EXIT_FAILURE);
        }

        buf_size = randomly_fill(buf);

        if(lseek(fd, 0, SEEK_SET) < 0) {
            perror("lseek");
            exit(EXIT_FAILURE);
        }
        if(write(fd, buf, buf_size) < 0) {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }

    exit(EXIT_SUCCESS);
}

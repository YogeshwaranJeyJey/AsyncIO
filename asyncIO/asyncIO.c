#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <liburing.h>
#include <sys/types.h>
#include <sys/stat.h>

#define IOU_DEFAULT_FILE "/tmp/simple_io_uring.bin"
#define IOU_DEFAULT_BLK 4096
#define IOU_DEFAULT_MB 64
#define IOU_QDEPTH 64

static inline void iou_fill_buffer(char *buf, int sz){
    unsigned char *u = (unsigned char*)buf;
    for(int i = 0; i < sz; ++i) u[i] = (unsigned char)(i & 0xFF);
}

static inline long long iou_time_ns(const struct timespec *a, const struct timespec *b){
    return (b->tv_sec - a->tv_sec) * 1000000000LL + (b->tv_nsec - a->tv_nsec);
}

static int iou_init_ring(struct io_uring *ring, unsigned entries){
    return io_uring_queue_init(entries, ring, 0);
}

static long long iou_submit_write_batch(struct io_uring *ring, int fd, char *buf, int bs, off_t start_offset, int iterations){
    int qdepth = IOU_QDEPTH;
    int inflight = 0;
    struct io_uring_cqe *cqe;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for(int i = 0; i < iterations; ++i){
        off_t offset = start_offset + (off_t)i * bs;
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
        while(!sqe){
            io_uring_submit(ring);
            sqe = io_uring_get_sqe(ring);
        }
        io_uring_prep_write(sqe, fd, buf, bs, offset);
        ++inflight;
        if(inflight >= qdepth){
            int submitted = io_uring_submit(ring);
            for(int j = 0; j < submitted; ++j){
                io_uring_wait_cqe(ring, &cqe);
                if(cqe->res < 0){
                    io_uring_cqe_seen(ring, cqe);
                    return -1;
                }
                io_uring_cqe_seen(ring, cqe);
            }
            inflight = 0;
        }
    }
    if(inflight > 0){
        int submitted = io_uring_submit(ring);
        for(int j = 0; j < submitted; ++j){
            io_uring_wait_cqe(ring, &cqe);
            if(cqe->res < 0){
                io_uring_cqe_seen(ring, cqe);
                return -1;
            }
            io_uring_cqe_seen(ring, cqe);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return iou_time_ns(&t0, &t1);
}

static void iou_run_write_benchmark(const char *path, int blk_sz, int total_mb){
    int total_bytes = total_mb * 1024 * 1024;
    if(blk_sz <= 0) return;
    int iterations = total_bytes / blk_sz;
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if(fd < 0){
        perror("open");
        return;
    }
    char *buf = malloc(blk_sz);
    if(!buf){
        perror("malloc");
        close(fd);
        return;
    }
    iou_fill_buffer(buf, blk_sz);
    struct io_uring ring;
    if(iou_init_ring(&ring, IOU_QDEPTH) < 0){
        perror("io_uring_queue_init");
        free(buf);
        close(fd);
        return;
    }
    long long total_ns = iou_submit_write_batch(&ring, fd, buf, blk_sz, 0, iterations);
    if(total_ns < 0){
        fprintf(stderr, "io_uring write error\n");
    } else {
        double avg_ms = (double)total_ns / iterations / 1e6;
        printf("Write Benchmark: %d ops, avg time = %.6f ms\n", iterations, avg_ms);
    }
    io_uring_queue_exit(&ring);
    free(buf);
    close(fd);
}

static void iou_show_menu(){
    printf("\n1) Run Write Benchmark\n2) Exit\nChoose an option: ");
}

int main(){
    const char *file = IOU_DEFAULT_FILE;
    int blk = IOU_DEFAULT_BLK;
    int mb = IOU_DEFAULT_MB;
    int choice = 0;
    do{
        iou_show_menu();
        if(scanf("%d", &choice) != 1) break;
        switch(choice){
            case 1:
                iou_run_write_benchmark(file, blk, mb);
                break;
            case 2:
                break;
            default:
                printf("Invalid option\n");
        }
    } while(choice != 2);
    return 0;
}

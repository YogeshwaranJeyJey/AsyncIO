#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_FILES 10

typedef struct {
    char *filename;
    char *content;
    long long completion_ns;
    size_t size;
} FileTask;

static FileTask results[MAX_FILES];
static int file_count = 0;

// Utility: high-resolution monotonic clock in nanoseconds
static inline long long get_nanotime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Thread worker: read one file
void *thread_read_file(void *arg) {
    int idx = *(int *)arg;
    free(arg); // free the heap index memory

    const char *filename = results[idx].filename;
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        pthread_exit(NULL);
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat failed");
        close(fd);
        pthread_exit(NULL);
    }

    results[idx].size = st.st_size;
    results[idx].content = malloc(results[idx].size + 1);
    if (!results[idx].content) {
        perror("malloc failed");
        close(fd);
        pthread_exit(NULL);
    }

    ssize_t read_bytes = read(fd, results[idx].content, results[idx].size);
    if (read_bytes < 0) {
        perror("read failed");
        free(results[idx].content);
        close(fd);
        pthread_exit(NULL);
    }

    results[idx].content[read_bytes] = '\0';
    close(fd);

    results[idx].completion_ns = get_nanotime();
    pthread_exit(NULL);
}

// Comparison for qsort
int compare_by_time(const void *a, const void *b) {
    const FileTask *fa = (const FileTask *)a;
    const FileTask *fb = (const FileTask *)b;
    if (fa->completion_ns < fb->completion_ns) return -1;
    if (fa->completion_ns > fb->completion_ns) return 1;
    return 0;
}

// Merge and print results in order of completion
void merge_and_print_results() {
    qsort(results, file_count, sizeof(FileTask), compare_by_time);

    printf("\n--- Merged Output (by completion time) ---\n\n");
    for (int i = 0; i < file_count; i++) {
        printf(">>> From file: %s\n", results[i].filename);
        printf("%s\n", results[i].content);
    }
}

int main() {
    printf("Enter number of files (max %d): ", MAX_FILES);
    scanf("%d", &file_count);

    if (file_count <= 0 || file_count > MAX_FILES) {
        fprintf(stderr, "Invalid file count\n");
        return 1;
    }

    for (int i = 0; i < file_count; i++) {
        char buffer[256];
        printf("Enter path for file %d: ", i + 1);
        scanf("%255s", buffer);

        results[i].filename = strdup(buffer);
        results[i].content = NULL;
        results[i].completion_ns = 0;
        results[i].size = 0;
    }

    pthread_t threads[MAX_FILES];
    for (int i = 0; i < file_count; i++) {
        int *arg = malloc(sizeof(int));
        *arg = i;
        if (pthread_create(&threads[i], NULL, thread_read_file, arg) != 0) {
            perror("pthread_create failed");
            return 1;
        }
    }

    for (int i = 0; i < file_count; i++) {
        pthread_join(threads[i], NULL);
    }

    merge_and_print_results();

    for (int i = 0; i < file_count; i++) {
        free(results[i].content);
        free(results[i].filename);
    }

    return 0;
}

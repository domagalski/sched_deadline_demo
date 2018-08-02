#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <pthread.h>

// deadline scheduler, requires kernel 3.14
#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif

struct sched_attr {
    uint32_t size;              /* Size of this structure */
    uint32_t sched_policy;      /* Policy (SCHED_*) */
    uint64_t sched_flags;       /* Flags */
    int32_t  sched_nice;        /* Nice value (SCHED_OTHER, SCHED_BATCH) */
    uint32_t sched_priority;    /* Static priority (SCHED_FIFO, SCHED_RR) */
    /* Remaining fields are for SCHED_DEADLINE */
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

// wrap syscall to sched_setattr
int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags)
{
    return syscall(__NR_sched_setattr, pid, attr, flags);
}

// wrap syscall to sched_getattr
int sched_getattr(pid_t pid, struct sched_attr *attr, unsigned int size, unsigned int flags)
{
    return syscall(__NR_sched_getattr, pid, attr, size, flags);
}

// deadline args are for passing multiple args to pthread functions easily.
int time_deadline(long long int *, long long int *, int, uint64_t);
typedef struct {
    long long int *latency;
    long long int *delays;
    int n_lat;
    uint64_t runtime;
    uint64_t deadline;
    uint64_t period;
} deadline_args;

// Run a function using the deadline scheduler.
void *run_deadline(void *data)
{
    struct sched_attr attr;
    int x = 0, ret, n_lat;
    unsigned int flags = 0;
    long long int *latency, *delays;
    uint64_t period;
    deadline_args *args = (deadline_args *)data;

    // setup SCHED_DEADLINE
    attr.size = sizeof(attr);
    attr.sched_flags = 0;
    attr.sched_nice = 0;
    attr.sched_priority = 0;
    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = args -> runtime;
    attr.sched_deadline= args -> deadline;
    attr.sched_period  = args -> period;

    ret = sched_setattr(0, &attr, flags);
    if (ret < 0) {
        perror("sched_setattr");
        exit(-1);
    }

    // extract args to pass to time_deadline
    latency = args -> latency;
    delays = args -> delays;
    n_lat = args -> n_lat;
    period = args -> period;

    // run a function that does things
    time_deadline(latency, delays, n_lat, period);

    return NULL;
}

// generate timing data for SCHED_DEADLINE
int time_deadline(long long int *latency, long long int *delays, int n_lat, uint64_t period){
    int idx;
    struct timeval time1, time2;
    long long int t1, t2, prev_delay = 0;

    // initial time. since the first time call in the loop is immediately after
    // the first time call, it's not useful data and must be discarded.
    gettimeofday(&time1, NULL);
    t1 = time1.tv_sec * 1000000 + time1.tv_usec;

    // measure time delay between each iteration of the loop.
    for (idx = 0; idx<n_lat; idx++) {
        gettimeofday(&time2, NULL);
        t2 = time2.tv_sec * 1000000 + time2.tv_usec;
        latency[idx] = t2 - t1;
        delays[idx] = latency[idx] - period/1000;
        t1 = t2;

        // pause computation until the next SCHED_DEADLINE period.
        sched_yield();
    }
    return 0;
}

int main(int argc, char **argv){
    int idx, n_lat;
    long long int *latency, *delays;
    uint64_t runtime, deadline, period;
    deadline_args func_args;

    // parse arguments.
    if (argc != 5){
        printf("Usage: %s NUM_DELAYS RUNTIME DEADLINE PERIOD\n", argv[0]);
        exit(-1);
    }
    n_lat = atoi(argv[1]) + 1;
    runtime = strtoul(argv[2], NULL, 10);
    deadline = strtoul(argv[3], NULL, 10);
    period = strtoul(argv[4], NULL, 10);

    latency = (long long int *) malloc(n_lat*sizeof(long long int));
    delays = (long long int *) malloc(n_lat*sizeof(long long int));

    // populate the arguments of the run_deadline function
    func_args.latency = latency;
    func_args.delays = delays;
    func_args.n_lat = n_lat;
    func_args.runtime = runtime;
    func_args.deadline = deadline;
    func_args.period = period;

    // times are in nanoseconds
    printf("Runtime: %lu\n", runtime);
    printf("Deadline: %lu\n", deadline);
    printf("Period: %lu\n", period);

    // pthreads aren't strictly necessary, but the tutorial had them.
    pthread_t thread;
    pthread_create(&thread, NULL, run_deadline, (void *)&func_args);
    pthread_join(thread, NULL);

    // print the results. skip the first number since it's useless.
    for (idx = 1; idx < n_lat; idx++){
        printf("%lld %lld\n", latency[idx], delays[idx]);
    }

    free(delays);
    free(latency);
    return 0;
}

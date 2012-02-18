#ifndef TDNS_H
#define TDNS_H

#define MAX_IP_LENGTH INET6_ADDRSTRLEN
#define MAX_NAME_LENGTH 1025
#define MIN_RESOLVER_THREADS 2

#define MINARGS 3
#define USAGE "INPUT_FILE [INPUT_FILE ...] OUTPUT_FILE"
#define Q_SIZE 5
#define PROCESSING 1
#define FINISHED 0

struct reader_args {
    FILE *inputfp;
    queue *url_q;
    pthread_mutex_t *qmutex;
    pthread_mutex_t *randmutex;
};

struct consumer_args {
    FILE *outputfp;
    queue *url_q;
    int *reader_stat;
    pthread_mutex_t *qmutex;
    pthread_mutex_t *status_mutex;
    pthread_mutex_t *outmutex;
    pthread_mutex_t *randmutex;
};

/* Desc:    A thread safe sleep function that will sleep
 *          for a random amount of time between 0 and 100
 *          useconds.
 * Args:    A mutex to protect the rand function.
 * Return   0 on success. 1 on failure.
 */
int rsleep(pthread_mutex_t *randmutex);


/* Desc:    A thread safe wrapper for pushing to the queue.
 *          The queue is globally defined with name url_q.
 * Args:    item: a string ending with \0
 * Return:  0 on success. 1 on failure.
 */
int ts_queue_push(queue* url_q, pthread_mutex_t *qmutex,
        pthread_mutex_t *randmutex, char item[]);

/* Desc:    A thread safe wrapper for popping from the queue.
 *          The queue is globally defined with name url_q.
 * Return:  A pointer to the popped item. NULL is returned
 *          if either:
 *          1) The call has failed.
 *          2) The queue is empty and the readers are done.
 */
char *ts_queue_pop(queue *url_q, pthread_mutex_t *qmutex,
        pthread_mutex_t *randmutex,
        pthread_mutex_t *status_mutex,
        int *reader_stat);

/* Desc: Removes the first newline character in the string
 *          by setting it to '\0'.
 * Args: str: pointer to the string.
 * Return: 0 on success. 1 on failure.
 */
int removenl(int max_len, char *str);

/* Desc:    The reader/producer thread function.
 *          Reads from a file and writes to a queue.
 * Args:    Pointer to reader_args.
 * Return:  NULL
 */
void *reader(void *arg);

/* Desc:    The writer/consumer thread function.
 *          Reads from the queue and writes IPs
 *          to a file.
 * Args:    Pointer to consumer_args.
 * Return:  NULL
 */
void *writer(void *arg);

#endif

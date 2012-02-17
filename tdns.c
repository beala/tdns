/*
 * File: tdns.c
 * Author: Alex Beal
 * Description:
 *      A threaded DNS lookup program.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include "queue.h"
#include "util.h"

#define DEBUG

#define MAX_IP_LENGTH INET6_ADDRSTRLEN
#define MAX_NAME_LENGTH 1025

#define MAX_READ_BUF 1024
#define MINARGS 3
#define USAGE "INPUT_FILE [INPUT_FILE ...] OUTPUT_FILE"
#define SBUFSIZE 1025
#define Q_SIZE 1
#define CONSUMER_THREADS 3
#define PROCESSING 1
#define FINISHED 0

//pthread_mutex_t qmutex;
//queue url_q;
//int reader_status;

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

int rsleep(pthread_mutex_t *randmutex){
    int rsec;
    pthread_mutex_lock(randmutex);
    rsec = rand()%100;
    pthread_mutex_unlock(randmutex);
    usleep(rsec);
    return 0;
}

/* Desc:    A thread safe wrapper for pushing to the queue.
 *          The queue is globally defined with name url_q.
 * Args:    item: a string ending with \0
 * Return:  0 on success. 1 on failure.
 */
int ts_queue_push(queue* url_q, pthread_mutex_t *qmutex,
        pthread_mutex_t *randmutex, char item[]) {
    int rc;

    while(1){
        pthread_mutex_lock(qmutex);
        if(queue_is_full(url_q)){
            /* Unlock to allow consumers to pull from the queue. */
            pthread_mutex_unlock(qmutex);
            rsleep(randmutex);
            continue;
        } else {
            break;
            /* The mutex is still locked! */
        }
    }

    rc = queue_push(url_q, item);
    pthread_mutex_unlock(qmutex);
    if(rc != QUEUE_SUCCESS)
        return 1;

    return 0;
}

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
        int *reader_stat){
    char *itemp;

    /* Check if the queue is empty. */
    while(1){
        pthread_mutex_lock(qmutex);
        if(queue_is_empty(url_q)){
            /* Return NULL if the q is empty and the readers
             * are done. */
            pthread_mutex_lock(status_mutex);
            if((*reader_stat) == FINISHED){
                pthread_mutex_unlock(qmutex);
                pthread_mutex_unlock(status_mutex);
                return NULL;
            }
            pthread_mutex_unlock(qmutex);
            pthread_mutex_unlock(status_mutex);
            rsleep(randmutex);
            continue;
        } else {
            break;
            /* Mutex is still locked! */
        }
    }

    /* Pop from the queue and return the pointer */
    itemp = queue_pop(url_q);
    pthread_mutex_unlock(qmutex);

    return itemp;
}

/* Desc:    Copies string to a location in the heap.
 * Args:    string: the string to be copied, ending with \0.
 *            This will explode in your face if it doesn't have a
 *            \0.
 * Return:  A pointer to the copy. NULL on failure.
 */
char *alloc_str(char string[]) {
    int len = 0;
    int i;
    char *stringp;

    /* Get the length of string, including \0 */
    while(string[len++] != '\0')
        ;

    stringp = malloc(sizeof(char)*len);
    if(stringp == NULL) {
        return NULL;
    }

    for(i=0; i<len; i++){
        stringp[i] = string[i];
    }

    return stringp;
}
/* Desc: Removes the first newline character in the string
 *          by setting it to '\0'.
 * Args: str: pointer to the string.
 * Return: 0 on success. 1 on failure.
 */
int removenl(int max_len, char *str) {
    int len = strlen(str);
    int i;

    /* Traverse to '\n' (if it exists) then set it to
     * '\0'.
     */
    for(i=0; i<len && str[i] != '\n' && i<max_len; i++);
    str[i] = '\0';

    return 0;
}

void *reader(void *arg) {

    struct reader_args *args = arg;
    FILE *inputfp = args->inputfp;
    queue *url_q = args->url_q;
    pthread_mutex_t *qmutex = args->qmutex;
    pthread_mutex_t *randmutex = args->randmutex;
    char linebuf[MAX_NAME_LENGTH];
    char *heap_str;
    int rc;

    /* Read a line from the file and push it to the q */
    while(fgets(linebuf, MAX_NAME_LENGTH, inputfp) != NULL){
        /* Remove any newlines from the end of the URL and move it
         * to the heap. */
        removenl(MAX_NAME_LENGTH, linebuf);
        heap_str = alloc_str(linebuf);
        if(heap_str == NULL){
            fprintf(stderr, "Error copying string to the heap.\n");
            return NULL;
        }
        rc = ts_queue_push(url_q, qmutex, randmutex, heap_str);
        if(rc == 1){
            fprintf(stderr, "There was an error pushing to the queue.\n");
            break;
        }
    }

    return NULL;
}

void *writer(void *arg) {
    struct consumer_args *args = arg;
    char *str;
    char ip_str[MAX_IP_LENGTH];
    int rc;
    FILE *outputfp = args->outputfp;
    queue *url_q = args->url_q;
    int *reader_stat = args->reader_stat;
    pthread_mutex_t *qmutex = args->qmutex;
    pthread_mutex_t *status_mutex = args->status_mutex;
    pthread_mutex_t *outmutex = args->outmutex;
    pthread_mutex_t *randmutex = args->randmutex;

    while(1){
        str = ts_queue_pop(url_q, qmutex, randmutex,
                status_mutex, reader_stat);
        /* If a NULL ptr is returned, either:
         * 1) An error occured.
         * 2) The queue is empty and the readers are done. */
        if(str == NULL)
            break;
        rc = dnslookup(str, ip_str, MAX_IP_LENGTH);
        if(rc == UTIL_FAILURE){
            fprintf(stderr, "Failure looking up %s.\n", str);
            /* Empty ip_str because it probably contains junk */
            ip_str[0] = '\0';
        }
        /* Write the URL and IP to the file */
        pthread_mutex_lock(outmutex);
        fprintf(outputfp, "%s, %s\n", str, ip_str);
        pthread_mutex_unlock(outmutex);
        /* Remove the string from the heap */
        free(str);
    }

    return NULL;
}

int main(int argc, char *argv[]){

    /* File vars */
    const int inputfc = argc - 2;     // Number of input files.
    FILE *outputfp;             // Pointer to the output file.
    FILE *inputfps[inputfc];
    /* Threads vars */
    pthread_t rthreads[inputfc];
    pthread_t wthreads[CONSUMER_THREADS];
    struct reader_args rargs[inputfc]; // Array of arguments for reader threads
    struct consumer_args cargs[CONSUMER_THREADS]; // Array of arguments for reader threads
    /* Queue vars */
    queue url_q;
    pthread_mutex_t qmutex;
    /* Reader args */
    int reader_stat;
    pthread_mutex_t status_mutex;
    /* Consumer vars */
    pthread_mutex_t outmutex;
    /* Misc vars */
    pthread_mutex_t randmutex;
    char errorstr[SBUFSIZE];
    int i, rc;

    /* Check the args */
    if(argc < MINARGS){
        fprintf(stderr, "Not enough arguments: %d\n", (argc - 1));
        fprintf(stderr, "Usage:\n %s %s\n", argv[0], USAGE);
        return EXIT_FAILURE;
    }

    /* Open the input files */
    for(i=0; i<inputfc; i++){
#ifdef DEBUG
        printf("Opening input: %s @ %d\n", argv[i+1], i);
#endif
        inputfps[i] = fopen(argv[i+1], "r");
        if(!inputfps[i]) {
            fprintf(stderr, "Error opening input file: %s", argv[i+1]);
            perror(errorstr);
            return EXIT_FAILURE;
        }
    }

    /* Open the output file */
    outputfp = fopen(argv[argc-1], "w");
    if(!outputfp){
        perror("Error opening ouput file.");
        return EXIT_FAILURE;
    }

    /* Init the url queue */
    rc = queue_init(&url_q, Q_SIZE);
    if(rc == QUEUE_FAILURE){
        fprintf(stderr, "Error initializing the queue.\n");
        return EXIT_FAILURE;
    }

    /* Spawn reader threads */
    /* Init reader vars */
    reader_stat = PROCESSING;
    pthread_mutex_init(&qmutex, NULL);
    pthread_mutex_init(&randmutex, NULL);
    for(i = 0; i < inputfc; i++) {
        /* Init reader arg struct */
        rargs[i].inputfp = inputfps[i];
        rargs[i].url_q = &url_q;
        rargs[i].qmutex = &qmutex;
        rargs[i].randmutex = &randmutex;
        rc = pthread_create(rthreads + i, NULL, reader, rargs + i);
        if(rc){
            fprintf(stderr, "ERROR: Return code from pthread_create() is %d\n", rc);
            return EXIT_FAILURE;
        }
    }

    /* Spawn writer threads */
    /* Init reader vars */
    pthread_mutex_init(&outmutex, NULL);
    pthread_mutex_init(&status_mutex, NULL);
    for(i = 0; i < CONSUMER_THREADS; i++) {
        /* Init consumer args struct */
        cargs[i].outputfp = outputfp;
        cargs[i].url_q = &url_q;
        cargs[i].reader_stat = &reader_stat;
        cargs[i].qmutex = &qmutex;
        cargs[i].status_mutex = &status_mutex;
        cargs[i].outmutex = &outmutex;
        cargs[i].randmutex = &randmutex;
        rc = pthread_create(wthreads + i, NULL, writer, cargs + i);
        if(rc){
            fprintf(stderr, "ERROR: Return code from pthread_create() is %d\n", rc);
            return EXIT_FAILURE;
        }
    }

    /* Join the reader threads before closing the files. */
    for(i = 0; i < inputfc; i++){
        pthread_join(rthreads[i], NULL);
    }
    /* The readers are finished. */
    pthread_mutex_lock(&status_mutex);
    reader_stat = FINISHED;
    pthread_mutex_unlock(&status_mutex);

    /* Close the input files */
    for(i=0; i<inputfc; i++){
        fclose(rargs[i].inputfp);
    }

    /* Join the reader threads before closing the files. */
    for(i = 0; i < CONSUMER_THREADS; i++){
        pthread_join(wthreads[i], NULL);
    }

    /* Close the ouput file */
    fclose(outputfp);

    /* Cleanup queue */
    queue_cleanup(&url_q);

    pthread_exit(NULL);
}

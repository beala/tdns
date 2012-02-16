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

#define MAX_READ_BUF 1024
#define MINARGS 3
#define USAGE "INPUT_FILE [INPUT_FILE ...] OUTPUT_FILE"
#define SBUFSIZE 1025
#define Q_SIZE 50
#define CONSUMER_THREADS 4
#define PROCESSING 1
#define FINISHED 0

//pthread_mutex_t qmutex;
//queue url_q;
//int reader_status;

struct reader_args {
    FILE *inputfp;
    queue *url_q;
    int *reader_stat;
    pthread_mutex_t *qmutex;
    pthread_mutex_t *status_mutex;
};

struct consumer_args {
    FILE *outputfp;
    queue *url_q;
    int *reader_stat;
    pthread_mutex_t *qmutex;
    pthread_mutex_t *status_mutex;
    pthread_mutex_t *outmutex;
};

/* Desc:    A thread safe wrapper for pushing to the queue.
 *          The queue is globally defined with name url_q.
 * Args:    item: a string ending with \0
 * Return:  0 on success. 1 on failure.
 */
int ts_queue_push(queue* url_q, pthread_mutex_t *qmutex,
        char item[]) {
    int rc;

    while(1){
        pthread_mutex_lock(qmutex);
        if(queue_is_full(url_q)){
            /* Unlock to allow consumers to pull from the queue. */
            pthread_mutex_unlock(qmutex);
            continue;
        } else {
            break;
            /* The mutex is still locked! */
        }
    }

    rc = queue_push(url_q, item);
    pthread_mutex_unlock(qmutex);
    if(rc != QUEUE_SUCCESS)
        exit(EXIT_FAILURE);

    return 0;
}

/* Desc:    A thread safe wrapper for popping from the queue.
 *          The queue is globally defined with name url_q.
 * Return:  A pointer to the popped item. NULL on failure.
 */
char *ts_queue_pop(queue *url_q, pthread_mutex_t *qmutex) {
    char *item;

    while(1){
        pthread_mutex_lock(qmutex);
        if(queue_is_empty(url_q)){
            pthread_mutex_unlock(qmutex);
            continue;
        } else {
            break;
            /* Mutex is still locked! */
        }
    }

    item = queue_pop(url_q);
    pthread_mutex_unlock(qmutex);

    return item;
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


void *reader(void *arg) {

    struct reader_args *args = arg;
    FILE *inputfp = args->inputfp;
    queue *url_q = args->url_q;
    int *reader_stat = args->reader_stat;
    pthread_mutex_t *qmutex = args->qmutex;
    pthread_mutex_t *status_mutex = args->status_mutex;
    char linebuf[SBUFSIZE];
    char *heap_str;

    while(fscanf(inputfp, "%s\n", linebuf) != EOF){
        heap_str = alloc_str(linebuf);
        if(heap_str == NULL)
            return NULL;
        ts_queue_push(url_q, qmutex, heap_str);
    }

    return NULL;
}

void *writer(void *arg) {
    struct consumer_args *args = arg;
    char *str;
    FILE *outputfp = args->outputfp;
    queue *url_q = args->url_q;
    int *reader_stat = args->reader_stat;
    pthread_mutex_t *qmutex = args->qmutex;
    pthread_mutex_t *status_mutex = args->status_mutex;
    pthread_mutex_t *outmutex = args->outmutex;

    while(1){
        str = ts_queue_pop(url_q, qmutex);
        printf("%s\n", str);
        return NULL;
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
            sprintf(errorstr, "Error opening input file: %s", argv[i+1]);
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
    pthread_mutex_init(&status_mutex, NULL);
    for(i = 0; i < inputfc; i++) {
        /* Init reader arg struct */
        rargs[i].inputfp = inputfps[i];
        rargs[i].url_q = &url_q;
        rargs[i].reader_stat = &reader_stat;
        rargs[i].qmutex = &qmutex;
        rargs[i].status_mutex = &status_mutex;
        rc = pthread_create(rthreads + i, NULL, reader, rargs + i);
        if(rc){
            printf("ERROR: Return code from pthread_create() is %d\n", rc);
            exit(EXIT_FAILURE);
        }
    }

    /* Spawn writer threads */
    /* Init reader vars */
    pthread_mutex_init(&outmutex, NULL);
    for(i = 0; i < CONSUMER_THREADS; i++) {
        /* Init consumer args struct */
        cargs[i].outputfp = outputfp;
        cargs[i].url_q = &url_q;
        cargs[i].reader_stat = &reader_stat;
        cargs[i].qmutex = &qmutex;
        cargs[i].status_mutex = &status_mutex;
        cargs[i].outmutex = &outmutex;
        rc = pthread_create(wthreads + i, NULL, writer, cargs + i);
        if(rc){
            printf("ERROR: Return code from pthread_create() is %d\n", rc);
            exit(EXIT_FAILURE);
        }
    }

    /* Join the reader threads before closing the files. */
    for(i = 0; i < inputfc; i++){
        pthread_join(rthreads[i], NULL);
    }
    reader_stat = FINISHED;

    /* Join the reader threads before closing the files. */
    for(i = 0; i < CONSUMER_THREADS; i++){
        pthread_join(wthreads[i], NULL);
    }

    /* Close the input files */
    for(i=0; i<inputfc; i++){
        fclose(rargs[i].inputfp);
    }

    /* Close the ouput file */
    fclose(outputfp);

    pthread_exit(NULL);
}





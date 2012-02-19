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
#include "multi-lookup.h"

int rsleep(pthread_mutex_t *randmutex){
    int rsec, rc;
    rc = pthread_mutex_lock(randmutex);
    if(rc != 0) {
        fprintf(stderr, "There was an error locking the mutex.\n");
        return 1;
    }
    rsec = rand()%100;
    rc = pthread_mutex_unlock(randmutex);
    if(rc != 0) {
        fprintf(stderr, "There was an error unlocking the mutex.\n");
        return 1;
    }
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
        rc = pthread_mutex_lock(qmutex);
        if(rc != 0) {
            fprintf(stderr, "There was an error locking the mutex.\n");
            return 1;
        }
        if(queue_is_full(url_q)){
            /* Unlock to allow consumers to pull from the queue. */
            rc = pthread_mutex_unlock(qmutex);
            if(rc != 0) {
                fprintf(stderr, "There was an error unlocking the mutex.\n");
                return 1;
            }
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
    int rc;

    /* Check if the queue is empty. */
    while(1){
        rc = pthread_mutex_lock(qmutex);
        if(rc != 0) {
            fprintf(stderr, "There was an error locking the mutex.\n");
            return NULL;
        }
        if(queue_is_empty(url_q)){
            /* Return NULL if the q is empty and the readers
             * are done. */
            rc = pthread_mutex_lock(status_mutex);
            if(rc != 0) {
                fprintf(stderr, "There was an error locking the mutex.\n");
                return NULL;
            }
            if((*reader_stat) == FINISHED){
                rc = pthread_mutex_unlock(qmutex);
                rc = pthread_mutex_unlock(status_mutex) || rc;
                if(rc != 0) {
                    fprintf(stderr, "There was an error unlocking the mutex.\n");
                    return NULL;
                }
                return NULL;
            }
            rc = pthread_mutex_unlock(qmutex);
            rc = pthread_mutex_unlock(status_mutex) || rc;
            if(rc != 0) {
                fprintf(stderr, "There was an error unlocking the mutex.\n");
                return NULL;
            }
            rsleep(randmutex);
            continue;
        } else {
            break;
            /* Mutex is still locked! */
        }
    }

    /* Pop from the queue and return the pointer */
    itemp = queue_pop(url_q);
    rc = pthread_mutex_unlock(qmutex);
    if(rc != 0) {
        fprintf(stderr, "There was an error unlocking the mutex.\n");
        return NULL;
    }

    return itemp;
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
        /* Remove any newlines from the end of the URL */
        removenl(MAX_NAME_LENGTH, linebuf);
        /* Skip blank lines */
        if(linebuf[0] == '\0')
            continue;
        /* Copy the string to the heap */
        heap_str = strndup(linebuf, MAX_NAME_LENGTH - 1);
        if(heap_str == NULL){
            fprintf(stderr, "Error copying string to the heap.\n");
            return NULL;
        }
        /* Push a ptr to the string onto the q */
        rc = ts_queue_push(url_q, qmutex, randmutex, heap_str);
        if(rc == 1){
            fprintf(stderr, "There was an error pushing to the queue.\n");
            return NULL;
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
            /* dnslookup prints an error, so no need to print
             * one here.
             * Empty ip_str because it probably contains junk */
            ip_str[0] = '\0';
        }
        /* Write the URL and IP to the file */
        rc = pthread_mutex_lock(outmutex);
        if(rc != 0){
            fprintf(stderr, "There was an error locking the mutex.\n");
            return NULL;
        }
        fprintf(outputfp, "%s, %s\n", str, ip_str);
        rc = pthread_mutex_unlock(outmutex);
        if(rc != 0){
            fprintf(stderr, "There was an error unlocking the mutex.\n");
            return NULL;
        }
        /* Remove the string from the heap */
        free(str);
    }

    return NULL;
}

int main(int argc, char *argv[]){

    /* File vars */
    int inputfc = argc - 2;     // Number of input files.
    FILE *outputfp;             // Pointer to the output file.
    FILE *inputfps[inputfc];
    /* Threads vars */
    pthread_t rthreads[inputfc];
    pthread_t *wthreads;
    struct reader_args rargs[inputfc]; // Array of arguments for reader threads
    struct consumer_args cargs;
    /* Queue vars */
    queue url_q;
    pthread_mutex_t qmutex;
    /* Reader args */
    int reader_stat;
    pthread_mutex_t status_mutex;
    /* Consumer vars */
    pthread_mutex_t outmutex;
    int core_count;
    /* Misc vars */
    pthread_mutex_t randmutex;
    int i, j, rc;

    /* Check the args */
    if(argc < MINARGS){
        fprintf(stderr, "Not enough arguments: %d\n", (argc - 1));
        fprintf(stderr, "Usage:\n %s %s\n", argv[0], USAGE);
        return EXIT_FAILURE;
    }

    /* Open the input files */
    j = 0; //argv index.
    for(i=0; i<inputfc; i++){
        inputfps[i] = fopen(argv[j+1], "r");
        j++;
        if(inputfps[i] == NULL) {
            fprintf(stderr, "Error opening input file: %s\n", argv[j]);
            perror("");
            /* Reduce the file count, and decrement i so the next
             * iteration will store to the same index */
            inputfc--;
            i--;
            continue;
        }
    }

    /* Check that there are input files */
    if(inputfc < 1){
        fprintf(stderr, "No valid input files. Terminating.\n");
        return EXIT_FAILURE;
    }

    /* Open the output file */
    outputfp = fopen(argv[argc-1], "w");
    if(!outputfp){
        perror("Error opening ouput file");
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
    rc = pthread_mutex_init(&qmutex, NULL);
    rc = pthread_mutex_init(&randmutex, NULL) || rc;
    if(rc != 0){
        fprintf(stderr, "There was an error initializing the mutex.\n");
        return EXIT_FAILURE;
    }
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
    /* Get the number of cores */
    core_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if(core_count < MIN_RESOLVER_THREADS)
        core_count = MIN_RESOLVER_THREADS;
    wthreads = malloc(sizeof(pthread_t)*core_count);
    if(wthreads == NULL) {
        fprintf(stderr, "Error mallocing.\n");
        return EXIT_FAILURE;
    }
    /* Init writer mutexes */
    rc = pthread_mutex_init(&outmutex, NULL);
    rc = pthread_mutex_init(&status_mutex, NULL) || rc;
    if(rc != 0){
        fprintf(stderr, "There was an error initializing the mutex.\n");
        return EXIT_FAILURE;
    }
    /* Init writer args */
    cargs.outputfp = outputfp;
    cargs.url_q = &url_q;
    cargs.reader_stat = &reader_stat;
    cargs.qmutex = &qmutex;
    cargs.status_mutex = &status_mutex;
    cargs.outmutex = &outmutex;
    cargs.randmutex = &randmutex;
    for(i = 0; i < core_count; i++) {
        /* Init consumer args struct */
        rc = pthread_create(wthreads + i, NULL, writer, &cargs);
        if(rc){
            fprintf(stderr, "ERROR: Return code from pthread_create() is %d\n", rc);
            return EXIT_FAILURE;
        }
    }

    /* Join the reader threads before closing the files. */
    for(i = 0; i < inputfc; i++){
        rc = pthread_join(rthreads[i], NULL);
        if(rc != 0){
            fprintf(stderr, "There was an error joining the threads.\n");
            return EXIT_FAILURE;
        }
    }
    /* The readers are finished. */
    rc = pthread_mutex_lock(&status_mutex);
    if(rc != 0){
        fprintf(stderr, "There was an error locking the mutex.\n");
        return EXIT_FAILURE;
    }
    reader_stat = FINISHED;
    rc = pthread_mutex_unlock(&status_mutex);
    if(rc != 0){
        fprintf(stderr, "There was an error unlocking the mutex.\n");
        return EXIT_FAILURE;
    }

    /* Close the input files */
    for(i=0; i<inputfc; i++){
        rc = fclose(rargs[i].inputfp);
        if(rc != 0){
            fprintf(stderr, "There was an error closing an input file. ");
            perror("");
        }
    }

    /* Join the reader threads before closing the files. */
    for(i = 0; i < core_count; i++){
        rc = pthread_join(wthreads[i], NULL);
        if(rc != 0){
            fprintf(stderr, "There was an error joining the threads.\n");
            return EXIT_FAILURE;
        }
    }

    /* Close the ouput file */
    rc = fclose(outputfp);
    if(rc != 0){
        fprintf(stderr, "There was an error closing the output file. ");
        perror("");
    }

    /* Free wthreads */
    free(wthreads);

    /* Cleanup queue */
    queue_cleanup(&url_q);

    pthread_exit(NULL);
}

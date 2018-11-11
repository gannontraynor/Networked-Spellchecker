#include "network.h"

void *worker_routine(void* args) {
    server *serv = args;

    //lock clientQ
    pthread_mutex_lock(&serv->client_mutex);

    //if client Q is empty
    while (serv->c_read_ptr == serv->c_write_ptr && serv->client_count == 0) {
        pthread_cond_wait(&serv->client_not_empty, &serv->client_mutex);
    }

    //get socket from clientQ
    int socket = remove_client(serv);

    //send signal that Q is not full
    pthread_cond_signal(&serv->client_not_full);

    //unlock mutex
    pthread_mutex_unlock(&serv->client_mutex);

    char *msgError = "Problem receiving message";
    int bytesReturned;
    char *res;
    char *word;

    //keep receiving words until the client disconnects
    while(1) {
        //zero-initialize var to receive word
        word = calloc(DICT_BUF, 1);

        //receive word
        bytesReturned = (int) recv(socket, word, DICT_BUF, 0);
        //printf("%s\n", word);

        //if there was an error in the message reception
        if (bytesReturned < 0) {
            send(socket, msgError, strlen(msgError), 0);
            continue;
        }

        //or if the client pressed escape key
        if(word[0] == 27) {
            send(socket, "Goodbye.\n", strlen("Goodbye.\n"), 0);
            break;
        }

        //search for word, set the result equal to whether it was found or not
        int iscorrect = lookup(word);
        iscorrect ? (res = "OK\n") : (res = "MISSPELLED\n");

        //print results to client
        send(socket, res, strlen(res), 0);

        //push the result to the log queue, get lock first
        pthread_mutex_lock(&serv->log_mutex);

        //check if the buffer is full
        while(serv->l_write_ptr == serv->l_read_ptr && serv->log_count == BUFFER_MAX) {
            pthread_cond_wait(&serv->log_not_full, &serv->log_mutex);
        }

        //write to the logQ
        insert_log(serv, word, iscorrect);

        //signal that log Q isn't empty
        pthread_cond_signal(&serv->log_not_empty);

        //unlock the mutex
        pthread_mutex_unlock(&serv->log_mutex);
    }
    close(socket);
    return NULL;
}

int remove_client(server *serv) {
    int socket = clients[serv->c_read_ptr];

    //clear the array index
    clients[serv->c_read_ptr] = 0;

    //increment the index, and optionally loop back to 0 if we reach end of buffer
    serv->c_read_ptr = (++serv->c_read_ptr) % BUFFER_MAX;

    //decrement the amount of clients in the buffer
    --serv->client_count;

    return socket;
}

void insert_log(server *serv, char *word, int iscorrect) {
    //var to hold complete log text
    char string[DICT_BUF];

    //clear buffer
    memset(string, '\0', sizeof(char) * DICT_BUF);

    //remove '\n'
    size_t len = strlen(word);
    word[len - 1] = '\0';

    //var to hold OK/MISSPELLED
    char *res;

    //set that var accordingly with quick test of argument passed
    iscorrect ? (res = "OK") : (res = "MISSPELLED");

    //"[word]" is [OK/MISSPELLED]
    strcpy(string, "\"");
    strcat(string, word);
    strcat(string, "\" is ");
    strcat(string, res);

    //push string to the queue
    strcpy(logs[serv->l_write_ptr], string);

    //increment the index, and optionally loop back to 0 if we reach end of buffer
    serv->l_write_ptr = (++serv->l_write_ptr) % BUFFER_MAX;

    //increment the amount of logs in the buffer
    ++(serv->log_count);
}
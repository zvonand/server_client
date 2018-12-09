#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>

typedef char * word;

struct clientStruc {
    word name;
    int descr;
    int nameSet;
};

typedef struct clientStruc * client;


int readString (int readFrom, char ** str) {
    /*
        Takes source descriptor (int) and destination as a pointer (char **)
        Returns number of bytes read, in case of read() failure returns -1
    */
    int allSize = 16, currpos = 0;
    word tmp;
    int buf_size = allSize;
    ssize_t bytes_read;
    char buf;
    *str = (word) malloc (allSize * sizeof(char));
    bytes_read = read (readFrom, &buf, sizeof (char));
    if (buf == '\n' ) {
        return 1;
    }
    if (bytes_read < 0) {
        return -1;
    }
    if (bytes_read == 0) {
        return 0;  //connection closed
    }
    while (buf != '\0' && buf != '\n') {
        (*str)[currpos] = buf;
        currpos++;
        if (allSize < (currpos + 1) * sizeof (char)) {
            allSize *= 2;
            *str = (word) realloc (*str, sizeof (char) * allSize);
        }
        bytes_read = read (readFrom, &buf, sizeof (char));
    }
    (*str)[currpos] = '\0';
    *str = (word) realloc (*str, sizeof(char) * (strlen (*str) + 1));
    return strlen (*str);
}


int sendString (int sendTo, char * str) {
    /*
        Takes destination descriptor and string as (char *)
        returns 0 if sent successfully, -1 in case of write () error
    */
    if (write (sendTo, str, (strlen (str) + 1)) == -1) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}


client initClient (int descr) {
    /*
        Returns a (struct clientStruc) with default values
    */
    client ret = (client) malloc (sizeof (struct clientStruc));
    ret->name = (word) calloc (10, sizeof (char));
    sprintf(ret->name, "%d", descr);
    ret->descr = descr;
    ret->nameSet = 0;
    return ret;
}


void freeClient (client * entry) {
    //Frees a (struct clientStruc) record
    free ((*entry)->name);
    free (*entry);
    *entry = 0;
}


int openListener (int port) {
    /*
        Takes port number, returns opened listener's descriptor
    */
    struct sockaddr_in addr;
    int ls = socket (AF_INET, SOCK_STREAM, 0);
    if (ls == -1) {
        printf ("Failed to open socket");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (0 != bind(ls, (struct sockaddr *) &addr, sizeof(addr))) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return -1;
    }
    if (-1 == listen(ls, 5)) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return -1;
    }
    return ls;

}


void broadcast (int serverMsg, int sender, int maxClients, word st, client * clients) {
    /*
        Helper function for actually sending messages (from clients and from server)
    */

    word tosend = (word) calloc ((strlen(st) + strlen(clients[sender]->name) + 5), sizeof (char));
    if (serverMsg) {
        strcat (tosend, "# ");
        strcat (tosend, clients[sender]->name);
        strcat (tosend, " ");
        strcat (tosend, st);
        //strcat (tosend, "\n");
    } else {
        tosend[0] = '[';
        strcat (tosend, clients[sender]->name);
        strcat (tosend, "]: ");
        strcat (tosend, st);
        //strcat (tosend, "\n");
    }
    printf ("%s\n", tosend);
    for (int i = 0; i < maxClients; i++) {
        if (i == sender || clients[i] == 0) {
            continue;
        }
        if (clients[i]->nameSet != 0 ) {
            sendString (clients[i]->descr, tosend);
        }
    }
    free (tosend);
}


int nameAvailabe (word name, int maxClients, client * clients) {
    /*
        Returns 1 if name can be chosen,
        0 if not
    */
    int ret = 1, i, strl = strlen (name);
    if (strl < 3 || strl > 16) {
        return 0;
    }

    for (i = 0; i < strl; i++) {
        if (((name[i] < 'a' && name[i] < 'A') || (name[i] > 'z' && name[i] > 'Z')) && (name[i] != '_')){
            return 0;
        }
    }

    for (i = 0; i < maxClients; i++) {
        if (clients[i] == 0) {
            continue;
        }
        if (0 == strcmp (clients[i]->name, name) ) {
            ret = 0;
            break;
        }
    }
    return ret;
}


void acceptNewClient (int * currentlyConnected, int lstnr, int maxClients, client * clients) {
    /*
        Accepts a new client and adds it to the (client *)
    */
    int tmp, i;
    tmp = accept (lstnr, NULL, NULL);
    if (tmp == -1) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return;
    }
    if (*currentlyConnected == maxClients) {
        sendString (tmp, "Limit of connections exceeded, try again later :(");
        shutdown (tmp, 2);
        close (tmp);
    } else {
        for (i = 0; i < maxClients; i++) {
            if (clients[i] == 0) {
                clients[i] = initClient (tmp);
                sendString (clients[i]->descr, "Choose a name");
                printf ("[%s] joined, choosing name at the moment\n", clients[i]->name);
                break;
            }
        }
        (*currentlyConnected)++;
    }
    return;
}


void processIncomingMsgs (int * currentlyConnected, fd_set * set, int maxClients, client * clients) {
    /*
        Cycle through all the clients; if one is in (&set)
    */
    word input, oldname;
    int max_d, received, i;

    for (i = 0; i < maxClients; i++) {
        //some of the clients sent some request
        if (clients[i] == 0) {
            continue;
        }

        if (FD_ISSET(clients[i]->descr, set)) {

            received = readString (clients[i]->descr, &input);
            if (received != 0 && clients[i]->nameSet == 0) {
                if (nameAvailabe (input, maxClients, clients)) {
                    oldname = clients[i]->name;
                    clients[i]->name = input;
                    clients[i]->nameSet = 1;
                    printf ("%s changed name to %s\n", oldname, input);
                    free (oldname);
                    sendString (clients[i]->descr, "Welcome to chat!!!");
                    broadcast (1, i, maxClients, "joined the chat", clients);
                } else {
                    sendString (clients[i]->descr, "Name unavailable, choose another: ");
                    free (input);
                }
            } else if (received == 0 || (strcmp (input, "bye!") == 0)) {
                if (clients[i]->nameSet != 0) {
                    broadcast (1, i, maxClients, "left the chat" , clients);
                } else {
                    printf ("[%s] left the chat", clients[i]->name);
                }
                shutdown (clients[i]->descr, 2);
                close (clients[i]->descr);
                freeClient (&clients[i]);
                (*currentlyConnected)--;
            } else {
                broadcast (0, i, maxClients, input, clients);
            }
        }
    }
    return;
}


int chatServer (int lstnr) {
    struct timeval * tv = (struct timeval *) malloc (sizeof (struct timeval));
    tv->tv_sec = 4;
    char* input;
    int currentlyConnected = 0;
    int maxClients = 3;
    int max_d, received, i;
    client * clients = (client *) calloc (maxClients, sizeof (client));
    fd_set set;

    while (1) {
        FD_ZERO (&set);
        FD_SET (lstnr, &set);
        max_d = lstnr;
        for (i = 0; i < maxClients; i++) {
            if (clients[i] > 0) {
                FD_SET (clients[i]->descr, &set);
                if (clients[i]->descr > max_d) {
                    max_d = clients[i]->descr;
                }
            }
        }

        int res = select(max_d+1, &set, NULL, NULL, tv);
        if(res < 1) {
            //fprintf(stderr, "Error: %s\n", strerror(errno));
            printf ("Server is running, %i clients connected...\n\n", currentlyConnected);
            tv->tv_sec = 4;
        }


        if (FD_ISSET(lstnr, &set)) {
            acceptNewClient (&currentlyConnected, lstnr, maxClients, clients);                             //new connection acception
        }

        processIncomingMsgs (&currentlyConnected, &set, maxClients, clients);

    }
    return 0;
}




int main (int argc, char *argv[]) {
    if (argc != 2) {                                // Check if required parameters passed
        fprintf(stderr, "%s", "\nUsage: program [port number >= 1024] \n\n");
        return -1;
    }
    int port = atoi (argv[1]);

    int listener = openListener (port);
    if (listener == -1) {
        return -1;
    }

    //Main magic starts here
    chatServer (listener);

    return 0;
}

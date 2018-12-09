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

typedef char * word;

struct wort {                                   //for storing string and link to next
    word wrd;
    struct wort * next;
};

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

int sendString (int sendTo, word str) {
    if (write (sendTo, str, (strlen (str) + 1)) == -1) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    return 0;
}

int chatClient (int sockfd) {
    word input;
    int received;
    fd_set set;

    while (1) {
        FD_ZERO (&set);
        FD_SET (0, &set);
        FD_SET (sockfd, &set);

        int res = select(sockfd + 1, &set, NULL, NULL, NULL);
        if(res < 1) {
            fprintf(stderr, "Error: %s\n", strerror(errno));
        }

        if (FD_ISSET (sockfd, &set)) {
            received = readString (sockfd, &input);

            if (received == 0) {
                    printf ("Connection closed\n");
                    return 0;
            } else if (received > 0) {
                printf ("%s\n\n", input);
            }
            free (input);
        }

        if (FD_ISSET (0, &set)) {
            received = readString (0, &input);
            input [strlen(input)] = '\0';
            //input = (word) realloc (input, (strlen(input) + 1) * sizeof (char));
            if (strlen (input) > 0) {
                if (sendString (sockfd, input) == EXIT_FAILURE) {
                    fprintf(stderr, "Error: %s, message not sent\n", strerror(errno));
                }
            }
            free (input);
            printf ("\n");
        }

    }


}

int main (int argc, char *argv[]) {

    if (argc != 3) {                                // Check if required parameters passed
        fprintf(stderr, "%s", "\nUsage: program [IP] [port number]\n\n");
        return EXIT_FAILURE;
    }

    int port = atoi (argv[2]);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(argv[1]);

    int sockfd = socket (AF_INET, SOCK_STREAM, 0);
    if (0 != connect(sockfd, (struct sockaddr *) &addr, sizeof(addr))) {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    //The connection is now established
    chatClient (sockfd);

    return 0;
}

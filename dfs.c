#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>

#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#define BUFFERSIZE 1024

#include "send_receive.h"

void sigchldHandler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
}


void setupSigchld(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchldHandler;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP; // https://www.gnu.org/software/libc/manual/html_node/Flags-for-Sigaction.html
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        fprintf(stderr, "failed to set SIGCHLD handler...\n");
        exit(-1);
    }
}


void createDirectoryIfNeeded(char* name) {
    struct stat st;
    if (stat(name, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return;
        } else {
            fprintf(stderr, "file exists with direcotry name %s, unable to make directory\n", name);
            exit(-1);
        }
    }

    if (mkdir(name, 0777) != 0) {
        fprintf(stderr, "unable to find or make directory %s\n", name);
        exit(-1);
    }
}


int main(int argc, char **argv) {
    int serverfd, clientfd;
    int portno; // port to listen on */
    int clientlen; // byte size of client's address
    struct sockaddr_in serveraddr; // server's addr
    struct sockaddr_in clientaddr; // client addr
    char buf[BUFFERSIZE]; // message buffer
    int optval; // flag value for setsockopt
    int n; // message byte size

    char directory[BUFFERSIZE];

    setupSigchld();

    // take in port num
    if (argc != 3) {
        fprintf(stderr, "usage: %s <directory> <port>\n", argv[0]);
        exit(1);
    }

    bzero(directory, BUFFERSIZE);
    strcpy(directory, argv[1]);
    portno = atoi(argv[2]);
    
    createDirectoryIfNeeded(directory);


    // setup socket
    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        fprintf(stderr, "Error opening socket!\n");
        exit(1);
    }

    // make it reusable
    optval = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

    // server internet address
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);


    // bind socket to port
    if(bind(serverfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        fprintf(stderr, "Unable to bind socket to port %d!\n", portno);
        exit(1);
    }


    if(listen(serverfd, 10) < 0) {
        fprintf(stderr, "Unable to listen on port %d!\n", portno);
    }


    // start listening!
    fprintf(stderr, "Server started! Awaiting requests...\n");
    clientlen = sizeof(clientaddr);
    while(1) {
        clientfd = accept(serverfd, (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen);
        if(clientfd < 0) {
            fprintf(stderr, "Failed to accept request\n");
            continue;
        }
        
        if(fork() != 0) {
            continue;
        }

        
        bzero(buf, BUFFERSIZE);
        n = recv(clientfd, buf, 1, 0);
        if(n < 0) {
            fprintf(stderr, "Failed to receive data!\n");
        }


        if(buf[0] == 'l') {
            sendList(directory, clientfd);
        } else if (buf[0] == 'p') {
            receiveFile(directory, clientfd);
        } else if (buf[0] == 'g') {
            uint32_t nameLength;
            char filename[BUFFERSIZE];
            if(recv(clientfd, filename, sizeof(nameLength), 0) < sizeof(nameLength)) {
                fprintf(stderr, "failed to get file name length!\n");
                break;
            }

            nameLength = getInt(filename);
            
            bzero(filename, BUFFERSIZE);
            if(recv(clientfd, filename, nameLength, 0) < nameLength) {
                fprintf(stderr, "failed to get file name!\n");
                break;
            }

            sendFile(directory, filename, clientfd);
        }

        close(clientfd);
        exit(0);
        break;
    }
}
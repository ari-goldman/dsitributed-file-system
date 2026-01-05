#ifndef SEND_RECEIVE_H
#define SEND_RECEIVE_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>


#define PAYLOAD_SIZE 1024


int receiveFile(char* directory, int clientfd);
int sendFile(char* directory, char* filename, int clientfd);
void sendList(char* directory, int clientfd);
int connectToServer(char* _ip_port);

uint32_t getInt(char* buf);
void setInt(uint32_t n, char* buf);



#endif
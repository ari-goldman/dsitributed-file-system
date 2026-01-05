#include "send_receive.h"


int receiveFile(char* directory, int clientfd) {
    char filename[PAYLOAD_SIZE * 2];
    char buf[PAYLOAD_SIZE];
    uint32_t receivedBytes = 0;
    uint32_t nameLength, fileLength;
    int n;
    FILE* fp;

    if((n = recv(clientfd, buf, sizeof(nameLength) * 2, 0)) < sizeof(nameLength) * 2) {
        fprintf(stderr, "failed getting file length!\n");
        return -1;
    }

    nameLength = getInt(buf);
    fileLength = getInt(buf + sizeof(nameLength));

    bzero(buf, PAYLOAD_SIZE);
    if((n = recv(clientfd, buf, nameLength, 0)) != nameLength) {
        fprintf(stderr, "failed getting file name!\n");
        return -1;
    }

    sprintf(filename, "%s/%s", directory, buf);

    
    fp = fopen(filename, "wb");
    if(!fp) {
        fprintf(stderr, "unable to open file (%s)\n", filename);
        return -1;
    }
    
    while(receivedBytes < fileLength) {
        bzero(buf, PAYLOAD_SIZE);
        n = recv(clientfd, buf, PAYLOAD_SIZE, 0);

        if(n < 0) {
            fprintf(stderr, "recv failed\n");
            fclose(fp);
            remove(filename);
            return -1;
        } else if(n == 0) {
            printf("client closed\n");
            fclose(fp);
            remove(filename);
            return -1;
        }

        receivedBytes += n;
        fwrite(buf, 1, n, fp);
        fflush(stderr);
    }

    fclose(fp);

    return 0;
}

int sendFile(char* directory, char* filename, int clientfd) {
    char buf[PAYLOAD_SIZE];
    char filepath[1024];
    uint32_t nameLength, fileLength;
    FILE* fp;
    int bytesRead, bytesSent;

    sprintf(filepath, "%s/%s", directory, filename);
    fp = fopen(filepath, "rb");
    if(fp == NULL) {
        fprintf(stderr, "failed to open file %s\n", filename);
        return -1;
    }

    fseek(fp, 0L, SEEK_END);
    fileLength = (uint32_t) ftell(fp);
    rewind(fp);
    
    nameLength = strlen(filename);
    
    // build info "packet"
    // nameSize, fileSize, name ...
    bzero(buf, PAYLOAD_SIZE);
    setInt(nameLength, buf);
    setInt(fileLength, buf + sizeof(nameLength));
    strcpy(buf + sizeof(nameLength) + sizeof(fileLength), filename);

    if(send(clientfd, buf, sizeof(nameLength) + sizeof(fileLength) + strlen(filename), 0) < sizeof(nameLength) + sizeof(fileLength) + strlen(filename)) {
        fprintf(stderr, "failed sending initial file info\n");
        fclose(fp);
        return -1;
    }

    // send file
    bzero(buf, PAYLOAD_SIZE);
    while((bytesRead = fread(buf, 1, PAYLOAD_SIZE, fp)) > 0) {
        if((bytesSent = send(clientfd, buf, bytesRead, 0)) < bytesRead) {
            fprintf(stderr, "failed to send part of file...\n");
            fclose(fp);
            return -1;
        }

        bzero(buf, PAYLOAD_SIZE);
    }

    fclose(fp);
    return 1;
}


void sendList(char* directory, int clientfd) {
    DIR *dp;
    struct dirent *entry;
    struct stat stats;

    dp = opendir(directory);
    if (dp == NULL) {
        fprintf(stderr, "no directory!\n");
    }

    char buf[256 + sizeof(uint32_t)];
    char filepath[512];
    while((entry = readdir(dp)) != NULL) {
        sprintf(filepath, "%s/%s", directory, entry->d_name);
        if(stat(filepath, &stats) < 0) continue;
        if(S_ISDIR(stats.st_mode)) continue;

        int nameLength = strlen(entry->d_name);
        if(nameLength >= 256) {
            fprintf(stderr, "file name too long (> 255?): %s\n", entry->d_name);
            continue;
        }

        setInt(nameLength, buf);
        memcpy(buf + sizeof(uint32_t), entry->d_name, nameLength);

        if(send(clientfd, buf, nameLength + sizeof(uint32_t), 0) < nameLength + sizeof(uint32_t)) {
            fprintf(stderr, "failed to send filename!\n");
            continue;
        };

    }

    setInt(0, buf); // probably just zeroes
    send(clientfd, buf, sizeof(uint32_t), 0);

    closedir(dp);

    return;
}


int connectToServer(char* _ip_port){

    char* ip_port = malloc(strlen(_ip_port) + 1);
    if(!ip_port) {
        fprintf(stderr, "failed to malloc!\n");
        return -1;
    }
    
    bzero(ip_port, strlen(_ip_port));

    strcpy(ip_port, _ip_port);

    char* colon = strchr(ip_port, ':');
    if(!colon) {
        fprintf(stderr, "invalid ip/port: %s\n", ip_port);
        free(ip_port);
        return -1;
    }

    int port = atoi(colon + 1);
    if(port <= 0) {
        fprintf(stderr, "invalid port from %s\n", ip_port);
        free(ip_port);
        return -1;
    }

    *colon = '\0';


    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error opening socket!\n");
        free(ip_port);
        return -1;
    }

    // make it reusable
    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int)); // add check?

    // set 1 second timeout
    struct timeval tv;
    tv.tv_sec  = 1;
    tv.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        fprintf(stderr, "failed to set socket timeout!");
        free(ip_port);
        return -1;
    }

    struct hostent* server = gethostbyname(ip_port);
    if (server == NULL) {
        fprintf(stderr,"unable to get host from %s\n", ip_port);
        free(ip_port);
        return -1;
    }

    struct sockaddr_in serveraddr;

    // build the server's Internet address
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr_list[0], 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(port);


    if(connect(sockfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0) {
        free(ip_port);
        close(sockfd);
        return -1;
    }

    free(ip_port);
    return sockfd;
}

// copy data from a buffer into integer for host use
uint32_t getInt(char* buf) {
    uint32_t length;
    memcpy(&length, buf, sizeof(uint32_t));
    length = ntohl(length);
    return length;
}

// copy integer data into buffer for network use
void setInt(uint32_t n, char* buf) {
    n = htonl(n);
    memcpy(buf, &n, sizeof(n));
}

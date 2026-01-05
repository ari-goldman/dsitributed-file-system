#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>
#include <netdb.h>
#include <openssl/md5.h>
#include <time.h>


#define BUFFERSIZE 1024

#include "send_receive.h"


FILE* openConfigFile() {
    FILE* fp;
    char filepath[512];
    char* home = getenv("HOME");
    if(!home) {
        fprintf(stderr, "FAILED TO GET $HOME\n");
        exit(-1);
    }

    sprintf(filepath, "%s/dfc.conf", home);
    fp = fopen(filepath, "r");
    if(!fp) {
        fprintf(stderr, "Unable to open config file!");
        exit(-1);
    }

    return fp;
}


int connectToNextServer(){
    static long pos = 0;
    FILE* fp = openConfigFile();
    
    if(fp == NULL) {
        printf("failed to open config file\n");
        return -1;
    }


    fseek(fp, pos, SEEK_SET);

    char buf[3 * 128];
    char name[128], ip[128];
    int sockfd;
    while(fgets(buf, 512, fp)) {
        if(sscanf(buf, "server %s %s", name, ip) != 2) continue;
        sockfd = connectToServer(ip);
        pos = ftell(fp);
        fclose(fp);
        return sockfd;
    }

    
    pos = 0;
    fclose(fp);
    return 0; // zero means no more to connect to
}


int connectToServerN(int n) {
    FILE* fp = openConfigFile();
    
    if(fp == NULL) {
        fprintf(stderr, "unable to get config file\n");
    }

    char buf[3 * 128];
    char name[128], ip[128];
    bzero(buf, 3 * 128); 
    bzero(name, 128); 
    bzero(ip, 128);
    while(fgets(buf, 512, fp)) {
        if(sscanf(buf, "server %s %s", name, ip) != 2) continue;
        if(n > 0) {
            n--;
            continue;
        }

        return connectToServer(ip);

        bzero(buf, 3 * 128); 
        bzero(name, 128); 
        bzero(ip, 128);
    }

    
    return 0; // zero means it's OFFLINE
}


int getOnlineServerCount() {
    int count = 0;
    int sockfd;

    while((sockfd = connectToNextServer())) {
        if(sockfd >= 0) {
            close(sockfd);
            count++;
        }
    }

    return count;
}


// 0 == offline
// 1 == online
int isServerOnline(int n) {
    int sockfd = connectToServerN(n);
    int out = sockfd >= 0;
    close(sockfd);
    return out;
}


int getServerCount() {
    FILE* fp = openConfigFile();
    
    if(fp == NULL) {
        fprintf(stderr, "unable to get config file\n");
    }


    char buf[3 * 128];
    char name[128], ip[128];
    int n = 0;
    while(fgets(buf, 512, fp)) {
        if(sscanf(buf, "server %s %s", name, ip) != 2) continue;
        n++;
    }
    return n;
}

#define LIST_ENTRY_LEN 256
#define FOR_LIST(l) for(int i = 0; l[i] != NULL; i++)
#define FREE_LIST(l) FOR_LIST(l) { free(l[i]); if(l[i+1] == NULL) free(l[i+1]); } free(l); l = NULL;
char** getRawFileList() {
    int sockfd;
    int length = 0;
    char buf[BUFFERSIZE];
    

    char** list = malloc(sizeof(char*));
    list[length] = NULL;

    while((sockfd = connectToNextServer())) {
        send(sockfd, "l", 1, 0);

        uint32_t nameLength;
        while(1) {
            bzero(buf, sizeof(uint32_t));
            if(recv(sockfd, buf, sizeof(uint32_t), 0) < sizeof(uint32_t)) {
                fprintf(stderr, "failed getting filename size!\n");
                break;
            }

            nameLength = getInt(buf);
            if(nameLength <= 0) break;

            bzero(buf, BUFFERSIZE);
            if(recv(sockfd, buf, nameLength, 0) < nameLength) {
                fprintf(stderr, "failed getting file name!\n");
                break;
            }

            list[length] = malloc(LIST_ENTRY_LEN);
            memcpy(list[length], buf, LIST_ENTRY_LEN);
            list[length][LIST_ENTRY_LEN - 1] = '\0';
            length++;

            list = realloc(list, (sizeof(char*) * (length + 1)));
            list[length] = NULL;
        }

        close(sockfd);
    }

    return list;
}


int listLength(char** list) {
    int l;
    for(l = 0; list[l] != NULL; l++) {}
    return l;
}


char** copyList(char** toCopy) {
    char** list;

    int length = listLength(toCopy);

    list = malloc((length + 1) * (sizeof(char*)));

    FOR_LIST(toCopy) {
        list[i] = malloc(LIST_ENTRY_LEN);
        memcpy(list[i], toCopy[i], LIST_ENTRY_LEN);
    }

    list[length] = NULL;

    return list;
}


void makeRandName32(char *res) {
    char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int charsLen = strlen(chars);

    struct timeval tv;
    gettimeofday(&tv, NULL);

    srand((unsigned int) tv.tv_usec ^ getpid() ^ (unsigned int) tv.tv_sec);
    for(int i = 0; i < 32; i++) {
        res[i] = chars[rand() % charsLen];
    }
    res[32] = '\0';
}


int hashFilename(char* filename) {
    char res[MD5_DIGEST_LENGTH];
    int hash = 0;

    MD5((const unsigned char *) filename, strlen(filename), (unsigned char *) res);


	for(int i = 0; i < MD5_DIGEST_LENGTH / 4; i++) {
        hash ^= (res[i] << 24) | (res[i+1] << 16) | (res[i+2] << 8) | res[i+3];
    }

    hash = abs(hash);

    return hash;
}


int chunkAndSend(char* filename, int chunks, char* clientUUID) {
    int fileSize, chunkSize, chunkSizeRemainder;
    FILE* fp;

    if(chunks != getOnlineServerCount()) {
        fprintf(stderr, "chunk and send failed, chunk count not equal to number of servers online");
        return -1;
    }

    fp = fopen(filename, "rb");
    if(!fp) {
        fprintf(stderr, "unable to open file %s!\n", filename);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    rewind(fp);

    chunkSize = fileSize / chunks;
    chunkSizeRemainder = fileSize % chunks;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t milliseconds = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    char newFilename[256];
    int server1, server2;
    int toReadTotal, bytesRead;
    int serverfd;
    FILE* newFile;
    char buf[BUFFERSIZE];
    int fileHash = hashFilename(filename);

    for(int i = 0; i < chunks; i++) {
        toReadTotal = chunkSize + (i < chunkSizeRemainder);

        bzero(newFilename, 256);
        sprintf(newFilename, "%llu.%s.%d.%d.%s", milliseconds, clientUUID, i+1, chunks, filename);
        newFilename[255] = '\0'; // redundant, but... just in case

        server1 = (fileHash + i) % chunks;
        server2 = (fileHash + i + 1) % chunks;
        
        newFile = fopen(newFilename, "wb");

        bzero(buf, BUFFERSIZE);
        while((bytesRead = fread(buf, 1, toReadTotal < BUFFERSIZE ? toReadTotal : BUFFERSIZE, fp)) > 0) {
            toReadTotal -= bytesRead;
            fwrite(buf, 1, bytesRead, newFile);
            bzero(buf, BUFFERSIZE);
        }

        fclose(newFile);

        int fail1 = 0, fail2 = 0;

        serverfd = connectToServerN(server1);
        fail1 = fail1 || 1 != send(serverfd, "p", 1, 0);
        fail1 = fail1 || sendFile(".", newFilename, serverfd);
        close(serverfd);

        serverfd = connectToServerN(server2);
        fail2 = fail2 || 1 != send(serverfd, "p", 1, 0);
        fail2 = fail2 || sendFile(".", newFilename, serverfd);
        close(serverfd);

        remove(newFilename);
        
        if(fail1 && fail2) return -1; // if a chunk didn't get sent anywhere
    }

    return 0;
}



// based off this example
// https://en.cppreference.com/w/c/algorithm/qsort
int compareChunkNamesByBaseFile(const void* a, const void* b) {
    char* str1 = *(char**)a;
    char* str2 = *(char**)b;

    if(str1 == NULL) return -1;
    if(str1 == NULL) return 1;
    
    char baseFilename1[256];
    char baseFilename2[256];
    
    bzero(baseFilename1, 256);
    bzero(baseFilename2, 256);
    
    sscanf(str1, "%*[^.].%*[^.].%*d.%*d.%s", baseFilename1);
    sscanf(str2, "%*[^.].%*[^.].%*d.%*d.%s", baseFilename2);
    
    int baseFilenameLength1 = strlen(baseFilename1);
    int baseFilenameLength2 = strlen(baseFilename2);
    
    for(int i = 0; i < baseFilenameLength1 && i < baseFilenameLength2; i++) {
        if(baseFilename1[i] < baseFilename2[i]) return -1;
        else if (baseFilename1[i] > baseFilename2[i])  return 1;
    }
    
    return 0;
}


int compareChunkNamesInOrder(const void* a, const void* b) {
    char* str1 = *(char**)a;
    char* str2 = *(char**)b;
    
    if(str1 == NULL) return -1;
    if(str1 == NULL) return 1;
    
    char* end1, * end2;
    uint64_t t1 = strtoull(str1, &end1, 10);
    uint64_t t2 = strtoull(str2, &end2, 10);
    
    // descending order by time
    if(t1 < t2) {
        return 1; // if older, push to END
    } else if (t1 > t2) {
        return -1;
    }

    end1++;
    end2++;
    for(int i = 0; i < 32; i++) {
        if(end1[i] < end2[i]) return -1;
        else if(end1[i] > end2[i]) return 1;
    }
    
    end1 += 33;
    end2 += 33;
    
    int chunkNum1 = atoi(end1);
    int chunkNum2 = atoi(end2);
    
    if(chunkNum1 < chunkNum2) return -1;
    else if(chunkNum1 > chunkNum2) return 1;
    
    // skip over total chunk amount
    end1 = strchr(end1, '.') + 1;
    end2 = strchr(end2, '.') + 1;
    
    end1 = strchr(end1, '.') + 1;
    end2 = strchr(end2, '.') + 1;

    for(int i = 0; end1[i] != '\0' && end2[i] != '\0'; i++) {
        if(end1[i] < end2[i]) return -1;
        else if(end1[i] > end2[i]) return 1;
    }
    
    return 0;
}


int isFileValid(char* filename, char** list) {
    int chunkNum, chunkCount;
    char latestHash[33], hash[33], baseFilename[256];
    
    
    int chunksFound = 0;
    int chunksNeeded = -1;

    FOR_LIST(list) {
        bzero(baseFilename, 256);
        sscanf(list[i], "%*[^.].%[^.].%d.%d.%s", hash, &chunkNum, &chunkCount, baseFilename);

        if(!strcmp(filename, baseFilename)) {
            if(chunksNeeded == -1) {
                strcpy(latestHash, hash);
                chunksNeeded = chunkCount;
            } else {
                if(strcmp(latestHash, hash)) return 0;
            }
            
            if(chunksFound == chunkNum) continue;
            
            if(chunksFound + 1 == chunkNum) chunksFound++;
            else return 0; // next chunk is not in order! incomplete
            
            if(chunksFound == chunkCount) return 1; // found all chunks
        } else if(chunksNeeded != -1) {
            return 0; // found chunks but lost ending chunks so it continues on
        } else {
            continue;
        }
    }

    return 0;
}


void listRemoteFiles() {
    char** listInOrder = getRawFileList();
    char** listByFile = copyList(listInOrder);
    
    qsort(listInOrder, listLength(listInOrder), sizeof(char*), compareChunkNamesInOrder);
    qsort(listByFile, listLength(listByFile), sizeof(char*), compareChunkNamesByBaseFile);
    
    char baseFilename[256], baseFilenameOld[256];
    bzero(baseFilename, 256);
    bzero(baseFilenameOld, 256);
    
    FOR_LIST(listByFile) {
        sscanf(listByFile[i], "%*[^.].%*[^.].%*d.%*d.%s", baseFilename);
        
        if(!strcmp(baseFilename, baseFilenameOld)) continue;
        memcpy(baseFilenameOld, baseFilename, 256);
        
        fprintf(stderr, "%s", baseFilename);
        
        if(isFileValid(baseFilename, listInOrder)) {
            fprintf(stderr, "\n");
        } else {
            fprintf(stderr, " [incomplete]\n");
        }
    }
    
    FREE_LIST(listInOrder)
    FREE_LIST(listByFile)
}


int receiveAndStitchChunks(char* filename) {
    char** list = getRawFileList();
    qsort(list, listLength(list), sizeof(char*), compareChunkNamesInOrder);

    if(!isFileValid(filename, list)) {
        FREE_LIST(list)
        return -1;
    }

    int fileHash = hashFilename(filename);

    int chunks = -1;
    char** fileStart;
    char baseFilename[256];
    
    FOR_LIST(list) {
        sscanf(list[i], "%*[^.].%*[^.].%*d.%d.%s", &chunks, baseFilename);
        if(!strcmp(filename, baseFilename)) {
            fileStart = list + i;
            break;
        }
        chunks = -1;
    }

    if(chunks == -1) {
        FREE_LIST(list)
        return -1;
    }

    int* filenameIndices = malloc(sizeof(int) * chunks);
    for(int i = 0; i < chunks; i++) filenameIndices[i] = -1;

    int chunk, chunksReceived = 0;
    int server1, server2;
    int sockfd;
    char buf[BUFFERSIZE];
    FOR_LIST(fileStart) {
        sscanf(fileStart[i], "%*[^.].%*[^.].%d.%*d.%s", &chunk, baseFilename);

        if(strcmp(filename, baseFilename)) break;
        if(chunksReceived + 1 != chunk) continue;

        server1 = (fileHash + chunksReceived) % chunks;
        server2 = (fileHash + chunksReceived + 1) % chunks;
        
        if(isServerOnline(server1)) {
            sockfd = connectToServerN(server1);
            
            send(sockfd, "g", 1, 0);

            bzero(buf, BUFFERSIZE);
            uint32_t nameLength = strlen(fileStart[i]);
            setInt(nameLength, buf);
            strcpy(buf + sizeof(uint32_t), fileStart[i]);
            
            send(sockfd, buf, sizeof(uint32_t) + nameLength, 0);
            
            if(receiveFile(".", sockfd) == 0) {
                chunksReceived++;
            }
            
            close(sockfd);
        }
        
        if(chunksReceived != chunk && isServerOnline(server2)) {
            sockfd = connectToServerN(server2);
            
            send(sockfd, "g", 1, 0);
            
            bzero(buf, BUFFERSIZE);
            uint32_t nameLength = strlen(fileStart[i]);
            setInt(nameLength, buf);
            strcpy(buf + sizeof(uint32_t), fileStart[i]);
            
            send(sockfd, buf, sizeof(uint32_t) + nameLength, 0);
            
            if(receiveFile(".", sockfd) == 0) {
                chunksReceived++;
            }
            
            close(sockfd);
        }
        
        if(chunksReceived != chunk) {            
            for(int i = 0; i < chunks && filenameIndices[i] != -1; i++) {
                remove(fileStart[filenameIndices[i]]);
            }
            
            free(filenameIndices);
            FREE_LIST(list)
            return -1;
        }
        
        filenameIndices[chunksReceived-1] = i;
    }


    // combine them
    FILE* newFile = fopen(filename, "wb");
    FILE* chunkFile;
    int bytesRead;
    for(int i = 0; i < chunksReceived && filenameIndices[i] != -1; i++) {
        chunkFile = fopen(fileStart[filenameIndices[i]], "rb"); // add check
        bzero(buf, BUFFERSIZE);
        while((bytesRead = fread(buf, 1, BUFFERSIZE, chunkFile)) > 0) {
            fwrite(buf, 1, bytesRead, newFile);
        }
        fclose(chunkFile);
        remove(fileStart[filenameIndices[i]]);
    }
    
    fclose(newFile);
    free(filenameIndices);
    FREE_LIST(list)
    return 0;
}


int main(int argc, char **argv) {
    char UUID[33];
    
    // take in port num
    if (argc < 2) {
        fprintf(stderr, "usage: %s <command> [filenames...]\n", argv[0]);
        exit(1);
    }

    makeRandName32(UUID);

    if(strcmp(argv[1], "list") == 0) {
        listRemoteFiles();
        return 0;
    }

    for(int i = 2; i < argc; i++) {
        if(strcmp(argv[1], "get") == 0) {
            if(receiveAndStitchChunks(argv[i])) fprintf(stderr, "%s is incomplete\n", argv[i]);
            else fprintf(stderr, "succesffully got %s!\n", argv[i]);
        }
    
        else if(strcmp(argv[1], "put") == 0) {
            if(chunkAndSend(argv[i], getServerCount(), UUID)) fprintf(stderr, "%s put failed\n", argv[i]);
            else fprintf(stderr, "succesffully put %s!\n", argv[i]);
        }

        else {
            fprintf(stderr, "usage: %s [get|put|list] [filenames...]\n", argv[0]);
            exit(1);
        }
    }
}
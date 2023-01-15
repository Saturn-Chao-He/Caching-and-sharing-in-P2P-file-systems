//******************************************************************************
// Project: Caching and sharing in P2P systems
// Programmer: Chao He
// g++ server.cpp -o server -lpthread
// fog server 0 1 and its standby server 2 3
//******************************************************************************
#include <stdio.h>
#include <stdlib.h> // exit
#include <pthread.h>
#include <cstring>
#include <string.h>
#include <time.h>
#include <iostream>
#include <sys/socket.h> //for socket(), connect()
#include <unistd.h>     //for close()
#include <netinet/in.h> //for internet address family
#include <arpa/inet.h>  //for sockaddr_in, inet_addr()
#include <errno.h>      //for system error numbers
#include <netdb.h>
#include <list>
#include <unordered_map>
#include <string>
#include <cstdlib>
//#include <signal.h>
//#include <sys/types.h> //for signal

using namespace std;

#define MESSAGESIZE 80
#define MAX_BUF 10240 //10M
#define N 1024        //1M

char const *ipAddress[] = {"127.0.0.1", "127.0.0.1"};

char const *FogServerPath[] = {"/Users/hechao/Desktop/Coding/FogServer01/",
                               "/Users/hechao/Desktop/Coding/FogServer234/",
                               "/Users/hechao/Desktop/Coding/Standby01/",
                               "/Users/hechao/Desktop/Coding/Standby234/"};
struct CacheNode
{
    string key;   // file name
    string value; // path
    int freq;
    // pointer to the node in the list
    list<string>::const_iterator it;
};

class FogServerLFUCache
{
private:
    int capacity_;
    int min_freq_;

    // for file operation delete(rename) or copy(fopen)
    string strFogServerPath, FogServerPathFN;
           
    const char *cStrFogServerPathFN;

    // key -> CacheNode
    unordered_map<string, CacheNode> n_;
    // freq -> keys with freq
    unordered_map<int, list<string> > l_;

    void touch(CacheNode &node)
    {
        // Step 1: update the frequency
        const int prev_freq = node.freq;
        const int freq = ++(node.freq);

        // Step 2: remove the entry from old freq list
        l_[prev_freq].erase(node.it);

        if (l_[prev_freq].empty() && prev_freq == min_freq_)
        {
            // Delete the list
            l_.erase(prev_freq);

            // Increase the min freq
            ++min_freq_;
        }

        // Step 4: insert the key into the front of the new freq list
        l_[freq].push_front(node.key);

        // Step 5: update the pointer
        node.it = l_[freq].cbegin();
    }

public:
    FogServerLFUCache(int capacity) : capacity_(capacity), min_freq_(0) {}

    string get(string key)
    {
        auto it = n_.find(key);
        if (it == n_.cend())
            return "no result!";
        touch(it->second);
        return it->second.value;
    }

    void put(string key, int peer)
    {
        // send the shared file to fog server
        strFogServerPath = FogServerPath[peer];
        FogServerPathFN = strFogServerPath + key; 
        cStrFogServerPathFN = FogServerPathFN.c_str();

        if (capacity_ == 0)
            return;

        auto it = n_.find(key);

        // if the key already exists, update the value and touch it
        if (it != n_.cend())
        {
            it->second.value = FogServerPathFN;
            touch(it->second);
            return;
        }
        // if the cache is full
        if (n_.size() == capacity_)
        {
            // No capacity, need to remove one entry that
            // has the lowest freq
            // least recently used if there are multiple ones

            // Step 1: remove the element from min_freq_ list
            const string key_to_evict = l_[min_freq_].back();
            const string FogServer01PathKey_to_evict = strFogServerPath + l_[min_freq_].back();
            l_[min_freq_].pop_back();

            // Step 2: remove the key from the cache
            n_.erase(key_to_evict);
            remove(FogServer01PathKey_to_evict.c_str());
            cout << "Full! Remove the file : " << key_to_evict << endl;
        }

        // We know new item has freq of 1, thus set min_freq to 1
        const int freq = 1;
        min_freq_ = freq;

        // Add the key to the freq list
        l_[freq].push_front(key);

        // Create a new node in fog server
        n_[key] = {key, FogServerPathFN, freq, l_[freq].cbegin()};

        cout << "\nserver cache: " << n_.size() << endl;      
    }
};// =====end Class Fog Server=====

FogServerLFUCache fogServer(3);
int cnt = 0;
int heartbeatFlag = 1;

//This thread for sending heartbeat
void *SendHeartbeat(void *number)
{
    int peer = atoi((char *)number);
    int sockfd, retcode, nread;
    char const *serv_host_addr = "127.0.0.1";
    struct sockaddr_in server_addr;
    int server_port_id;

    if(peer == 0)// fog01 7010
        server_port_id = 7012; 
    if(peer == 1)// fog234 7011
        server_port_id = 7013;

    // Socket Initialization:
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("heartbeat: socket failed!\n");
        exit(1);
    }

    // initialize server's address and port info
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port_id);
    server_addr.sin_addr.s_addr = inet_addr(serv_host_addr);

    while (1)
    {
        retcode = sendto(sockfd, "alive", 5+1, 0,
                                 (struct sockaddr *)&server_addr, sizeof(server_addr));
        //if (retcode > 0)
            //cout << "\nsending heartbeat " << endl;
        //sleep(1);
    }
    close(sockfd);
    return 0;
}

//This thread for sending heartbeat
void *RecvHeartbeat(void *number)
{
    int sockfd;
    int bindCode;
    struct sockaddr_in my_addr, client_addr;
    int nread, retcode, addrlen;
    int peer = atoi((char *)number);
    int my_port_id;
    char recvBuf[MESSAGESIZE];

    if(peer == 2)
        my_port_id = 7012;
    if(peer == 3)
        my_port_id = 7013;

    // Initialization:
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("heartbeat: socket failed!\n");
        exit(1);
    }

    // set non-blocking recvfrom
    struct timeval timeout;
    timeout.tv_sec = 1;//秒
    timeout.tv_usec = 0;//微秒
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) 
        cout << "receive heartbeat thread: setsockopt failed\n";

    //cout << "Server: binding my local socket...\n";
    memset(&my_addr, 0, sizeof(my_addr));        // Zero out structure
    my_addr.sin_family = AF_INET;                // Internet address family
    my_addr.sin_port = htons(my_port_id);        //My port
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface

    bindCode = ::bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr));
    // binding:
    if (bindCode < 0)
    {
        printf("heartbeat: bind failed!\n");
        exit(2);
    }

    while (1)
    {
        addrlen = sizeof(client_addr);
        nread = recvfrom(sockfd, recvBuf, MESSAGESIZE, 0,
                          (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
        if(nread > 0 && strncmp(recvBuf, "alive", 5) == 0)
        {
            heartbeatFlag = 1;
            //cout << "\nrecv heartbeat\n";
        }   
        else
        {
            heartbeatFlag = 0;
            cout << "standby: fog crash!!!\n"; 
        }               
    }
    close(sockfd);
    return 0;
}

// request thread
void *request(void *number)
{
    string fileName;
    string msg;
    const char *resultMsg;
    string fileAddr;
    char recvFNBuf[MESSAGESIZE]; // for searching file name
    char recvBuf[MESSAGESIZE]; // for downloading
    int sockfd;
    int bindCode;
    struct sockaddr_in my_addr, client_addr;
    int nread, retcode, addrlen;
    int peer = atoi((char *)number);
    int my_port_id;

    if(peer == 0)
        my_port_id = 8000 + cnt;
    else if(peer == 1)
        my_port_id = 9000 + cnt;
    else if(peer == 2)
        my_port_id = 10000 + cnt;
    else
        my_port_id = 11000 + cnt;

    // Initialization:
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("Server: socket failed!\n");
        exit(1);
    }

    // set non-blocking recvfrom
    struct timeval timeout;
    timeout.tv_sec = 1;//秒
    timeout.tv_usec = 0;//微秒
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) 
        cout << "request thread: setsockopt failed\n";

    //cout << "Server: binding my local socket...\n";
    memset(&my_addr, 0, sizeof(my_addr));        // Zero out structure
    my_addr.sin_family = AF_INET;                // Internet address family
    my_addr.sin_port = htons(my_port_id);        //My port
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface

    bindCode = ::bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr));
    // binding:
    if (bindCode < 0)
    {
        printf("Server: bind failed!\n");
        exit(2);
    }

        addrlen = sizeof(client_addr);
        nread = recvfrom(sockfd, recvFNBuf, N, 0,
                         (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
        cout << "request length: " << nread << endl;
        
        // receive a file name
        if(nread > 0)
        {
            cout << "server: Receive search request.\n";
            cout << "recvBuf: " << recvFNBuf << endl;
            fileName = recvFNBuf;
            // search the file in hash table
            fileAddr = fogServer.get(fileName);

            // the server did not find the file, need to receive popular file
            if(fileAddr == "no result!") 
            {
                cout << "server: did not find the file, send noResult\n";
                sendto(sockfd,"noResult", 9, 0,
                                   (struct sockaddr *)&client_addr, sizeof(client_addr));
                
                // server need to download the popular file from peer
                nread = recvfrom(sockfd, recvFNBuf, N, 0,
                         (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);

                if(nread > 0 && strncmp(recvFNBuf, "popular", 7) == 0) 
                { 
                    cout << "server: Received popular request.\n";
                    string strFogServerPath, strFogServerPathFN;
                    const char *cStrFogServerPathFN;

                    // download the file
                    strFogServerPath = FogServerPath[peer];
                    strFogServerPathFN = strFogServerPath + fileName;
                    cStrFogServerPathFN = strFogServerPathFN.c_str();
                    cout << "Download: " << cStrFogServerPathFN << endl;

                    fogServer.put(fileName, peer);
                    FILE *fp = fopen(cStrFogServerPathFN, "w");

                    addrlen = sizeof(client_addr);
                    int allByte, count;
                    int rByte = recvfrom(sockfd, recvBuf, N, 0,
                                    (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
                    allByte += rByte;

                    if (rByte > 0)
                    {  
                        fwrite(recvBuf, 1, rByte, fp);
                        fclose(fp);
                        count++;
                        cout << "Download: allByte = " << allByte << endl;
                    }
                    else
                        fclose(fp);               
                }                 
            }

            // if server found the file, send the file, 
            // Fog first. If fog is dead, standby sends
            if( fileAddr != "no result!" &&
              ( (peer == 0 && heartbeatFlag == 1) || (peer == 1 && heartbeatFlag == 1) ||
                (peer == 2 && heartbeatFlag == 0) || (peer == 3 && heartbeatFlag == 0) )  ) 
            {
                sendto(sockfd,"found", 5, 0,
                                   (struct sockaddr *)&client_addr, sizeof(client_addr));
                cout << "server: found the file Addr:  " << fileAddr << endl;
            
                FILE *fps = fopen(fileAddr.c_str(), "r");
                if (!fps)
                    cout << "Open file error!\n";

                fseek(fps, 0, SEEK_END);

                // get the file size
                int fileSize = ftell(fps);
                rewind(fps);

                char resultBuf[N];
                int sByte;
                
                int numReads = fread(resultBuf, 1, fileSize, fps);
                cout << "numReads = " << numReads << "   fileSize: " << fileSize << endl;
                sByte = sendto(sockfd, resultBuf, numReads, 0,
                                   (struct sockaddr *)&client_addr, sizeof(client_addr));
                
                if (sByte == -1)
                        cout << "sendto() failed!\n";
                
                cout << "Send: allByte = " << sByte << endl;
                fclose(fps);
            } 
        }

    if ( pthread_detach(pthread_self()) == 0 )
        cout << "request processing is done, thread detached\n";
    close(sockfd);
    return 0;
}

int main(int argc, char *argv[])
{
    cout << "\n************************************************************\n";
    pthread_t request_thread, SendHeartbeat_thread, RecvHeartbeat_thread;
    
    string fileName;
    char recvMsg[MESSAGESIZE]; // for msg
    int sockfd;
    int bindCode;
    struct sockaddr_in my_addr, client_addr;
    int nread, retcode, addrlen;
    int peer = atoi(argv[1]);
    int my_port_id;
    int newThreadPort;
    char cStrNewThreadPort[6];

    if(peer == 0)
        my_port_id = 7000; // fogServer01
    else if (peer == 1)    
        my_port_id = 7001; // forServer234
    else if (peer == 2)
        my_port_id = 7002; // standby server for 01
    else
        my_port_id = 7003; // standby server for 234

    if(peer == 0 || peer == 1)
        pthread_create(&SendHeartbeat_thread, NULL, SendHeartbeat, (void *)argv[1]);

    if(peer == 2 || peer == 3)
        pthread_create(&RecvHeartbeat_thread, NULL, RecvHeartbeat, (void *)argv[1]);

    // Initialization:
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("Server: socket failed!\n");
        exit(1);
    }

    //cout << "Server: binding my local socket...\n";
    memset(&my_addr, 0, sizeof(my_addr));        // Zero out structure
    my_addr.sin_family = AF_INET;                // Internet address family
    my_addr.sin_port = htons(my_port_id);        //My port
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface

    bindCode = ::bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr));
    // binding:
    if (bindCode < 0)
    {
        printf("Server: bind failed!\n");
        exit(2);
    }

    while (1)
    {
        addrlen = sizeof(client_addr);
        nread = recvfrom(sockfd, recvMsg, MESSAGESIZE, 0,
                            (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
        //cout << "Receive a request for a port, nread = " << nread << endl;

        // Once receive a request, creat a new thread to process that request
        if(nread > 0 && strncmp(recvMsg, "port", 4) == 0)
        {
            cnt++;
            // creat a thread, and send the port of the thread to the peer
            if(pthread_create(&request_thread, NULL, request, (void *)argv[1]) == 0)
                cout << "request thread created\n";

            if(peer == 0)
                newThreadPort = 8000 + cnt;
            else if(peer == 1)
                newThreadPort = 9000 + cnt;
            else if(peer == 2)
                newThreadPort = 10000 + cnt;
            else    
                newThreadPort = 11000 + cnt;
            
            retcode = sendto(sockfd, (to_string(newThreadPort)).c_str(), 7, 0,
                                 (struct sockaddr *)&client_addr, sizeof(client_addr));
            if (retcode > 0)
                cout << "send the new thread's port to the peer, port: " 
                     << to_string(newThreadPort) << endl;        
        }
    }

    //cout << "Client thread created\n";

    // wait for them to finish
    if(peer == 0 || peer == 1)
        pthread_join(SendHeartbeat_thread, NULL);
    if(peer == 2 || peer == 3)
        pthread_join(RecvHeartbeat_thread, NULL);
    cout << "\n*************************************************************\n";
    return 0;
}
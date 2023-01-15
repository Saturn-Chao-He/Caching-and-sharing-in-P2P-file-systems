//******************************************************************************
// Project: Caching and sharing in P2P systems
// Programmer: Chao He
// g++ p2p.cpp -o p2p -lpthread
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
//#include <signal.h>
//#include <sys/types.h> //for signal

using namespace std;

#define SERVER_PORT_ID 5000 
#define MESSAGESIZE 80
#define MAX_BUF 10240 //10M
#define N 1024        //1M
//#define MAX_BUF 655360 //640M
//#define N 61440        //60M

char const *ipAddress[] = {
    "127.0.0.1", "127.0.0.1",
    "127.0.0.1", "127.0.0.1",
    "127.0.0.1"};

char const *port[] = {
    "8000", "8001",
    "8002", "8003",
    "8004"};

char const *serverPort[] = {"7000", "7001"};

char const *NVRAM_CachePath[] = {
    "/Users/hechao/Desktop/Coding/peer0/NVRAM_Cache/",
    "/Users/hechao/Desktop/Coding/peer1/NVRAM_Cache/",
    "/Users/hechao/Desktop/Coding/peer2/NVRAM_Cache/",
    "/Users/hechao/Desktop/Coding/peer3/NVRAM_Cache/",
    "/Users/hechao/Desktop/Coding/peer4/NVRAM_Cache/"};

char const *Shared_HDPath[] = {
    "/Users/hechao/Desktop/Coding/peer0/Shared_HD/",
    "/Users/hechao/Desktop/Coding/peer1/Shared_HD/",
    "/Users/hechao/Desktop/Coding/peer2/Shared_HD/",
    "/Users/hechao/Desktop/Coding/peer3/Shared_HD/",
    "/Users/hechao/Desktop/Coding/peer4/Shared_HD/"};

char const *UserPath[] = {
    "/Users/hechao/Desktop/Coding/peer0/User/",
    "/Users/hechao/Desktop/Coding/peer1/User/",
    "/Users/hechao/Desktop/Coding/peer2/User/",
    "/Users/hechao/Desktop/Coding/peer3/User/",
    "/Users/hechao/Desktop/Coding/peer4/User/"};

char const *DownloadsPath[] = {
    "/Users/hechao/Desktop/Coding/peer0/User/Downloads/",
    "/Users/hechao/Desktop/Coding/peer1/User/Downloads/",
    "/Users/hechao/Desktop/Coding/peer2/User/Downloads/",
    "/Users/hechao/Desktop/Coding/peer3/User/Downloads/",
    "/Users/hechao/Desktop/Coding/peer4/User/Downloads/"};

struct CacheNode
{
    string key;   // file name
    string value; // path
    int freq;
    // pointer to the node in the list
    list<string>::const_iterator it;
};

class LFUCache
{
private:
    int capacity_;
    int min_freq_;

    // for file operation delete(rename) or copy(fopen)
    string strUserPath, strShared_HDPath, strNVRAM_CachePath;
    string UserPathFN, Shared_HDPathFN, NVRAM_CachePathFN;
    const char *cStrUserPathFN, *cStrShared_HDPathFN, *cStrNVRAM_CachePathFN;

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
    LFUCache(int capacity) : capacity_(capacity), min_freq_(0) {}

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
        // move the shared file to local Shared_HD
        strNVRAM_CachePath = NVRAM_CachePath[peer];
        strShared_HDPath = Shared_HDPath[peer];
        strUserPath = UserPath[peer];

        NVRAM_CachePathFN = strNVRAM_CachePath + key;
        Shared_HDPathFN = strShared_HDPath + key;
        UserPathFN = strUserPath + key;

        cStrNVRAM_CachePathFN = NVRAM_CachePathFN.c_str();
        cStrShared_HDPathFN = Shared_HDPathFN.c_str();
        cStrUserPathFN = UserPathFN.c_str();

        char *readBuf;

        if (capacity_ == 0)
            return;

        auto it = n_.find(key);

        // if the key already exists, update the value and touch it
        if (it != n_.cend())
        {
            it->second.value = NVRAM_CachePathFN;
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
            const string NVRAM_CachePathKey_to_evict = strNVRAM_CachePath + l_[min_freq_].back();
            l_[min_freq_].pop_back();

            // Step 2: remove the key from the cache
            n_.erase(key_to_evict);
            remove(NVRAM_CachePathKey_to_evict.c_str());
            cout << "Full! Remove the file : " << key_to_evict << endl;
        }

        // We know new item has freq of 1, thus set min_freq to 1
        const int freq = 1;
        min_freq_ = freq;

        // Add the key to the freq list
        l_[freq].push_front(key);

        // Create a new node in cache
        n_[key] = {key, NVRAM_CachePathFN, freq, l_[freq].cbegin()};

        cout << "\nNVRAM cache: " << n_.size() << endl;

        // cooresponding file from Shared_HD to NVRAM_Cache
        FILE *fp = fopen(cStrUserPathFN, "r");
        fseek(fp, 0, SEEK_END);

        // get the file size
        int fileSize = ftell(fp);
        rewind(fp);
        fread(readBuf, 1, fileSize, fp);
        
        if (!fp)
        {
            printf("No such a file!\n");
        }
        else
        {
            // copy the file from Shared_HD to NVRAM_Cache
            fp = fopen(cStrNVRAM_CachePathFN, "w");
            fwrite(readBuf, 1, fileSize, fp);
            printf("move to NVRAM_Cache\n");
            fclose(fp);
            // copy the file from User to Shared_HD
            fp = fopen(cStrShared_HDPathFN, "w");
            fwrite(readBuf, 1, fileSize, fp);
            printf("Successfully share the file\n");
            fclose(fp);      
        }
    }
};// ===Class LFUCache====

LFUCache cache(3);
int printOperationFlag = 1;
//int switchToStandbyFlag = 0;
int fogFlag = 1;
int standbyFlag = 1;
int mainServer = 0;

//client thread
void *client(void *number)
{
    cout << endl;

    int input = 0;
    int rByte = 0;
    int peer = atoi((char *)number);
    char recvBuf[MESSAGESIZE];
    char threadBuf[MESSAGESIZE];
    FILE *fpc;

    string fileName, msg, searchResult;
    string strDownloadsPath = DownloadsPath[peer];
    string strDownloadsPathFN;

    const char *cStrDownloadsPathFN, *cStrMsg;

    //for socket
    int sockfd, retcode, nread, addrlen, newThreadPort;
    char const *serv_host_addr = "127.0.0.1";
    struct sockaddr_in server_addr;
    int server_port_id;

    serv_host_addr = ipAddress[peer];

    if (peer < 4)
        server_port_id = SERVER_PORT_ID + peer + 1; // neighbor's port
    else
        server_port_id = SERVER_PORT_ID;
   
    // Socket Initialization:
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("Client: socket failed!\n");
        exit(1);
    }

    // set non-blocking recvfrom, timeout needs to be higher than network latency
    struct timeval timeout;
    timeout.tv_sec = 1;//秒
    timeout.tv_usec = 0;//微秒
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) 
        cout << "client: setsockopt failed\n";

    // initialize server's address and port info
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port_id);
    server_addr.sin_addr.s_addr = inet_addr(serv_host_addr);

    while (printOperationFlag == 1)
    {
        printOperationFlag = 0;
        cout << "\n<<<<<<<<<<<<<<<<<<<< Operation List >>>>>>>>>>>>>>>>>>>>\n";
        cout << " 1. Search the file on other peers\n";
        cout << " 2. Share my file\n";
        cout << " 3. Cancel sharing my file\n";
        cout << "Operation > ";

        cin >> input;
        if (input != 1 && input != 2 && input != 3)
        {
            cout << "Input error! Please type 1 or 2 or 3 > ";
            cin >> input;
            //exit(0);
        }

        switch (input)
        {        
          case 1:{ //Search the file on other peers
            cout << "\nSearch > ";
            cin >> fileName;
            msg = fileName;
            cStrMsg = msg.c_str();
            cout << endl << "\nSearch the file: " << cStrMsg << endl;

            //step1:
            // ***************************************************************************
            // 1. send a port request to the fog server 7000 7001
            //    the fog server creates a thread to process the request
            // ***************************************************************************
            if(peer == 0 || peer == 1)                
                server_addr.sin_port = htons(7000);                                                             
            if(peer == 2 || peer == 3 || peer == 4)
                server_addr.sin_port = htons(7001);

            retcode = sendto(sockfd, "port", 5, 0,
                                 (struct sockaddr *)&server_addr, sizeof(server_addr));
            if (retcode > 0)
                cout << "send the port request to fog " << endl;

            nread = recvfrom(sockfd, threadBuf, MESSAGESIZE, 0,
                                 (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
            // if fog has no response                
            if(nread <= 0)
            {
                fogFlag = 0;
                //goto step2;
            }  
             else
                fogFlag = 1; 
            // receive a new thread port
            newThreadPort = atoi(threadBuf);
            server_addr.sin_port = htons(newThreadPort);
            // search for the file in new thread of the fog server
            retcode = sendto(sockfd, cStrMsg, strlen(cStrMsg) + 1, 0,
                                 (struct sockaddr *)&server_addr, sizeof(server_addr));
            if (retcode > 0)
                cout << "send the search request to fog " << endl;

            nread = recvfrom(sockfd, recvBuf, N, 0,
                                 (struct sockaddr *)&server_addr, (socklen_t *)&addrlen); 
            if(nread <= 0)
                fogFlag = 0;
            else
                fogFlag = 1; 

            // if the fog found the file
            if(nread > 0 && strncmp(recvBuf, "found", 5) == 0)
            {
                cout << "fog found the file \n";
                // download the file
                strDownloadsPathFN = strDownloadsPath + fileName;
                cStrDownloadsPathFN = strDownloadsPathFN.c_str();
                cout << "Download from fog: " << cStrDownloadsPathFN << endl;
                fopen(cStrDownloadsPathFN, "w");

                rByte = recvfrom(sockfd, recvBuf, N, 0,
                                     (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
                if (rByte >= 0)
                {
                    fwrite(recvBuf, 1, rByte, fpc);
                    fclose(fpc);
                    cout << "Download from fog: allByte = " << rByte << endl;
                    fogFlag = 1;
                }
                else
                {
                    fogFlag = 0;
                    fclose(fpc);
                }             
            }
            
            // if the fog server did not find the file, multicast to all peers
            if((nread>0 && strncmp(recvBuf, "noResult", 8)==0))
            {    
                cout << "fog did not find the file \n";
                for (int i = 0; i < 4; i++)
                {
                    server_addr.sin_port = htons(5000 + (server_port_id + i) % 5);
                    retcode = sendto(sockfd, cStrMsg, strlen(cStrMsg) + 1, 0,
                                     (struct sockaddr *)&server_addr, sizeof(server_addr));
                    if (retcode > 0)
                    cout << "Multicasted the search request to all peers\n";
                }
                // download the file
                strDownloadsPathFN = strDownloadsPath + fileName;
                cStrDownloadsPathFN = strDownloadsPathFN.c_str();
                cout << "Download from other peer: " << cStrDownloadsPathFN << endl;
                fopen(cStrDownloadsPathFN, "w");
                rByte = recvfrom(sockfd, recvBuf, N, 0,
                                 (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
                if (rByte >= 0)
                {
                    fwrite(recvBuf, 1, rByte, fpc);
                    fclose(fpc);
                    cout << "Download from other peer: allByte = " << rByte << endl;
                    fogFlag = 1;
                }
                else
                {
                    fogFlag = 0;
                    fclose(fpc);
                }
                // send the new thread num to the peer, let it send the popular file
                // to fog and standby servers
                retcode = sendto(sockfd, threadBuf, strlen(threadBuf) + 1, 0,
                                     (struct sockaddr *)&server_addr, sizeof(server_addr));
                if(retcode >0)
                    cout << "send the thread num to the peer for sending popular\n";               
            }

         //step2:
            // ***************************************************************************
            // 2. do the same thing to standby server 7002 7003
            // ***************************************************************************
            if(peer == 0 || peer == 1)                
                server_addr.sin_port = htons(7002);                                                             
            if(peer == 2 || peer == 3 || peer == 4)
                server_addr.sin_port = htons(7003);

            retcode = sendto(sockfd, "port", 5, 0,
                                 (struct sockaddr *)&server_addr, sizeof(server_addr));
            if (retcode > 0)
                cout << "send the port request to standby" << endl;

            nread = recvfrom(sockfd, recvBuf, N, 0,
                                 (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
            // if fog has no response                
            if(nread <= 0)
            {
                standbyFlag = 0;
                //goto step3;
            }
                
            else
                standbyFlag = 1; 
            // receive a new thread port
            newThreadPort = atoi(recvBuf);
            server_addr.sin_port = htons(newThreadPort);
            // search for the file in new thread of the fog server
            retcode = sendto(sockfd, cStrMsg, strlen(cStrMsg) + 1, 0,
                                 (struct sockaddr *)&server_addr, sizeof(server_addr));
            if(retcode > 0)
                cout << "send the search request to standby " << endl;

             nread = recvfrom(sockfd, recvBuf, N, 0,
                                 (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
            if(nread <= 0)
                standbyFlag = 0;
            else
                standbyFlag = 1;          
                 
            // if fogFlag == 0, indicating the fog server crashed, 
            // so need to download from standby server
            if( (nread > 0 && strncmp(recvBuf, "found", 5) == 0) && fogFlag == 0 )
            {
                cout << "standby found the file \n";
                // download the file
                strDownloadsPathFN = strDownloadsPath + fileName;
                cStrDownloadsPathFN = strDownloadsPathFN.c_str();
                cout << "Download from standby: " << cStrDownloadsPathFN << endl;
                fopen(cStrDownloadsPathFN, "w");

                rByte = recvfrom(sockfd, recvBuf, N, 0,
                                     (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
                if (rByte >= 0)
                {
                    fwrite(recvBuf, 1, rByte, fpc);
                    fclose(fpc);
                    cout << "Download from standby: allByte = " << rByte << endl;
                }
                else
                {
                    standbyFlag = 0;
                    fclose(fpc);
                }            
            }

            // if the standby server did not find the file, download from the peer
            if(nread>0 && strncmp(recvBuf, "noResult", 8)==0)
            {    
                cout << "standby did not find the file \n";
                for (int i = 0; i < 4; i++)
                {
                    server_addr.sin_port = htons(5000 + (server_port_id + i) % 5);
                    retcode = sendto(sockfd, cStrMsg, strlen(cStrMsg) + 1, 0,
                                     (struct sockaddr *)&server_addr, sizeof(server_addr));
                    if (retcode > 0)
                    cout << "Multicasted the search request to all peers\n";
                }
                // download the file
                strDownloadsPathFN = strDownloadsPath + fileName;
                cStrDownloadsPathFN = strDownloadsPathFN.c_str();
                cout << "Download from other peer: " << cStrDownloadsPathFN << endl;
                fopen(cStrDownloadsPathFN, "w");
                rByte = recvfrom(sockfd, recvBuf, N, 0,
                                 (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
                if (rByte >= 0)
                {
                    fwrite(recvBuf, 1, rByte, fpc);
                    fclose(fpc);
                    cout << "Download from other peer: allByte = " << rByte << endl;
                    standbyFlag = 1;
                }
                else
                {
                    standbyFlag = 0;
                    fclose(fpc);
                }                 
            }
           
            // send the new thread num to the peer, let it send the popular file
            // to fog and standby servers
            retcode = sendto(sockfd, threadBuf, strlen(threadBuf) + 1, 0,
                                     (struct sockaddr *)&server_addr, sizeof(server_addr));
            if(retcode >0)
                    cout << "send the thread num to the peer for sending popular \n ";


            //step3:
            // ***************************************************************************
            // 3. if both fog and standby server are dead, multicast to all peers
            // ***************************************************************************
            if(fogFlag == 0 && standbyFlag == 0)
            {
                cout << "both fog and standby server are dead\n";
                for (int i = 0; i < 4; i++)
                {
                    server_addr.sin_port = htons(5000 + (server_port_id + i) % 5);
                    retcode = sendto(sockfd, cStrMsg, strlen(cStrMsg) + 1, 0,
                                     (struct sockaddr *)&server_addr, sizeof(server_addr));
                    if (retcode > 0)
                    cout << "Multicasted the search request to all peers\n";
                }
                // download the file
                strDownloadsPathFN = strDownloadsPath + fileName;
                cStrDownloadsPathFN = strDownloadsPathFN.c_str();
                cout << "Download from other peer: " << cStrDownloadsPathFN << endl;
                fopen(cStrDownloadsPathFN, "w");
                rByte = recvfrom(sockfd, recvBuf, N, 0,
                                 (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
                if (rByte >= 0)
                {
                    fwrite(recvBuf, 1, rByte, fpc);
                    fclose(fpc);
                    cout << "Download from other peer: allByte = " << rByte << endl;
                    fogFlag = 1;
                }
                else
                {
                    fogFlag = 0;
                    fclose(fpc);
                }            
            }

            break; }//===== end case 1 =====
            
          case 2: //Share my file
            {  
                cout << "\nShare > ";
                cin >> fileName;
                cache.put(fileName, peer);
                break;
            }

        }// ===== end switch =====
        printOperationFlag = 1;

    } // ===== end while() =====

    close(sockfd);
    return 0;
}

// server thread
void *server(void *number)
{
    string fileName;
    string msg;
    const char *resultMsg;
    string fileAddr;
    char recvFNBuf[MESSAGESIZE];
    char threadBuf[MESSAGESIZE];
    int sockfd;
    int bindCode;
    struct sockaddr_in my_addr, client_addr;
    char const *serv_host_addr = "127.0.0.1";
    int nread, retcode, addrlen;
    int peer = atoi((char *)number);
    int my_port_id = SERVER_PORT_ID + peer; //my own port
    int newThreadPort;

    // Initialization:
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("peer: socket failed!\n");
        exit(1);
    }

    // set non-blocking recvfrom
    struct timeval timeout;
    timeout.tv_sec = 1;//秒
    timeout.tv_usec = 0;//微秒
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) 
        cout << "server: setsockopt failed\n";

    //cout << "Server: binding my local socket...\n";
    memset(&my_addr, 0, sizeof(my_addr));        // Zero out structure
    my_addr.sin_family = AF_INET;                // Internet address family
    my_addr.sin_port = htons(my_port_id);        //My port
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface

    bindCode = ::bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr));
    // binding:
    if (bindCode < 0)
    {
        printf("peer: bind failed!\n");
        exit(2);
    }

    while (1)
    {
        addrlen = sizeof(client_addr);
        nread = recvfrom(sockfd, recvFNBuf, N, 0,
                         (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
        //cout << "\npeer: receive a request, length: " << nread << endl;
        
        if(nread > 0)
        {
            cout << "\nreceive a request.\n";
            cout << "recvBuf: " << recvFNBuf << endl;
            fileName = recvFNBuf;

            // search the file, if find it, send it to the peer who searched
            fileAddr = cache.get(fileName);
            if(fileAddr != "no result!")
            {
                cout << "The address: " << fileAddr << endl;      
                FILE *fps = fopen(fileAddr.c_str(), "r");
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
                        cout << "sentto() failed!\n";
                
                cout << "Send file to peer: allByte = " << sByte << endl;

                // send the popular file to the fog server
                nread = recvfrom(sockfd, threadBuf, MESSAGESIZE, 0,
                         (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
                // tell the peer who owns the file sends to particular request thread
                newThreadPort = atoi(threadBuf);
                              
                if(peer == 0 || peer == 1)
                {
                    if(fogFlag == 1)
                    {
                        // send to fog
                        my_addr.sin_port = htons(newThreadPort);  
                        my_addr.sin_addr.s_addr = inet_addr(serv_host_addr);
                        sendto(sockfd, "popular", 7, 0,
                                        (struct sockaddr *)&my_addr, sizeof(my_addr));
                                   
                        sByte = sendto(sockfd, resultBuf, numReads, 0,
                                        (struct sockaddr *)&my_addr, sizeof(my_addr));
                        if (sByte == -1)
                            cout << "sentto() failed!\n";
                
                        cout << "Send file to fog01: allByte = " << sByte << endl;

                        
                        // send to standby
                        my_addr.sin_port = htons(newThreadPort+2000);   
                        my_addr.sin_addr.s_addr = inet_addr(serv_host_addr);
                        sendto(sockfd, "popular", 7, 0,
                                        (struct sockaddr *)&my_addr, sizeof(my_addr));
                                   
                        sByte = sendto(sockfd, resultBuf, numReads, 0,
                                        (struct sockaddr *)&my_addr, sizeof(my_addr));
                        if (sByte == -1)
                            cout << "sentto() failed!\n";
                
                        cout << "Send file to standby01: allByte = " << sByte << endl;
                        
                    }
                    
                    else
                    {
                        my_addr.sin_port = htons(newThreadPort);  
                        my_addr.sin_addr.s_addr = inet_addr(serv_host_addr);
                        sendto(sockfd, "popular", 7, 0,
                                   (struct sockaddr *)&my_addr, sizeof(my_addr));
                                   
                        sByte = sendto(sockfd, resultBuf, numReads, 0,
                                   (struct sockaddr *)&my_addr, sizeof(my_addr));
                        if (sByte == -1)
                            cout << "sentto() failed!\n";
                
                        cout << "Send file to standby01: allByte = " << sByte << endl;
                    }
                                     
                }

                if(peer == 2 || peer == 3 || peer == 4)
                {
                    // send to fog
                    my_addr.sin_port = htons(newThreadPort);     
                    my_addr.sin_addr.s_addr = inet_addr(serv_host_addr);
                    sendto(sockfd, "popular", 7, 0,
                                   (struct sockaddr *)&my_addr, sizeof(my_addr));
                    sByte = sendto(sockfd, resultBuf, numReads, 0,
                                   (struct sockaddr *)&my_addr, sizeof(my_addr));
                    if (sByte == -1)
                        cout << "sentto() failed!\n";
                
                    cout << "Send to fog234: allByte = " << sByte << endl;

                    // send to standby
                    my_addr.sin_port = htons(newThreadPort+2000);     
                    my_addr.sin_addr.s_addr = inet_addr(serv_host_addr);                    
                    sendto(sockfd, "popular", 7, 0,
                                   (struct sockaddr *)&my_addr, sizeof(my_addr));
                    sByte = sendto(sockfd, resultBuf, numReads, 0,
                                   (struct sockaddr *)&my_addr, sizeof(my_addr));
                    if (sByte == -1)
                        cout << "sentto() failed!\n";
                
                    cout << "Send to standby234: allByte = " << sByte << endl;
                }
                fclose(fps);

            }// ===== end if(fileAddr != "no result!")=====            
        }// ===== end if(nread > 0)=====
    }//=====end while(1)=====

    //sleep(3);
    close(sockfd);
    return 0;
}

int main(int argc, char *argv[])
{
        cout << "\n************************************************************\n";
        pthread_t client_thread, server_thread;

        pthread_create(&client_thread, NULL, client, (void *)argv[1]);
        //cout << "Client thread created\n";
        pthread_create(&server_thread, NULL, server, (void *)argv[1]);
        //cout << "Server thread created\n";

        // wait for them to finish
        pthread_join(client_thread, NULL);
        pthread_join(server_thread, NULL);

        cout << "\n*************************************************************\n";
        return 0;
}

// note: sendto(C-string, string length +1)
// note recvfrom(recvBuf, must be buffer size, not c-string length)
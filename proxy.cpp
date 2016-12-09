#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <memory.h>
#include <errno.h>
#include <math.h>
#include <queue>
#include <pthread.h>
#include <dirent.h>

//C++
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
using namespace std;

typedef struct Ele{
    int client_fd;
    string client_msg;
}Ele;

const int MAX_CHARS_PER_LINE = 512;
const int MAX_TOKENS_PER_LINE = 20;
const char* const DELIMITER = " ";
pthread_t senderThreads[10];
string homeDir;
bool caughtSigInt;
int sock_fd;
int port;
FILE* logStream;
std::string logName;
struct sockaddr_in servSock, client;
#define NUMTAGS 6
std::string tags[NUMTAGS];// = {"id","pw","flag", "file","segment", "msg"}

pthread_mutex_t q_lock;
pthread_mutex_t client_sock_lock;
pthread_mutex_t dir_lock;
queue<Ele*> q;
int threadsActive;
vector<string> indexes;

/* Notes: TCP sockets differ from UDP in that they need a call to listen() and they use recv(), not recvfrom().
    Why are sockaddr_in structs created like that then cast to sockaddr structs?
*/

typedef struct Entry{
    string data;
    int len;
}Entry;

typedef struct Field{
  std::string name;
  std::string value;
}Field;

std::vector<Field> fields;

int parse_request(string msg){
  std::istringstream msgStream;
  std::string line;

  //Ignore first line, it's already been checked by check_line_one()
  getline(msgStream, line);

  while(getline(msgStream, line) != NULL) {
      std::cout<<"\tLine is "<<line<<std::endl;
      Field f;
      char* pch;
      int idx = 0;
      char* lineArr = strdup(line.c_str());
      pch = strtok (lineArr,":");
      do {
        std::string tokStr = pch;
        if(!idx) {
          f.name = tokStr;
        } else {
          f.value = tokStr;
          fields.push_back(f);
        }
        idx++;
      }while ((pch = strtok(NULL, " :"))!= NULL);
    }
  return 0;
}

string pack_header(int errCode, bool invalidMethod, bool invalidVersion) {
    string all;
    string resp_human;
    switch(*errCode) {
        case 200:
            resp_human = "OK";
            break;
        case 400:
            resp_human = "Bad Request";
            break;
        case 404:
            resp_human = "Not Found";
            break;
        case 500:
            resp_human = "Internal Server Error: cannot allocate memory";
            break;
        case 501:
            resp_human = "Not Implemented";
            break;
        default:
            cout<<"Error assigning human-readable error code in pack_header"<<endl;
            resp_human = "undefined error";
    }
    all = version + " " + errCode + " " + resp_human + "\r\n";
    return all;
}

int check_line_one(const char* client_req, string* uri, int* errCode, bool* invalidMethod, bool*invalidVersion) {
    char* charURI = new char[200];
    char* charMeth = new char[200];
    char* charVer = new char[200];
    sscanf(client_req, "%s %s %s", charMeth, charURI, charVer);
    string strMeth(charMeth);
    string strUri(charURI);
    string strVer(charVer);
    if(strMeth != "GET" && (strMeth == "POST" || strMeth == "DELETE" || strMeth == "HEAD" || strMeth == "PUT" || strMeth == "OPTIONS")) {
        printf("Unimplemented HTTP method\n");
        *errCode = 501;
        return -1;
    } else if(strMeth != "GET") {
        printf("Completely invalid HTTP method\n");
        *invalidMethod = true;
        *errCode = 400;
        return -1;
    } else if(strUri.find(' ') != string::npos && strUri[0] != '/'){ //TODO: is the check for / really necessary?
        cout<<"Invalid URI, uri = \""<<strUri<<"\""<<endl;
        *errCode = 404;
        return -1;
    } else if(strVer.find("HTTP/") == string::npos) {
        printf("Invalid HTTP version\n");
        *invalidVersion = true;
        *errCode = 400;
        return -1;
    } else if(strVer != "HTTP/1.0" && strVer != "HTTP/1.1") {
        cout<<"Incorrect version (not implemented)"<<endl;
        *errCode = 501;
        return -1;
    }
    *uri = strUri;
    return 0;
}


void respond(int client_fd, std::string msg) {
  //pthread_mutex_lock(&client_sock_lock);
  if((send(client_fd, msg.c_str(), msg.length(), 0)) < 0) {
    //pthread_mutex_unlock(&client_sock_lock);
    logStream = fopen(logName.c_str(), "a");
    fprintf(logStream, "respond() failed!\n");
    fclose(logStream);
    perror("");
  } else {
    logStream = fopen(logName.c_str(), "a");
    fprintf(logStream, "respond() succeeded\n");
    fclose(logStream);
    //pthread_mutex_unlock(&client_sock_lock);
  }
}


void handle_msg(int client_fd, std::string full_msg){
    string uri;
    string response;
    int errCode = 200;
    bool invalidMethod;
    bool invalidVersion;
    std::istringstream msgStream(full_msg);
    std::string line;
    getline(msgStream, line);
    if(check_line_one(line.c_str(), &uri, &errCode, &invalidMethod, &invalidVersion) == -1) {
        cout<<"ERROR: request line \n\t"<<line.c_str()<<"\nis invalid. Error code "<<errCode<<endl;
        pack_header(errCode, invalidMethod, invalidVersion);
    } else {
        cout<<"Valid request line!"<<endl;
        parse_request(full_msg);
        for(int i = 0; i < fields.size(); i++){
            cout<<"Name: "<<fields[i].name<<" Value: "<<fields[i].value<<endl;
        }
        //check if URI exists
        //forward message
    }
}

void catch_sigint(int s){
    cout<<"caught signal "<<s<<", exiting"<<endl;
    caughtSigInt = true;
    for(int i=0; i<10; i++)  {
        pthread_join(senderThreads[i], NULL);
        threadsActive--;
    }
    pthread_mutex_destroy(&q_lock);
    pthread_mutex_destroy(&client_sock_lock);
    pthread_mutex_destroy(&dir_lock);
    close(sock_fd);
    while(!q.empty()){
        cout<<"Stuff left in queue?"<<endl;
        Ele* ele = q.front();
        q.pop();
        delete(ele);
    }
    cout<<"done exiting"<<endl;
    //exit(0);
}

void catch_sigpipe(int s) {
  cout<<"Caught SIGPIPE"<<endl;
  sleep(10);
  exit(1);
}

void *crawlQueue(void *payload){
    //pop Ele from queue if queue isn't empty
    while(!caughtSigInt) {
        pthread_mutex_lock(&q_lock);
        int success = 0;
        Ele* ele = new Ele;
        if(!q.empty()) {
            success = 1;
            ele = q.front();
            q.pop();
        }
        pthread_mutex_unlock(&q_lock);
        if(success) {
            printf("Received message %s\n", ele->client_msg.c_str());
            handle_msg(ele->client_fd, ele->client_msg);
        } else{
            //if queue was empty wait and check again
            int sleep_time = rand()%101;
            usleep(sleep_time);
        }
        delete(ele);
    }
}

void init(){
    caughtSigInt = false;
    threadsActive = 0;
    tags[0] = "id";
    tags[1] = "pw";
    tags[2] = "flag";
    tags[3] = "file";
    tags[4] = "segment";
    tags[5] = "msg";
}

int main(int argc, char* argv[]) {
    struct sockaddr_in client;
    struct sigaction sigIntHandler, sigPipeHandler;
    int client_fd, read_size;
    socklen_t sockaddr_len;
    char client_req[2000];
    std::string locLogName = "log.txt";
    //std::string dir(argv[2]);
    std::string confName = "dfs.conf";
    init();

    //parse configuration
    //parseConfig(strdup(confName.c_str()));

    //Set up signal handler for SIGINT and SIGPIPE
    sigIntHandler.sa_handler = catch_sigint;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);

    sigPipeHandler.sa_handler = catch_sigpipe;
    sigemptyset(&sigPipeHandler.sa_mask);
    sigPipeHandler.sa_flags = 0;
    sigaction(SIGPIPE, &sigPipeHandler, NULL);

    //Initialize mutexes
    if(pthread_mutex_init(&q_lock, NULL) != 0) {
        fprintf(stderr, "ERROR: Mutex initialization failed on q_lock. \n");
        exit(1);
    }
    if(pthread_mutex_init(&client_sock_lock, NULL) != 0) {
        fprintf(stderr, "ERROR: Mutex initialization failed on client_sock_lock. \n");
        exit(1);
    }
    if(pthread_mutex_init(&dir_lock, NULL) != 0) {
        fprintf(stderr, "ERROR: Mutex initialization failed on dir_lock. \n");
        exit(1);
    }

    //Initialize socket
    //homeDir.assign(argv[2]);
    if((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return 1;
    }
    //Allows immediate reuse of socket: credit to stack overflow
    int yes = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
      perror("Setsocketopt");
      exit(1);
    }

    servSock.sin_family = AF_INET;
    servSock.sin_port= htons(atoi(argv[1]));
    servSock.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr_len = sizeof(servSock);

    if(bind(sock_fd,(struct sockaddr *)&servSock , sizeof(servSock)) < 0) {
        perror("Bind error");
        return 1;
    }

    if(listen(sock_fd, 10) < 0) {
      perror("Listen error");
        return 1;
    }

    //Initialize thread pool
    for(int i = 0; i < 10; i++) {
        int retVal = pthread_create(&senderThreads[i], NULL, crawlQueue, NULL);
        if(retVal) {
          logStream = fopen(logName.c_str(), "a");
          fprintf(logStream, "pthread_create error: %s\n", strerror(errno));
          fclose(logStream);
          exit(1);
        } else {
          threadsActive++;
        }
    }

    while(!caughtSigInt && threadsActive > 0) {
        //pthread_mutex_lock(&client_sock_lock);
        if((client_fd = accept(sock_fd, (struct sockaddr *)&client, &sockaddr_len)) < 0) {
            logStream = fopen(logName.c_str(), "a");
            fprintf(logStream, "Accept error: %s\n", strerror(errno));
            fclose(logStream);
            while(threadsActive > 0);
            //cout<<"threadsActive after accept error: "<<threadsActive<<endl;
            return 1;
        }
        //pthread_mutex_unlock(&client_sock_lock);
        while((read_size = recv(client_fd , client_req , 2000 , 0)) > 0 ) {
            Ele* ele = new Ele;
            string msg(client_req);
            ele->client_fd = client_fd;
            ele->client_msg = msg;
            pthread_mutex_lock(&q_lock);
            q.push(ele);
            pthread_mutex_unlock(&q_lock);
            bzero(client_req, 2000);
        }

        if(read_size < 0) {
            logStream = fopen(logName.c_str(), "a");
            fprintf(logStream, "Recv error: %s\n", strerror(errno));
            fclose(logStream);
            while(threadsActive > 0);
            //cout<<"threadsActive after accept error: "<<threadsActive<<endl;
            return 1;
        }
    }
    cout<<"Caught sig int in main"<<endl;
    return 0;
}

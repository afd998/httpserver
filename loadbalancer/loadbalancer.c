#include<err.h>
#include<limits.h>
#include <sys/time.h>
#include <errno.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#define BUFFER_SIZE 4096

struct hw2server {
    uint16_t  name;
    uint8_t  booted;
    uint64_t requests;
    uint64_t errors;
};

struct ReqNode {
    uint32_t fd;
    struct ReqNode * next;

};

struct ReqNode * head = NULL;
struct ReqNode * tail = NULL;
uint32_t num_requests = 0;
pthread_mutex_t health_mut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t prio_mut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ll_mut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t counting_mut = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t cv;
pthread_cond_t health_cv;
atomic_int req_count =0;

uint16_t num_hw2s=0;
uint16_t R=5;
uint16_t N=4;
struct hw2server ** priority_array;
uint16_t * hw2servers_h;
/*
 * client_connect takes a port number and establishes a connection as a client.
 * connectport: port number of server to connect to
 * returns: valid socket if successful, -1 otherwise
 */
int client_connect(uint16_t connectport) {
    int connfd;
    struct sockaddr_in servaddr;

    connfd=socket(AF_INET,SOCK_STREAM,0);
    if (connfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);

    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(connectport);

    /* For this assignment the IP address can be fixed */
    inet_pton(AF_INET,"127.0.0.1",&(servaddr.sin_addr));

    if(connect(connfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0){
      //printf("error connecting to hw2 server\n");
      return -1;
    }
    return connfd;
}

/*
 * server_listen takes a port number and creates a socket to listen on 
 * that port.
 * port: the port number to receive connections
 * returns: valid socket if successful, -1 otherwise
 */
int server_listen(int port) {
    int listenfd;
    int enable = 1;
    struct sockaddr_in servaddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
        return -1;
    if (bind(listenfd, (struct sockaddr*) &servaddr, sizeof servaddr) < 0)
        return -1;
    if (listen(listenfd, 500) < 0)
        return -1;
    return listenfd;
}

/*
 * bridge_connections send up to 100 bytes from fromfd to tofd
 * fromfd, tofd: valid sockets
 * returns: number of bytes sent, 0 if connection closed, -1 on error
 */
int bridge_connections(int fromfd, int tofd) {
    char recvline[4096];
    int n = recv(fromfd, recvline,4096, 0);
    if (n < 0) {
        perror("connection error receiving");
        //printf("connection error receiving\n");
        return -1;
    } else if (n == 0) {
        //printf("receiving connection ended\n");
        close(tofd);
        return n;
    }
    recvline[n] = '\0';
    n = send(tofd, recvline, n, 0);
    if (n < 0) {
        printf("connection error sending\n");
        return -1;
    } else if (n == 0) {
        printf("sending connection ended\n");
        close(fromfd);
        return n;
    }
    return n;
}
void boot(uint16_t hw2server){
    //printf("inboot\n");
    pthread_mutex_lock(&prio_mut);
    for(int i =0; i < num_hw2s; i++){
        if( (*(priority_array +i))->name==hw2server){ //not so sure about this one lol
            (*(priority_array +i))->booted=1; 
            break;
        }else{
        continue; 
        }   
    }
    pthread_mutex_unlock(&prio_mut);
}
void send_500_to_user(int userfd){
  char res[] ="HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
  send(userfd, res, strlen(res),0);
  return;
}
//test
/*
 * bridge_loop forwards all messages between both sockets until the connection
 * is interrupted. It also prints a message if both channels are idle.
 * sockfd1, sockfd2: valid sockets
 */
void printprio(){
    printf("========\n");
    for(int i =0; i < num_hw2s; i++){
        printf("________\n");
        printf("name     :%d\n", (*(priority_array +i))->name);
        printf("requests :%ld\n", (*(priority_array +i))->requests);
        printf("errors   :%ld\n", (*(priority_array +i))->errors);
        printf("booted   :%d\n", (*(priority_array +i))->booted);
        printf("________\n");

    }
    printf("========\n");
}
int get_hw2server(){
    int something=0;
    int min_pos =0;
    uint64_t min_req=9999999999999999;
    for(int i =0; i < num_hw2s; i++){
        if((*(priority_array +i))->booted==0){
            uint64_t reqs =(*(priority_array +i))->requests;
            if(reqs==0){
                min_pos=i;
                something=1;
                break;
            }
            if(reqs<min_req){
                min_pos=i;
                min_req=reqs;
                something =1;
            }else if(reqs==min_req){
                if(((*(priority_array +i))->errors) < ((*(priority_array +min_pos))->errors)){
                    min_pos=i;
                    min_req=reqs;
                    something =1;
                }
            }
        }
    }

    //printf("%ld\n", min_req);
    if (something==0){
        //pthread_mutex_unlock(&prio_mut);
        return 0;
    }else{
        pthread_mutex_lock(&prio_mut);
        (*(priority_array +min_pos))->requests++;
        pthread_mutex_unlock(&prio_mut);
    }
    return (*(priority_array +min_pos))->name;
}

void process_request(int userfd){
    printprio();
    uint16_t hw2server=get_hw2server();
    if(hw2server==0){
        //all are down
        send_500_to_user(userfd);
        return;
    }
    int hw2fd=client_connect(hw2server);
    if(hw2fd == -1){
        send_500_to_user(userfd);
        return;
    }
    fd_set set;
    struct timeval timeout;
    int fromfd, tofd;
    while(1) {
        // set for select usage must be initialized before each select call
        // set manages which file descriptors are being watched
        FD_ZERO (&set);
        FD_SET (userfd, &set);
        FD_SET (hw2fd, &set);

        // same for timeout
        // max time waiting, 5 seconds, 0 microseconds
        timeout.tv_sec = 2.5;
        timeout.tv_usec = 0;

        // select return the number of file descriptors ready for reading in set
        switch (select(FD_SETSIZE, &set, NULL, NULL, &timeout)) {
            case -1:
                printf("error during select, exiting\n");
                return;
            case 0:
                printf("timeout...\n");
                send_500_to_user(userfd);
                boot(hw2server);
                close(userfd);
                close(hw2fd);
                //handle this broken hw2server
                return;
            default:
                if (FD_ISSET(userfd, &set)) {
                    //printf("from user\n");
                    fromfd = userfd;
                    tofd = hw2fd;
                } else if (FD_ISSET(hw2fd, &set)) {
                    //printf("from hw2\n");
                    fromfd = hw2fd;
                    tofd = userfd;
                } else {
                    printf("this should be unreachable\n");
                    return;
                }
        }
        int ret = bridge_connections(fromfd, tofd);
        if(ret == -1){
            printf("some sort of error after bridge connections\n");
            send_500_to_user(userfd);
            break;
        }
        if(ret == 0){
            break;
        }
    }
  
    return;
}

uint64_t take_job(){
    if(num_requests ==0){
        printf("i locked when there were requests so there should still be requests...something went wrong");
        return 0;
    }
    int fd = head->fd;
    struct ReqNode * oldhead = head;
    head = head->next;
    free(oldhead);
    num_requests--;
    pthread_mutex_unlock(&ll_mut);
    return fd;
}

void add_job(int fd){
    struct ReqNode *newjob;
    newjob = (struct ReqNode *)malloc(sizeof(struct ReqNode));
    newjob->fd=fd;
    newjob->next = NULL;
    pthread_mutex_lock(&ll_mut);
    if(num_requests ==0){
        head=newjob;
        tail=newjob;
    }else{
        tail->next=newjob;
        tail=newjob;
    }
    num_requests++;
    pthread_mutex_unlock(&ll_mut);
    pthread_cond_signal(&cv);
}

void * worker_main(void * data){
    (void) data;
    while(1){
        pthread_mutex_lock(&ll_mut);
        if(num_requests>0){
            uint64_t client_sockd = take_job();
            if (client_sockd ==0){
                continue;    
            }else{
                process_request(client_sockd);
            }
        }else{
            //printf("waiting\n");
            pthread_cond_wait(&cv, &ll_mut);
            uint64_t client_sockd = take_job();
            if (client_sockd ==0){
                continue;

            }else{
                process_request(client_sockd);
            }
        }
    }
    return NULL;
}



void update_priority(int hw2server,  uint64_t  requests,  uint64_t  errors){
    pthread_mutex_lock(&prio_mut);
    for(int i =0; i < num_hw2s; i++){
        if( (*(priority_array +i))->name==hw2server){ //not so sure about this one lol
            (*(priority_array +i))->requests=requests;
            (*(priority_array +i))->errors=errors;
            (*(priority_array +i))->booted=0;
            break;
        }else{
        continue; 
        }   
    }
    pthread_mutex_unlock(&prio_mut);
}

int64_t find_content_length(char * buff){
    int cl=-1;
    int * content_length = &cl;
    char * rest = NULL;
    char buff_copy[strlen(buff)];
    strcpy(buff_copy, buff);
    char* curr_line=strtok_r(buff_copy, "\r\n", &rest);

    while(1){ 
        if(curr_line == NULL){break;}
        int matched = sscanf(curr_line, "Content-Length: %d", content_length);
        if(matched == 1){
            break;
        }
        curr_line=strtok_r(NULL, "\r\n", &rest);
    }
    return *content_length;
}

int8_t find_res_code(char * buff){
    char * rest = NULL;
    char buff_copy[strlen(buff)];
    strcpy(buff_copy, buff);
    char* tok=strtok_r(buff_copy, " ", &rest);
    tok=strtok_r(NULL, " ", &rest);  
    if(tok == NULL){
    return -1;
    }
    if(strcmp(tok,"200")==0){
        //printf("200\n");
       return 0; 
    }
    return -1;
}

int handle_healthcheck_res(int hw2fd, uint16_t hw2server){

    int32_t left_to_read = BUFFER_SIZE;
    char buff[BUFFER_SIZE+1];
    buff[BUFFER_SIZE]='\0';
    char * end_of_buff= buff;
    while(1){
        ssize_t bytes_read = read(hw2fd, end_of_buff, left_to_read);
        left_to_read = left_to_read-bytes_read;
        if(bytes_read==-1){
            return -1;
        }
        if(bytes_read==0){
            return -1;
        }
        if(strstr(buff, "\r\n\r\n")==NULL){
            if(left_to_read == 0){
                return -1;
            }else{
                end_of_buff = end_of_buff + sizeof(char)*bytes_read;
            }
        }else{
            break;
        }   
    }
    if(find_res_code(buff) == -1){
       // printf("response to healthcheck wasnt a 200\n");
        return -1;
    }
    int content_length =find_content_length(buff);
    if(content_length ==-1){
        printf("couldnt find the content length in the response to a health check\n");
        return -1;
    }
        

    //read the actual content of the health check.
    char* first_DCRNL=strstr(buff, "\r\n\r\n");
    char * payload_pos =NULL;
    int content_left = content_length;
    char content_buff[BUFFER_SIZE];

    if(strlen(first_DCRNL)!=4){
        //payload is in this read.
        payload_pos= first_DCRNL + 4*sizeof(char);
        int payload_first_buff= strlen(payload_pos);
        content_left -= payload_first_buff;
        strcpy(content_buff,payload_pos); //is this copying the null terminator?
        payload_pos = content_buff+payload_first_buff*sizeof(char);
    }

    while(content_left>0){
        ssize_t bytes_read = read(hw2fd, payload_pos, content_left);
        content_left = content_left-bytes_read;
        if(bytes_read==-1){
            return 0;
        }
        if(bytes_read==0){
            printf("lost the hw2 servre during healthcheck\n");
            return -1;
        }
        payload_pos += bytes_read*sizeof(char);
    }
    close(hw2fd);
    //printf("%s", buff);
    char * rest = NULL;
    char* tok=strtok_r(buff, "\n", &rest);
    tok =strtok_r(NULL, "\n", &rest);
    tok=strtok_r(NULL, "\n", &rest);
    tok=strtok_r(NULL, "\n", &rest); 

    long int errors= strtol(tok, NULL, 10);
    if ((errno == ERANGE && (errors == LONG_MAX || errors == LONG_MIN)) || (errno != 0 && errors == 0)) {
        printf("strtol\n");
        return -1;
    }
    tok=strtok_r(NULL, "\n", &rest);  
    long int entries= strtol(tok, NULL, 10);
    if ((errno == ERANGE && (entries == LONG_MAX || entries == LONG_MIN)) || (errno != 0 && entries == 0)) {
        printf("strtol\n");
        return -1;
    }
    //printf("%d Requests: %ld errors %ld\n", hw2server, entries, errors);

    update_priority(hw2server, (uint64_t) entries,  (uint64_t) errors);
    return 0;
}


void * healthcheck(void * data){
    uint16_t hw2server = *((uint16_t*)data);

    int hw2fd= client_connect(hw2server);
    if(hw2fd==-1){
        boot(hw2server);
        pthread_exit(NULL);
    }
    struct timeval timeout;
    char msg[] = "GET /healthcheck HTTP/1.1\r\n\r\n";
    int n = send(hw2fd, msg, strlen(msg), 0);
    if (n < 0) {
       // printf("error sending health check\n");
        pthread_exit(NULL);

    } else if (n == 0) {
        printf("tried to send a health check and the connection ended\n");
        boot(hw2server);
        pthread_exit(NULL);
    }

    fd_set set;
    FD_ZERO (&set);
    FD_SET (hw2fd, &set);
    timeout.tv_sec = 2.5;
    timeout.tv_usec = 0;

    // select return the number of file descriptors ready for reading in set
    switch (select(FD_SETSIZE, &set, NULL, NULL, &timeout)) {
        case -1:
            printf("error during select to read a response from the healthcheck, exiting\n");
            boot(hw2server);
            break;
        case 0:
            printf("timeout...\n");
            boot(hw2server);
            break;      
        default:
            if(handle_healthcheck_res(hw2fd, hw2server) ==-1){
                //printf("error handling healthcheck, maybe a bad response or hw2server closed the connection\n");
                boot(hw2server);
            }
    }
    pthread_exit(NULL);
}

void * health_main(void * data){
    (void) data;
    pthread_t  p_threads[num_hw2s];
    struct timespec   ts;
    struct timeval    tp;
    while(1){
        gettimeofday(&tp, NULL);
        /* Convert from timeval to timespec */
        ts.tv_sec  = tp.tv_sec;
        ts.tv_nsec = tp.tv_usec * 1000;
        ts.tv_sec += 3;
        pthread_mutex_lock(&health_mut);
        pthread_cond_timedwait(&health_cv, &health_mut, &ts);
        pthread_mutex_unlock(&health_mut);
        pthread_mutex_lock(&counting_mut);
        if(req_count<=R){
            req_count=0;
        }else{
            req_count-=R;
        }
        pthread_mutex_unlock(&counting_mut);
        for (int i=0; i<num_hw2s; i++) {
            pthread_create(&p_threads[i], NULL, healthcheck, (void*)( hw2servers_h+i));
        }
 }
 
}


uint8_t handle_cla(int argc, char * argv[], uint16_t * port,  uint16_t ** hw2servers,  uint16_t * num_servers){
    uint64_t num_other_args= argc;
    int64_t option_index = 0;
    while((option_index = getopt(argc, argv, "N:R:")) != -1){
        switch(option_index){
            case 'N':
                if(atoi(optarg) == 0 || atoi(optarg) <0){
                    perror("enter a valid number of threads.\n");
                    return 1;
                }else{
                    N=atoi(optarg);
                }
                num_other_args=num_other_args-2;
                break;
            case 'R':
               if(atoi(optarg) == 0|| atoi(optarg) <0){
                    perror("enter a valid number of requests.\n");
                    return 1;
                }else{
                    R=atoi(optarg);
                }
                num_other_args=num_other_args-2;
                break;
            default:
                //printf("invalid option");
                return 1;
        }
    }
    if(num_other_args < 3){
        perror("Please specify a port number for clients and the port number of a server(s).\n" );
        return 1;
    }
    *num_servers=num_other_args-2;
    //printf("%d\n", *num_servers); 
    //taking out the binary name and the client port
    //find the the port clients will connect to
    int i =1;
    while(i<argc){
        if(*argv[i]=='-'){
            i+=2;
        }
        else{
            if(atoi(argv[i]) == 0){
                 printf("enter a valid client port number.\n");
                return 1;
            }else{
                *port=atoi(argv[i]);
                i++;
                break;
                
            }
        }          
    }
    int j =0;
    (*hw2servers) = (uint16_t *) malloc(sizeof(uint16_t) * (*num_servers));
    memset(*hw2servers, 0, sizeof(uint16_t) * (*num_servers));
    while(i<argc){
       // printf("i: %d\n", i);
        if(*argv[i]=='-'){
            i+=2;
        }
        else{
            if(atoi(argv[i]) == 0){
                 //printf("enter a valid client port number.\n");
                return 1;
            }else{
                //printf("%d\n",atoi(argv[i]));
                //printf("j: %d\n",j);
                *((*hw2servers)+j) =atoi(argv[i]);
                j++;
                i++;
            }
        }          
    }
    return 0;
}

void init_priority(uint16_t * hw2servers){
    pthread_t  p_threads[num_hw2s];
    priority_array = (struct hw2server **)calloc(1,sizeof(struct hw2server*) *num_hw2s);
    for(int i =0; i <num_hw2s; i++){
        struct hw2server * server = (struct hw2server *)calloc(1,sizeof(struct hw2server));
        server->name=hw2servers[i];
        server->booted=0;
        server->requests=0;
        server->errors=0;
        *(priority_array+i) = server;
    }
    for (int i=0; i<num_hw2s; i++) {
            pthread_create(&p_threads[i], NULL, healthcheck, (void*)( hw2servers_h+i));
    }
     for (int i=0; i<num_hw2s; i++) {
            pthread_join(p_threads[i], NULL);
    }
  
}


int main(int argc,char **argv) {
    uint16_t port = 0;
    if(handle_cla(argc, argv,&port, &hw2servers_h, &num_hw2s) == 1){
        exit(1);
    }
    //printf("%d\n", R);
    //printf("%d\n", N);
    //printf("%d\n", port);
    //printf("\n");
    //printf("%d\n", num_hw2s);

    uint16_t hw2servers[num_hw2s];
    for(int i=0; i< num_hw2s; i++){
        hw2servers[i]= *(hw2servers_h +i);
    }

    //connectport = atoi(argv[1]);
    //listenport = atoi(argv[2]);
    //hw2fd = client_connect(connectport);
    //Initialize the healthcheck condition variable
    if((pthread_cond_init(&health_cv, NULL)) == -1){
        printf("failure to initialize dispach condtion variable");
        return 1;
    };
    //initialize client cv
    if((pthread_cond_init(&cv, NULL)) == -1){
        printf("failure to initialize dispach condtion variable");
        return 1;
    };

    //send out the handlers
    init_priority(hw2servers);
    pthread_t  h_thread;

	pthread_create(&h_thread, NULL, health_main, NULL);
    

    pthread_t  p_threads[N];
    for (int i=0; i<N; i++) {
	    pthread_create(&p_threads[i], NULL, worker_main, NULL);
    }

    
 
    int listenfd;
    if ((listenfd = server_listen(port)) < 0){
        err(1, "failed listening");
        return 1;
    }
    for(;;){
        struct sockaddr client_addr;
        socklen_t client_addrlen;
        int client_sockd = accept(listenfd, &client_addr, &client_addrlen);
        //printf("[+] adding job...\n");
        add_job(client_sockd);
        pthread_mutex_lock(&counting_mut);
        req_count++;
        if (req_count>=R){
            pthread_cond_signal(&health_cv);
        }
        pthread_mutex_unlock(&counting_mut);
        
    }
    
    return 0;
}
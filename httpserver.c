#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <err.h>
#include <regex.h>    
#include <errno.h>    
#include <getopt.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#define BUFFER_SIZE 4096
struct Request {
   char  resource_name[BUFFER_SIZE];
   int   content_length;
   char  type[BUFFER_SIZE];
   int   error;
   char * payload_pos;
};

struct HeaderBuffer {
   char buff[BUFFER_SIZE+1];
   int   error;
   int buffsize;
};

struct ReqNode {
    uint32_t fd;
    struct ReqNode * next;

};

struct ReqNode * head = NULL;
struct ReqNode * tail = NULL;
uint32_t num_requests = 0;
uint32_t LOGFD = 0;
pthread_cond_t cv;
pthread_mutex_t ll_mut = PTHREAD_MUTEX_INITIALIZER;
atomic_int where_to_write =0;
atomic_int num_errors =0;
atomic_int num_entries =0;
atomic_int tmpfile_incr=0;


void res(int client_sockd, uint32_t code, uint64_t content_length){
    char res_buff[BUFFER_SIZE];
    if(code!= 200){
        char *message;
        switch (code){
        case 400: message = "Bad Request"; break;
        case 403: message = "Forbidden"; break;
        case 500: message = "Internal Server Error"; break;
        case 404: message = "Not Found"; break;
        case 201: message = "Created"; break;
        }
        snprintf(res_buff, BUFFER_SIZE, "HTTP/1.1 %d %s\r\nContent-Length: 0\r\n\r\n", code, message); 
    }else{
        snprintf(res_buff, BUFFER_SIZE, "HTTP/1.1 400 OK\r\nContent-Length: %ld\r\n\r\n", content_length);   
    }

    
    ssize_t num_bytes_written;
    if(content_length==0){
        num_bytes_written = send(client_sockd, res_buff, strlen(res_buff)*sizeof(char),0);
    }else{
        num_bytes_written = send(client_sockd, res_buff, strlen(res_buff)*sizeof(char), MSG_MORE);
    }
    if(num_bytes_written == -1){
        //printf("failed to send a message");
    }
    return;
}
//error checking
struct HeaderBuffer* read_header(int client_sockd){
    //printf("create a char buffer...\n");
    struct HeaderBuffer *HBuff;
    HBuff =(struct HeaderBuffer *)malloc(sizeof(struct HeaderBuffer));
    HBuff->error=200;
    memset(HBuff->buff, '\0', sizeof(HBuff->buff));
    int32_t left_to_read = BUFFER_SIZE;
    char * end_of_buff= HBuff->buff;


    while(1){
        ssize_t bytes_read = read(client_sockd, end_of_buff, left_to_read);
        left_to_read = left_to_read-bytes_read;
        if(bytes_read==-1){
            HBuff->error = 500;
            return HBuff;
        }
        if(strstr(HBuff->buff, "\r\n\r\n")==NULL){
            if(left_to_read == 0){
                HBuff->error = 400;
                return HBuff;
            }else{
                end_of_buff = end_of_buff + sizeof(char)*bytes_read;
            }
        }else{

            HBuff->buffsize = BUFFER_SIZE - left_to_read;
            //printf("%s", HBuff->buff);
            return HBuff;
        }
    }
}

int64_t find_content_length(char * after_first_line){
    int cl=-1;
    int * content_length = &cl;
    char after_first_line_copy[strlen(after_first_line)];
    strcpy(after_first_line_copy, after_first_line);
    char * rest = NULL;
    char* curr_line=strtok_r(after_first_line_copy, "\r\n", &rest);
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

int32_t valid_kv_pairs(char* first_line, char * type){
    regex_t regex;
    int reti;
    if (strcmp("PUT", type)==0){
        reti = regcomp(&regex,"^(.+: .+\r\n)+\r\n.*", REG_EXTENDED);

    }else{
        reti = regcomp(&regex,"^(.+: .+\r\n)+\r\n", REG_EXTENDED);
    }
    if (reti) {
        //printf("Could not compile regex\n");
        return 500;
    }
    reti = regexec(&regex, first_line, 0, NULL, 0);
    if (!reti) {
        //printf("rest of headers match!\n");
    }
    else if (reti == REG_NOMATCH) {
        //printf("No match\n");
        return 400;
    }
    else {
        return 500;
    }
    //printf("%s", first_line);
    return 0;
}

int32_t valid_resource(char * resource){
    regex_t regex;
    int reti;
    if(strlen(resource) > 28){
        //ERROR assingment is too long
        return 400;
    }
    if(*resource != '/'){
        //perror("resource doesn't begin with a backslash");
        return 400;
    }
    reti = regcomp(&regex,"^/[[:alnum:]_-]+$", REG_EXTENDED);
    if (reti) {
        //printf("Could not compile regex\n");
        return 500;
    }
    reti = regexec(&regex, resource, 0, NULL, 0);
    if (!reti) {
    }
    else if (reti == REG_NOMATCH) {
        //printf("No match");
        return 400;
    }
    else {
        return 500;
    }
    return 0;
}

struct Request * parse_header(char *buff){

    //Initialize a Request struct 
    struct Request *Req;
    Req=(struct Request *)malloc(sizeof(struct Request));
    Req->error=200;
    Req->content_length=0;
    memset(Req->type, '\0', sizeof(Req->type));
    memset(Req->resource_name, '\0', sizeof(Req->resource_name));
    
    
    //Begin to Parse
    char * first_line= strtok_r(buff, "\r\n", &buff);
    char type [BUFFER_SIZE];
    char resource [BUFFER_SIZE];
    char http [BUFFER_SIZE];
    int num_matched = sscanf(first_line, "%s %s %s" ,type, resource, http);
    if(num_matched!=3){
        printf("ERROR more or less than 3 initial params the the HTTP request..\n");
        Req->error=400;
        return Req;
    }
    
    //printf("setting resource name...\n");
    strcpy(Req->type, type);
    strcpy(Req->resource_name, resource + sizeof(char));
    if(valid_resource(resource)== 400){
        //printf("error: invalid resource name...\n");
        Req->error=400;
        return Req;
    }
    if(valid_resource(resource)== 500){
        //printf("error: server error checking resource name...\n");
        Req->error=500;
        return Req;
    }
    if(strcmp(http, "HTTP/1.1") != 0){
       // printf("error: last param (HTTP/1.1) his wrong...\n");
        Req->error=400;
        return Req;
    }
    if(strcmp(type, "GET") != 0 && strcmp(type, "HEAD") != 0 && strcmp(type, "PUT") != 0){
       // printf("ERROR first param isnt GET HEAD or PUT...\n");
        Req->error=400;
        return Req;
    }
    //printf("setting request type...\n");
    //printf("checking if header is formatted corretly...\n");
    char * after_first_line= first_line + strlen(first_line)*sizeof(char)+2;
    int32_t header_format_error = valid_kv_pairs(after_first_line, type);
    if(header_format_error==500){
        //printf("Internal REGEX error...\n");
        Req->error=500;
        return Req;
    }
    if(header_format_error==400){
        //printf("ERROR weird headers");
        Req->error=400;
        return Req;
    }

    if(strcmp(type, "PUT") == 0){
        //printf("seting content length and payload point for a put...\n");
        int64_t content_length = find_content_length(after_first_line);
        if(content_length==-1){
            //printf("content length not found...\n");
            Req->error=400;
            return Req;
        }
        else{
             Req->content_length=content_length;
        }
        //check if there is a payload
        char* first_DCRNL=strstr(after_first_line, "\r\n\r\n");
        if(strlen(first_DCRNL)!=4){
            //payload is in this read.
            Req->payload_pos= first_DCRNL + 4*sizeof(char);
        }
    }
    return Req;
}

//queue
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
int space_to_reserve(char * tempfilename, int first_line_len){
    struct stat sb;
    stat(tempfilename, &sb);
    uint64_t payload_size = (uint64_t) sb.st_size;
    int bytes_in_last_line = payload_size%20;
    int lines=payload_size/20;
    if(bytes_in_last_line != 0){
        lines++;
    }
    return payload_size*3 +9*lines + first_line_len +9;
}
int get_num_lines(char * tempfilename){
    struct stat sb;
    stat(tempfilename, &sb);
    uint64_t payload_size = (uint64_t) sb.st_size;
    int bytes_in_last_line = payload_size%20;
    int lines=payload_size/20;
    if(bytes_in_last_line != 0){
        lines++;
    }
    return lines;
}
void log_func(struct Request * Req, char * tmpfilename){
    //printf("in log func\n");
    if(LOGFD==0){
        remove(tmpfilename);
        return;
    }
    int first_line_len;
    char res_buff[BUFFER_SIZE];
    if(Req->error != 200 && Req->error != 201){
        first_line_len = snprintf(res_buff, BUFFER_SIZE, "FAIL: %s /%s HTTP/1.1 --- response %d\n========\n", Req->type ,Req->resource_name, Req->error);
        int myoffset = __atomic_fetch_add (&where_to_write, first_line_len, __ATOMIC_SEQ_CST);
        pwrite(LOGFD, res_buff, first_line_len, (off_t) myoffset);
        num_errors ++;
    }else{
        if(strcmp(Req->type, "PUT")==0 ||strcmp(Req->type, "GET")==0 ){
           // printf("logging successfullget\n");
            first_line_len = snprintf(res_buff, BUFFER_SIZE, "%s /%s length %d\n", Req->type ,Req->resource_name, Req->content_length);
            int bytes_to_write=space_to_reserve(tmpfilename, first_line_len);
           // char final_buff[bytes_to_write];
            //char * writeoffset = final_buff;
            int myoffset = __atomic_fetch_add (&where_to_write, bytes_to_write, __ATOMIC_SEQ_CST);
           // strcpy(writeoffset, res_buff);
            //writeoffset += sizeof(char)*first_line_len;
            pwrite(LOGFD,res_buff,first_line_len,(off_t) myoffset);
            myoffset+=first_line_len;
            //first line
            ssize_t tmpfd =open(tmpfilename, O_RDONLY, S_IRUSR | S_IWUSR);
            int lines = get_num_lines(tmpfilename);
            for(int i=0; i<lines; i++ ){
                char count_buff[9];
                snprintf(count_buff, 9, "%08d", i*20);
                //strcpy(writeoffset, count_buff);
                pwrite(LOGFD,count_buff,8,(off_t) myoffset);
                myoffset+=8;
                //writeoffset+= sizeof(char)*8;
                char buff[20];
                int bytes_read =read(tmpfd, buff, 20);
                for(int j=0; j<bytes_read; j++){
                    char small_buff[4];
                    snprintf(small_buff, 4, " %02x", (uint)*(buff +sizeof(char)*j) & 0xff);
                    //strcpy(writeoffset, small_buff);
                    pwrite(LOGFD,small_buff,3,(off_t) myoffset);
                    myoffset+=3;
                    //writeoffset+= sizeof(char)*3;
                }
                //strcpy(writeoffset, "\n");
                pwrite(LOGFD,"\n",1,(off_t) myoffset);
                myoffset++;
                //writeoffset+= sizeof(char);
            } 
            //strcpy(writeoffset, "========\n");
            pwrite(LOGFD,"========\n",9,(off_t) myoffset);
            //pwrite(LOGFD, final_buff, bytes_to_write,(off_t) myoffset);
            close(tmpfd);
        }
        if(strcmp(Req->type, "HEAD")==0 ){
            first_line_len = snprintf(res_buff, BUFFER_SIZE, "%s /%s length %d\n========\n", Req->type ,Req->resource_name, Req->content_length);
            int myoffset = __atomic_fetch_add (&where_to_write, first_line_len, __ATOMIC_SEQ_CST);
            pwrite(LOGFD,res_buff,first_line_len,(off_t) myoffset);
        }
    }
    remove(tmpfilename);
    num_entries++;
    return;
}
void log_no_header(struct HeaderBuffer * HBuff){
    if(LOGFD==0){
        return;
    }
    int first_line_len;
    char res_buff[BUFFER_SIZE];
    HBuff->buff[42]='\0';
    first_line_len = snprintf(res_buff, BUFFER_SIZE, "FAIL: %s --- response %d\n========\n", HBuff->buff ,HBuff->error);
    int myoffset = __atomic_fetch_add (&where_to_write, first_line_len, __ATOMIC_SEQ_CST);
    pwrite(LOGFD, res_buff, first_line_len, (off_t) myoffset);
    num_errors ++;
    num_entries ++;
    return;
}

//processing
void process_put(struct HeaderBuffer * HBuff ,struct Request* Req, int client_sockd){
    if(strcmp(Req->resource_name, "healthcheck")==0){
        Req->error =403;
        res(client_sockd, Req->error, 0);
        close(client_sockd);
        log_func(Req, NULL);
        free(Req);
        return;
    }

    int64_t payload_left=Req->content_length;
    //opentempfile
    int tmpincr = __atomic_fetch_add (&tmpfile_incr, 1, __ATOMIC_SEQ_CST);
    char tmpfilename[64+4];
    sprintf(tmpfilename,"%d.txt", tmpincr);
    ssize_t putfd =open(Req->resource_name, O_CREAT|O_RDWR |O_TRUNC, S_IRUSR | S_IWUSR);
    ssize_t tmpfd =open(tmpfilename, O_CREAT|O_RDWR |O_TRUNC, S_IRUSR | S_IWUSR);
    if(putfd == -1){
        printf("error opening file on server\n");
        if(errno == EACCES){
            Req->error= 403;
        }else{
            Req->error= 500;
        }
        res(client_sockd, Req->error, 0);
        close(client_sockd);
        log_func(Req, tmpfilename);
        close(tmpfd);
        free(Req);
        free(HBuff);
        return; 
    }
   
    if(Req->payload_pos != 0){
        int header_len = Req->payload_pos-HBuff->buff;
        int32_t payload_in_first_buff = HBuff->buffsize - header_len;
        //printf("payload in first buff %d\n",payload_in_first_buff);
        ssize_t bytes_written = write(putfd, (int64_t *)Req->payload_pos, payload_in_first_buff);
       // printf("bytes written %ld\n",bytes_written);

        payload_left = payload_left-payload_in_first_buff;
       //printf("new payload left %d\n", payload_left);

        write(tmpfd, Req->payload_pos, payload_in_first_buff);
        //printf("bytes written %ld\n",bytes_written);

        if(bytes_written == -1){
           // printf("Error writing part of the body.\n");
            Req->error=500;
            res(client_sockd, Req->error, 0);
            close(client_sockd);
            log_func(Req, tmpfilename);
            close(putfd);
            close(tmpfd);
            free(Req);
            free(HBuff);
            return;
            }
    }
    int8_t ERR=0;
    while(payload_left>0){
      //  printf("payload left:     %d\n", payload_left);
        char buffer[BUFFER_SIZE];
        memset(&buffer[0], 0, sizeof(buffer));
        ssize_t bytes_read = read(client_sockd, buffer, BUFFER_SIZE);
        //buffer[bytes_read+1]=0;
        //buffer[BUFFER_SIZE]=0; not doing shit
        if(bytes_read==-1){
            printf("cant read body from the socket...\n");
            ERR =-1;
            break;
        }
        payload_left =payload_left-bytes_read;
       // printf("bytes read: %ld\n", bytes_read);
        write(tmpfd, buffer, bytes_read);
        ssize_t bytes_written = write(putfd, buffer, bytes_read);
        if(bytes_written == -1){
            printf("cant write body to the server...\n");
            ERR=-1;
            break;
        }
    }
    if(ERR==-1){
        res(client_sockd, 500, 0);
        close(client_sockd);
        log_func(Req, tmpfilename);

    }else{
        res(client_sockd, 201, 0);
        close(client_sockd);
        log_func(Req, tmpfilename);

    }
    close(tmpfd);
    close(putfd);
    free(Req);
    free(HBuff);
    return;
}

void process_head(struct HeaderBuffer * HBuff ,struct Request* Req, int client_sockd){
    //printf("handling a HEAD...\n");
    free(HBuff);
    if(strcmp(Req->resource_name, "healthcheck")==0){
        Req->error =403;
        res(client_sockd, Req->error, 0);
        close(client_sockd);
        log_func(Req, NULL);
        free(Req);
        return;
    }
    ssize_t getfd = open(Req->resource_name, O_RDONLY);
    if(getfd == -1){
        //printf("error opening file on server\n");
        if(errno == EACCES){
            Req->error= 403;
        }else if(errno ==ENOENT){
            Req->error= 404;
        }else{
            Req->error= 500;
        }
        res(client_sockd, Req->error, 0);
        close(client_sockd);
        log_func(Req, NULL);
        free(Req);
        return; 
    }
    //printf("handling a head that was able to open the requested file...\n");
    struct stat sb;
    if (stat(Req->resource_name, &sb) == -1) {
    // printf("ERROR cant check size of requested file\n");
        Req->error= 500;
        res(client_sockd, Req->error, 0);
        close(client_sockd);
        log_func(Req, NULL);
        close(getfd);
        free(Req);
        return;  
    }
    //printf("handling a HEAD that was able to open the requested file...\n");
    Req->content_length=(uint64_t) sb.st_size;
    char res_buff[BUFFER_SIZE];
    snprintf(res_buff, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", Req->content_length); 
    send(client_sockd, res_buff, strlen(res_buff)*sizeof(char),0);
    close(client_sockd);
    log_func(Req, NULL);
    close(getfd);
    free(Req);
    return; 
}

void process_get(struct HeaderBuffer * HBuff ,struct Request* Req, int client_sockd){
    //printf("handling a GET...\n");
    int tmpincr = __atomic_fetch_add (&tmpfile_incr, 1, __ATOMIC_SEQ_CST);
    char tmpfilename[64+4];
    sprintf(tmpfilename,"%d.txt", tmpincr);
    ssize_t tmpfd =open(tmpfilename, O_CREAT|O_RDWR |O_TRUNC, S_IRUSR | S_IWUSR);

    free(HBuff);
     if(strcmp(Req->resource_name, "healthcheck")==0){
        if(LOGFD==0){
            Req->error =404;
            res(client_sockd, Req->error, 0);
            close(client_sockd);
            log_func(Req, tmpfilename);

        }else{
            char health[200];
            int pl_sz = snprintf(health, 200, "%d\n%d", num_errors, num_entries);
            Req->content_length=pl_sz;

            res(client_sockd, Req->error, pl_sz);
            send(client_sockd, health, pl_sz, 0);
            close(client_sockd);
            write(tmpfd, health, pl_sz);
            log_func(Req, tmpfilename);
        }
        close(tmpfd);
        free(Req);
        return;
    }
    ssize_t getfd = open(Req->resource_name, O_RDONLY);
    if(getfd == -1){
        //printf("error opening file on server\n");
        if(errno == EACCES){
            Req->error= 403;
        }else if(errno ==ENOENT){
            Req->error= 404;
        }else{
            Req->error= 500;
        }
        res(client_sockd, Req->error, 0);
        close(client_sockd);
        log_func(Req, tmpfilename);
        free(Req);
        return; 
    }
    //printf("handling a get that was able to open the requested file...\n");
    struct stat sb;
    if (stat(Req->resource_name, &sb) == -1) {
    // printf("ERROR cant check size of requested file\n");
        Req->error= 500;
        res(client_sockd, Req->error, 0);
        close(client_sockd);
        log_func(Req, tmpfilename);
        close(getfd);
        free(Req);
        return;  
    }
    //printf("handling a get that was able to open the requested file and check its size...\n");
    uint64_t content_length = (uint64_t) sb.st_size;
    uint64_t left_to_read =content_length;
    //printf("%ld\n", left_to_read);
    res(client_sockd, Req->error, left_to_read); //200
    ssize_t num_bytes_read =-1;
    while(left_to_read>0){
        char buffer[BUFFER_SIZE];
        num_bytes_read =read(getfd, buffer, BUFFER_SIZE);
        if(num_bytes_read == -1){
            //printf("error reading the file from server...\n"); 
            break; 
        }
        //printf("more content to read...\n"); 
        left_to_read = left_to_read-num_bytes_read;
        ssize_t num_bytes_written;
        if(left_to_read<=0){
            //printf("nothing left to read\n");
            num_bytes_written = send(client_sockd, buffer, num_bytes_read,0);
            write(tmpfd,buffer,num_bytes_read);
            break;
        }else{
            //printf("more to read\n");
            write(tmpfd,buffer,num_bytes_read);
            num_bytes_written = send(client_sockd, buffer, num_bytes_read, MSG_MORE);
        }
        if(num_bytes_written == -1){
            //printf("could not send to client"); 
        }
    }
    Req->content_length=content_length;
    close(client_sockd);
    log_func(Req, tmpfilename);
    close(tmpfd);
    close(getfd);
    free(Req);
    return;  
}

void process_request(int client_sockd){
    struct HeaderBuffer * HBuff =read_header(client_sockd);
    if(HBuff->error!=200){
        res(client_sockd, HBuff->error, 0);
        log_no_header(HBuff);
        close(client_sockd);
        free(HBuff);
        return;
    }
    struct Request * Req= parse_header(HBuff->buff);
    // printf("printing the struct request generated by first buffer:\n");
    //printf("%s %d %s %d\n", Req->resource_name, Req->content_length, Req->type, Req->error);
    if(Req->error !=200){
        printf("%d\n", Req->error);
        res(client_sockd, Req->error, 0);
        log_func(Req, NULL);
        close(client_sockd);
        free(HBuff);
        free(Req);

        return;
    }
    if(strcmp(Req->type,"PUT")==0){
        process_put(HBuff, Req, client_sockd);
    }
    else if(strcmp(Req->type,"HEAD")==0){
        process_head(HBuff, Req, client_sockd);
    } 
    else if(strcmp(Req->type,"GET")==0){
        process_get(HBuff, Req, client_sockd);               
    }   
}

uint8_t handle_cla(int argc, char * argv[], char ** log_file, uint16_t * num_threads, char  ** port){
    uint64_t num_other_args= argc;
    int64_t option_index = 0;
    while((option_index = getopt(argc, argv, "N:l:")) != -1){
        switch(option_index){
            case 'N':
                if(atoi(optarg) == 0){
                    printf("enter a valid number of threads.\n");
                    return 1;
                }else{
                    *num_threads=atoi(optarg);
                }
                num_other_args=num_other_args-2;
                break;
            case 'l':
                *log_file =(char *)malloc(sizeof(char) * strlen(optarg));
                strcpy(*log_file, optarg);
                num_other_args=num_other_args-2;
                break;   
            default:
                printf("ivalid option");
                return 1;
        }
    }
    if(num_other_args > 2){
        fprintf(stderr, "Too many command line arguments.\n");
        return 1;
    }
    if(num_other_args < 2){
        fprintf(stderr,"Please specify a port number.\n" );
        return 1;
    }
    if(num_other_args==2){
        int16_t arg;
        if(*argv[1] == '-'){
            if (*argv[3] == '-'){
                arg =5;
            }else{
               arg =3;
            }
        }else{
          arg =1;
        }
        if(atoi(argv[arg])<8000){
            fprintf(stderr,"Please specify a port number greater than 8000.\n");
            return 1;
        }
        *port =(char *)malloc(sizeof(char) * strlen(argv[arg]));
        strcpy(*port, argv[arg]);
    }
    return 0;
}

//mains
void * worker_func(void * data){
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

int main(int argc, char *argv[]) {
    uint16_t num_threads = 4;
    char * log_file_h= NULL;
    char  * port_h=NULL;
    if (handle_cla(argc, argv, &log_file_h, &num_threads, &port_h) == 1){
        return 1;
    }
    char port[strlen(port_h)];
    strcpy(port, port_h);
    free(port_h);

    if(log_file_h!= NULL){
        ssize_t lfd =open(log_file_h, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR | S_IWUSR);
        if(lfd==-1){
            printf("couldnt open that log file\n");
            return 1;
        }
        LOGFD=lfd;
    }

    // printf("logfile: %s\n", log_file);
    // printf("num_threads: %d\n", num_threads);
    // printf("port: %s\n", port);
   
    /*Create sockaddr_in with server information*/
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(server_addr);
    /*Create server socket*/
    int server_sockd = socket(AF_INET, SOCK_STREAM, 0);
    // Need to check if server_sockd < 0, meaning an error
    if (server_sockd < 0) {
        perror("socket");
        exit(1);

    }
    /* Configure server socket*/
    int enable = 1;
    /*This allows you to avoid: 'Bind: Address Already in Use' error*/
    int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    /* Bind server address to socket that is open*/
    ret = bind(server_sockd, (struct sockaddr *) &server_addr, addrlen);
    /*Listen for incoming connections*/
    ret = listen(server_sockd, SOMAXCONN); // 5 should be enough, if not use SOMAXCONN
    if (ret < 0) {
        exit(1);
    }

    pthread_t  p_threads[num_threads];
    for (int i=0; i<num_threads; i++) {
	    pthread_create(&p_threads[i], NULL, worker_func, NULL);
    }

    if((pthread_cond_init(&cv, NULL)) == -1){
        printf("failure to initialize dispach condtion variable");
        return 1;
    };

    for(;;){
        struct sockaddr client_addr;
        socklen_t client_addrlen;
        int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);
        //printf("[+] adding job...\n");
        add_job(client_sockd);
    }
    
    return 0;
}




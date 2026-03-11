#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string.h>

char bad_request[] = "HTTP/1.0 400 Bad Request\r\n";

char permission_denied[] = "HTTP/1.0 403 Permission Denied\r\n";

char not_found[] ="HTTP/1.0 404 Not Found\r\n";

char internal_error[]="HTTP/1.0 500 Internal Error\r\n";

char not_implemented[] = "HTTP/1.0 501 Not Implemented\r\n";

void sigchild_handler(int signo){
   while(1){
      pid_t pid=waitpid(-1,NULL,WNOHANG);
      if (pid<=0){
         printf("Cleaned up terminated processes\n");
         break; //'reaped' all terminated children
      }
   }
}

void error_response(char error[],int nfd){
   write(nfd,error,strlen(error));

   char ct[]="Content-Type: text/html\r\n";
   write(nfd,ct,strlen(ct));
   char s[]="<!DOCTYPE html><head><title>Error</title></head><body><h1>Error</h1></body></html>\r\n";
   write(nfd,s,strlen(s));
}

void handle_request(int nfd)
{
   FILE *network = fdopen(nfd, "r+");
   if (network == NULL)
   {
      error_response(internal_error,nfd);
      close(nfd);
      exit(1);
   }
   printf("handling request\n");
   char *line = NULL;
   size_t hlen=0;
   ssize_t read;
   char* method;
   char* filename;
   char* req3;
   //Getting the html request
   read=getline(&line,&hlen,network);
   printf("%s\n",line);
   printf("past this point");
   //Should contain either GET or HEAD
   method=strtok(line," ");
   printf("%s\n",method);
   if (method==NULL){
      error_response(bad_request,nfd);
      exit(1);
   }

   //Should be an html filename
   filename=strtok(NULL," ");
   printf("%s\n",filename);
   if (filename==NULL){
      error_response(bad_request,nfd);
      exit(1);
   }

   //We also want to make sure the third part of the request is there,
   //even though we wont use it
   req3=strtok(NULL," ");
   printf("%s\n",req3);
   if (req3==NULL){
      error_response(bad_request,nfd);
      exit(1);
   }

   //Making sure method is either HEAD or GET
   if (strcmp(method,"HEAD")!=0 && strcmp(method,"GET")!=0){
      error_response(not_implemented,nfd);
      exit(1);
   }

   //Creating the HTML header
   //And opening desired file
   char* directory="cgi-like";
   char pathname[128];
   snprintf(pathname,sizeof(pathname),"%s%s",directory,filename);
   printf("%s\n",pathname);

   FILE *fp=fopen(pathname,"r");
   if(fp==NULL){
      printf("file open error\n");
      error_response(not_found,nfd);
      exit(1);
   }

   char buffer[1024];
   ssize_t len=0;
   struct stat fileStat;
   stat(pathname,&fileStat);

   len+=snprintf(buffer+len,sizeof(buffer)-len,"HTTP/1.0 200 OK\r\n");
   len+=snprintf(buffer+len,sizeof(buffer)-len,"Content-Type: text/html\r\n");
   len+=snprintf(buffer+len,sizeof(buffer)-len,"Content-Length: %ld\r\n",fileStat.st_size);
   len+=snprintf(buffer+len,sizeof(buffer)-len,"\r\n");

   write(nfd,buffer,len);

   //The actual file contents
   //SHOULD ONLY WORK WITH A 'HEAD' REQUEST
   if(strcmp(method,"GET")==0){
      while((read=getline(&line,&hlen,fp))!=-1){
         write(nfd,line,read);
      }
   }
   fclose(fp);
   
   
   free(line);
   fclose(network);
}

void run_service(int fd)
{
   pid_t pid;

   struct sigaction sa;
   sa.sa_handler = sigchild_handler;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

   if (sigaction(SIGCHLD,&sa,NULL)==-1){
      printf("sigaction error\n");
      exit(1);
   }

   while (1)
   {
      int nfd = accept_connection(fd);
      if (nfd != -1)
      {
         pid=fork();

         if (pid<0){
            printf("fork error\n");
            exit(1);
         }
         else if (pid==0){
            //child 
            printf("Connection established\n");
            handle_request(nfd);
            printf("Connection closed\n");

            //child closes fd and exits after handling request
            close(nfd);
            exit(0);
         }
         else{
            //parent
            //parent does not need fd at all to begin with
            close(nfd);
         }
      }
   }
}

int main(int argc, char* argv[])
{
   //Taking in a port number from argv[1] with error checking
   int portNum;
   char* endptr;

   //Checking arg count
   if (argc!=2){
      printf("usage: ./httpd <port number (int)>\n");
      exit(1);
   }
   portNum=strtol(argv[1],&endptr,10);

   if (*endptr!='\0'){
      printf("invalid port number\n");
      exit(1);
   }

   //Using the selected port
   int fd = create_service(portNum);

   if (fd == -1)
   {
      perror(0);
      exit(1);
   }

   printf("listening on port: %d\n", portNum);
   run_service(fd);
   close(fd);

   return 0;
}

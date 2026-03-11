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
#include <fcntl.h>

char bad_request[] = "HTTP/1.0 400 Bad Request\r\n";
char permission_denied[] = "HTTP/1.0 403 Permission Denied\r\n";
char not_found[] ="HTTP/1.0 404 Not Found\r\n";
char internal_error[]="HTTP/1.0 500 Internal Error\r\n";
char not_implemented[] = "HTTP/1.0 501 Not Implemented\r\n";

#define MAX_SUB_ARGS 16

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
   char *line = NULL;
   size_t hlen=0;
   ssize_t read;
   char* method;
   char* filename;
   char* req3;


   //Getting the html request
   printf("reached right here\n");
   read=getline(&line,&hlen,network);
   printf("%s\n",line);

   //Should contain either GET or HEAD
   method=strtok(line," ");
   printf("%s\n",method);
   if (method==NULL){
      error_response(bad_request,nfd);
      return;
   }

   //Should be an html filename
   filename=strtok(NULL," ");
   printf("%s\n",filename);
   if (filename==NULL){
      error_response(bad_request,nfd);
      return;
   }

   //We also want to make sure the third part of the request is there,
   //even though we wont use it
   req3=strtok(NULL," ");
   printf("%s\n",req3);
   if (req3==NULL){
      error_response(bad_request,nfd);
      return;
   }

   //Making sure method is either HEAD or GET
   if (strcmp(method,"HEAD")!=0 && strcmp(method,"GET")!=0){
      error_response(not_implemented,nfd);
      return;
   }

   //Creating the HTML header
   //And opening desired file
   //removing the / from the begginning 
   memmove(filename,filename+1,strlen(filename));

   //Checking for a ? and splitting the string, in the case
   //a cgi command is ran with arguments
   filename=strtok(filename,"?");

   //if args are present
   char* args=strtok(NULL,"?");

   FILE *fp=fopen(filename,"r");
   if(fp==NULL){
      printf("file open error\n");
      error_response(not_found,nfd);
      fclose(fp);
      return;
   }

   //Checking if the request was for a cgi-like command
   char* temp;
   temp=strtok(filename,"/");

   if (strcmp(temp,"cgi-like")==0){
      //we need to open/create a new file, exec
      //and write the commands output to that file,
      //then use that file for the remainder of the program
      printf("cgi-like command: True\n");

      char* command=strtok(NULL,"/");
      printf("command: %s\n",command);
      printf("args: %s\n",args);

      //now args need to be split into an actual list
      char* argList[MAX_SUB_ARGS];
      char* token=strtok(args,"&");
      int count=1;
      argList[0]=command;

      while (token!=NULL){
         argList[count]=token;
         token=strtok(NULL,"&");
         count+=1;
      }
      argList[count]=NULL;

      //forking a process to make the temp file
      //and then exec with output directed to said file
      printf("about to open output.txt\n");
      int subfd=open("output.txt", O_WRONLY|O_CREAT|O_TRUNC,0644);
      if (subfd<0){
         printf("subfd open error\n");
         exit(1);
      }
      printf("opened output.txt\n");
      pid_t subpid=fork();

      printf("successful fork for exec\n");
      if (subpid<0){
         printf("fork error\n");
         close(subfd);
         return;
      }
      else if (subpid==0){
         //child
         if (dup2(subfd,STDOUT_FILENO)<0){
            printf("dup2 error\n");
            close(subfd);
            exit(1);
         }
         close(subfd);
         execvp(argList[0],argList);

         printf("execvp error\n");
         exit(1);
      }
      else{
         //parent
         int status;
         printf("waiting\n");
         waitpid(subpid,&status,0);
         printf("waited\n");
         //After waiting, close the previsouly open file in the parent
         //Then open the output to display the exec program contents
         fclose(fp);
         filename="output.txt";
         fp=fopen(filename,"r");
      }
   }

   char buffer[1024];
   ssize_t len=0;
   struct stat fileStat;
   stat(filename,&fileStat);

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

   //Needs to wait to close the socket and exit until client
   //exits the window. Maybe a signal?
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
            close(nfd);
            exit(1);
         }
         else if (pid==0){
            //child 
            printf("Connection established\n");
            handle_request(nfd);
            printf("Connection closed\n");

            //child closes nfd and exits after handling request
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

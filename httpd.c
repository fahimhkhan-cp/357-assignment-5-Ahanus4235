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
char br_msg[]="Bad Request\n";
char pd_msg[]="Permission Denied\n";
char nf_msg[]="Not Found\n";
char ie_msg[]="Internal Error\n";
char ni_msg[]="Not Implemented\n";

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

void error_response(char error[], char msg[], int nfd){
   char buffer[128];
   int len=0;
   len+=snprintf(buffer+len,sizeof(buffer)-len,"%s",error);
   len+=snprintf(buffer+len,sizeof(buffer)-len,"Content-Type: text/html\r\n");
   len+=snprintf(buffer+len,sizeof(buffer)-len,"%s",msg);
   write(nfd,buffer,len);
}

void handle_request(int nfd)
{
   FILE *network = fdopen(nfd, "r+");
   if (network == NULL)
   {
      error_response(internal_error,ie_msg,nfd);
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
   read=getline(&line,&hlen,network);

   //Should contain either GET or HEAD
   method=strtok(line," ");
   if (method==NULL){
      error_response(bad_request,br_msg,nfd);
      return;
   }

   //Should be an html filename or a command for the cgi-like directory
   filename=strtok(NULL," ");
   if (filename==NULL){
      error_response(bad_request,br_msg,nfd);
      return;
   }

   //We also want to make sure the third part of the request is there,
   //even though we wont use it
   req3=strtok(NULL," ");
   if (req3==NULL){
      error_response(bad_request,br_msg,nfd);
      return;
   }

   // We also need to test whether or not and .. are present in the path given
   // If so, permission denied
   char* testfilename=(char*)malloc(sizeof(char)*strlen(filename));
   strcpy(testfilename,filename);
   char* branch=strtok(testfilename,"/");

   while (branch!=NULL){
      if (strcmp(branch,"..")==0){
         error_response(permission_denied,pd_msg,nfd);
         exit(1);
      }
      branch=strtok(NULL,"/");
   }
   free(testfilename);
   //Making sure method is either HEAD or GET
   if (strcmp(method,"HEAD")!=0 && strcmp(method,"GET")!=0){
      error_response(not_implemented,ni_msg,nfd);
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
      error_response(not_found,nf_msg,nfd);
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
      char* command=strtok(NULL,"/");

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
      int subfd=open("output.txt", O_WRONLY|O_CREAT|O_TRUNC,0644);
      if (subfd<0){
         error_response(internal_error,ie_msg,nfd);
         exit(1);
      }
      pid_t subpid=fork();
      if (subpid<0){
         error_response(internal_error,ie_msg,nfd);
         close(subfd);
         return;
      }
      else if (subpid==0){
         //child
         if (dup2(subfd,STDOUT_FILENO)<0){
            error_response(internal_error,ie_msg,nfd);
            close(subfd);
            exit(1);
         }
         close(subfd);
         execvp(argList[0],argList);

         error_response(internal_error,ie_msg,nfd);
         exit(1);
      }
      else{
         //parent
         int status;
         waitpid(subpid,&status,0);
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

   //If opened file is output.txt, delete it
   if (strcmp(filename,"output.txt")==0){
      remove(filename);
   }
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

   if (*endptr!='\0' || !(portNum>=1024 && portNum<=65535)){
      printf("invalid port number (Must be in range 1024-65535)\n");
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

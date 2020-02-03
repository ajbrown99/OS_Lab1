#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <fcntl.h>
#include <sys/stat.h>

#define maxLine 2000
#define maxCharsPerToken 30
#define maxTokensPerLine 67

#define STOPPED 0
#define RUNNING 1
#define DONE 2

//int pipeFound = 0;

int jobN = 1;

struct processGroup {

    int pgid;
    int jobNumber;
    int state;
    int status;
    char** argv;
    //int argLength;
    char** leftProcess;
    char** rightProcess;
    int isInBackground;
    struct processGroup* next;
};

typedef struct processGroup job;

/*
struct processjobs {

    process_Group* next;
    process_Group* prev;  
};

typedef struct processjobs jobs;
*/

int checkForPipe(char** process){

    int index = 0;
    while(process[index] != NULL){

        if(strcmp(process[index],"|") == 0){

            return index;
        }
        index++;
    }
    return -1;

}

int parseCommand(char* input, job* j){

    int index = 0;
    char delimiter[2] = " ";
    char* token;
    token = strtok(input, delimiter);
    while(token != NULL){

        if(token[strlen(token) - 1] == '\n'){

            token[strlen(token) - 1] = '\0';
        }

        //j->argv[index] = token;
        strcpy(j->argv[index], token);
        index++;
        token = strtok(NULL, delimiter);
    }
    return index;
}

void allocateProcess(char** process){

    for(int i = 0; i < maxTokensPerLine; i++){

        process[i] = malloc(sizeof(char) * maxCharsPerToken);
    }
}

void freeProcess(char** process){

    for(int i = 0; i < maxTokensPerLine; i++){

        free(process[i]);
    }
}

void addJob(job** head, job* j){

    if(*head == NULL){

        *head = j;
    }
    else {

        j->next = *head;
        *head = j;
    }
    //j->jobNumber = jobN;
    //jobN++;
}

void outputRedirect(char** argv, int index, int fileNotExist){

    char* filename = argv[index+1];
    if(fileNotExist == 0){

        int ofd = open(filename,O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
        dup2(ofd,STDOUT_FILENO);
    } 
}

int inputRedirect(char** argv, int index){

    char* filename = argv[index+1];
    int ifd = open(filename,O_RDONLY);
    if(ifd == -1){

        return 1;
    }
    dup2(ifd,STDIN_FILENO);
    return 0;
}

void errorRedirect(char** argv, int index, int fileNotExist){

    char* filename = argv[index+1];
    if(fileNotExist == 0){

        int errorfd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
        dup2(errorfd,STDERR_FILENO);
    }
}

void doFileRedirectionNoPipe(job* j, int arglength){

    int fileRedirectCount = 0;
    //char** argvCopy;
    //argvCopy = malloc(sizeof(char*) * maxTokensPerLine);
    j->leftProcess = malloc(sizeof(char*) * maxTokensPerLine);
    allocateProcess(j->leftProcess);

    int fileNotExist = 0;
    
    for(int i = 0; i < arglength; i++){

        if(strcmp(j->argv[i],"<") == 0){

            fileNotExist = inputRedirect(j->argv,i);
            if(fileNotExist == 0){
                i++;
            }
            fileRedirectCount++;      
        }
    }
    

    int indexCopy = 0;
    //int fileNotExist = 0;
    for(int i = 0; i < arglength; i++){

        if(strcmp(j->argv[i],">") == 0){

            outputRedirect(j->argv,i,fileNotExist);
            fileRedirectCount++;
            i++;
        }
        
        else if(strcmp(j->argv[i],"<") == 0){

            //fileNotExist = inputRedirect(j->argv,i);
            if(fileNotExist == 0){
                i++;
            }
            fileRedirectCount++;
        }
        
        else if(strcmp(j->argv[i],"2>") == 0){

            errorRedirect(j->argv,i, fileNotExist);
            fileRedirectCount++;
            i++;
        }
        else {
            //strcpy(argvCopy[indexCopy],j->argv[i]);
            strcpy(j->leftProcess[indexCopy],j->argv[i]);
            indexCopy++;
        }
    }

    if(fileRedirectCount == 0){

        execvp(j->argv[0],j->argv);

        //freeProcess(argvCopy);
        //freeProcess(j->leftProcess);
    }
    else {

        //argvCopy[indexCopy] = NULL;
        j->leftProcess[indexCopy] = NULL;
        execvp(j->argv[0], j->leftProcess);

        //freeProcess(j->leftProcess);
    }
}

int doFileRedirectLeftWithPipe(job* j, int startIndex, int endIndex){

    int fileRedirectCount = 0;
    int fileOutputRedirectCount = 0;

    int fileNotExist = 0;

    for(int i = startIndex; i < endIndex; i++){

        if(strcmp(j->argv[i],"<") == 0){

            fileNotExist = inputRedirect(j->argv,i);
            if(fileNotExist == 0){
                i++;
            }
            fileRedirectCount++;
        }
    }

    int indexCopy = 0;
    //int fileNotExist = 0;
    for(int i = startIndex; i < endIndex; i++){

        if(strcmp(j->argv[i],">") == 0){

            outputRedirect(j->argv,i,fileNotExist);
            fileRedirectCount++;
            fileOutputRedirectCount++;
            i++;
        }
        
        else if(strcmp(j->argv[i],"<") == 0){

            //fileNotExist = inputRedirect(j->argv,i);
            if(fileNotExist == 0){
                i++;
            }
            fileRedirectCount++;
        }
        
        else if(strcmp(j->argv[i],"2>") == 0){

            errorRedirect(j->argv,i, fileNotExist);
            fileRedirectCount++;
            i++;
        }
        else {
            strcpy(j->leftProcess[indexCopy],j->argv[i]);
            indexCopy++;
        }
    }
    j->leftProcess[indexCopy] = NULL;
    return fileOutputRedirectCount;
}

int doFileRedirectRightWithPipe(job* j, int startIndex, int endIndex){

    int fileRedirectCount = 0;
    int fileInputRedirectCount = 0;

    int fileNotExist = 0;
    for(int i = startIndex; i < endIndex; i++){

        if(strcmp(j->argv[i],"<") == 0){

            fileNotExist = inputRedirect(j->argv,i);
            if(fileNotExist == 0){
                fileInputRedirectCount++;
                i++;
            }
            fileRedirectCount++;
        }
    }

    int indexCopy = 0;
    //int fileNotExist = 0;
    for(int i = startIndex; i < endIndex; i++){

        if(strcmp(j->argv[i],">") == 0){

            outputRedirect(j->argv,i,fileNotExist);
            fileRedirectCount++;
            i++;
        }
        
        else if(strcmp(j->argv[i],"<") == 0){

            //fileNotExist = inputRedirect(j->argv,i);
            if(fileNotExist == 0){
                fileInputRedirectCount++;
                i++;
            }
            fileRedirectCount++;
        }
        
        else if(strcmp(j->argv[i],"2>") == 0){

            errorRedirect(j->argv,i, fileNotExist);
            fileRedirectCount++;
            i++;
        }
        else {
            strcpy(j->rightProcess[indexCopy],j->argv[i]);
            indexCopy++;
        }
    }
    j->rightProcess[indexCopy] = NULL;
    return fileInputRedirectCount;
}

void executeWithPipe(job* j, int pipeFound, int arglength, int pipefd[]){    

    pipe(pipefd);
    j->leftProcess = malloc(sizeof(char*)* maxTokensPerLine);
    allocateProcess(j->leftProcess);
    j->rightProcess = malloc(sizeof(char*) * maxTokensPerLine);
    allocateProcess(j->rightProcess);

    j->pgid = fork();
    if(j->pgid == 0){

        int fileOutputRedirectCounter = doFileRedirectLeftWithPipe(j,0,pipeFound);
        if(fileOutputRedirectCounter == 0){

            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
        }
        execvp(j->leftProcess[0],j->leftProcess);
        //freeProcess(j->leftProcess);
    }

    j->pgid = fork();
    if(j->pgid == 0){

        int fileInputRedirectCounter = doFileRedirectRightWithPipe(j,pipeFound+1,arglength);
        if(fileInputRedirectCounter == 0){

            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
        }
        execvp(j->rightProcess[0], j->rightProcess);
        //freeProcess(j->rightProcess);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    wait(&(j->status));
    wait(&(j->status));
}

void processCommand(job* j, int pipeFound, int arglength, int pipefd[]){

    //if no pipe,just execute normally
    if(pipeFound == -1){

        j->pgid = fork();
        if(j->pgid == 0){

            doFileRedirectionNoPipe(j,arglength); 
        }
        wait(&(j->status));
    }
    //if pipe
    else {

        executeWithPipe(j,pipeFound,arglength,pipefd);
    }
    
}

void setSignalsToIgnore(){

    signal(SIGINT, SIG_IGN);
    //signal(SIGTSTP, SIG_IGN);
}

void removeJob(job** head, int jobNum){

    if(*head == NULL){
        return;
    }
    else {

        if(((*head)->jobNumber) == jobNum){

            *head = ((*head)->next);
            //free(head);
        }
        else {
            job* tempHead = *head;
            //job* next = head->next;
            job* prev;
            while(tempHead != NULL && tempHead->jobNumber != jobNum){

                prev = tempHead;
                tempHead = tempHead->next;
            }

            prev->next = tempHead->next;
            free(tempHead);
        }
    }
}

void displayJobs(job** head){


    job* tempHead = *head;
    while(tempHead != NULL){

        if(tempHead->state == RUNNING){

            printf("[%d]+  RUNNING      ",tempHead->jobNumber);
            int i = 0;
            while(tempHead->argv[i] != NULL){

                printf("%s ",tempHead->argv[i]);
                if(tempHead->argv[i+1] == NULL){
                    printf("\n");
                }
                i++;
            }
            //printf("IT REACHES HERE\n");
        }
        
        tempHead = tempHead->next;
    }
}

int main(){

    //setSignalsToIgnore();

    job* head = malloc(sizeof(job));

    char* input;
    pid_t cpid;

    int pipefd[2];

    while(1){

        //create new process group
        job* j = malloc(sizeof(job));
        j->argv = malloc(sizeof(char*) * maxTokensPerLine);
        allocateProcess(j->argv);
        
        input = readline("# ");

        //parse the input into tokens
        int arglength = parseCommand(input, j);
        j->argv[arglength] = NULL;

        //see if the command has a pipe
        int pipeFound = checkForPipe(j->argv);

        j->state = RUNNING;
        j->isInBackground = 0;
        j->jobNumber = jobN;
        jobN++;
        addJob(&head, j);
        

        //printf("IT REACHES HERE\n");
        if(strcmp(j->argv[0],"jobs") == 0){

            //printf("IT REACHES HERE\n");
            displayJobs(&head);
        }
        else {

            //do the command
            processCommand(j,pipeFound,arglength, pipefd);
        }

        

        //once process is done,free the process
        //freeProcess(j->argv);
        //free(j->argv);

        removeJob(&head,j->jobNumber);

        
        //free(j);

        //removeJob(head,j->jobNumber);
        
    }
}        
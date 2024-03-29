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


#define RUNNING 0
#define STOPPED 1
#define DONE 2

int foregroundJobNumber = -1;

int status;

struct processGroup {

    pid_t pgid;
    int jobNumber;
    int state;
    char** argv;

    int pipeFound;

    int leftProcessPID;
    int leftProcessState;
    char** leftProcess;

    int rightProcessPID;
    int rightProcessState;
    char** rightProcess;

    int isInBackground;

    int hasAddedToList;

    int noPipeFlag;
    int leftPipeFlag;
    int rightPipeFlag;

    struct processGroup* next;
};

typedef struct processGroup job;

job* head = NULL;
job* foregroundJob = NULL;

int shellTerminalPID = -1;
int shellTerminal = -212;

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

//add job at tail
void addJob(job* j){

    if(head == NULL){

        head = j;
        j->next = NULL;
    }
    else {

        job* tempHead = head;
        job* prev;
        while(tempHead != NULL){

            prev = tempHead;
            tempHead = tempHead->next;
        }
        prev->next = j;
        j->next = NULL;
    }
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
    for(int i = 0; i < arglength; i++){

        if(strcmp(j->argv[i],">") == 0){

            outputRedirect(j->argv,i,fileNotExist);
            fileRedirectCount++;
            i++;
        }
        
        else if(strcmp(j->argv[i],"<") == 0){

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

    if(fileRedirectCount == 0){

        int errorCommand = execvp(j->argv[0],j->argv);
        if(errorCommand == -1){

            kill(getpgid(getpid()),SIGINT);          
        }

    }
    else {

        j->leftProcess[indexCopy] = NULL;
        execvp(j->argv[0], j->leftProcess);

    }
}

int doFileRedirectLeftWithPipe(job* j, int startIndex){

    int fileRedirectCount = 0;
    int fileOutputRedirectCount = 0;

    int fileNotExist = 0;

    for(int i = startIndex; i < j->pipeFound; i++){

        if(strcmp(j->argv[i],"<") == 0){

            fileNotExist = inputRedirect(j->argv,i);
            if(fileNotExist == 0){
                i++;
            }
            fileRedirectCount++;
        }
    }

    int indexCopy = 0;
    for(int i = startIndex; i < j->pipeFound; i++){

        if(strcmp(j->argv[i],">") == 0){

            outputRedirect(j->argv,i,fileNotExist);
            fileRedirectCount++;
            fileOutputRedirectCount++;
            i++;
        }
        
        else if(strcmp(j->argv[i],"<") == 0){

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

int doFileRedirectRightWithPipe(job* j, int endIndex){

    int fileRedirectCount = 0;
    int fileInputRedirectCount = 0;

    int fileNotExist = 0;
    for(int i = j->pipeFound + 1; i < endIndex; i++){

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
    for(int i = j->pipeFound + 1; i < endIndex; i++){

        if(strcmp(j->argv[i],">") == 0){

            outputRedirect(j->argv,i,fileNotExist);
            fileRedirectCount++;
            i++;
        }
        
        else if(strcmp(j->argv[i],"<") == 0){

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

int calculateJobNumber(){

    if(head == NULL){
        return 1;
    }
    else {

        int result = -212;
        job* tempHead = head;
        while(tempHead != NULL){

            if(result < tempHead->jobNumber){
                
                result = tempHead->jobNumber;
            }
            tempHead = tempHead->next;
        }
        return (result + 1);
    }    
}

void executeWithPipe(job* j, int arglength, int pipefd[]){    

    pipe(pipefd);
    j->leftProcess = malloc(sizeof(char*)* maxTokensPerLine);
    allocateProcess(j->leftProcess);
    j->rightProcess = malloc(sizeof(char*) * maxTokensPerLine);
    allocateProcess(j->rightProcess);
    

    pid_t leftPID = fork();
    j->leftProcessPID = leftPID;
    j->leftProcessState = RUNNING;
    j->pgid = j->leftProcessPID;

    if(j->leftProcessPID == 0){

        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);
        signal(SIGCONT,SIG_DFL);
        signal(SIGTTIN,SIG_DFL);


        int fileOutputRedirectCounter = doFileRedirectLeftWithPipe(j,0);
        if(fileOutputRedirectCounter == 0){

            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
        }
        execvp(j->leftProcess[0],j->leftProcess);
        
    }

    pid_t rightPID = fork();
    j->rightProcessPID = rightPID;
    j->rightProcessState = RUNNING;

    if(j->rightProcessPID == 0){
        
        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);
        signal(SIGCONT,SIG_DFL);
        signal(SIGTTIN,SIG_DFL);

        int fileInputRedirectCounter = doFileRedirectRightWithPipe(j,arglength);
        if(fileInputRedirectCounter == 0){

            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
        }
        execvp(j->rightProcess[0], j->rightProcess);   
    }
    
    close(pipefd[0]);
    close(pipefd[1]);

    setpgid(j->leftProcessPID,0);
    setpgid(j->rightProcessPID,j->leftProcessPID);
    if(j->isInBackground == 0){

        tcsetpgrp(shellTerminal,j->pgid);
        while(1){

            if(j->leftPipeFlag == 1 && j->rightPipeFlag == 1){
                break;
            }
        }
    }
    
}

void setSignalsToIgnore(){

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCONT,SIG_IGN);
}

void removeJob(int jobNum){

    if(head == NULL){
        return;
    }
    else {

        if((head->jobNumber) == jobNum){

            head = head->next;
        }
        else {
            job* tempHead = head;
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

void displayJobs(){

    if(head == NULL){
        return;
    }

    job* tempHeadOne = head;
    job* findMostRecent;
    while (tempHeadOne != NULL)
    {
        if(tempHeadOne->state == RUNNING || tempHeadOne->state == STOPPED){

            findMostRecent = tempHeadOne;
        }
        tempHeadOne = tempHeadOne->next;
    }

    int mostRecentPGID = findMostRecent->pgid;

    job*tempHeadTwo = head;
    while(tempHeadTwo != NULL){

        printf("[%d]",tempHeadTwo->jobNumber);
        if(tempHeadTwo->pgid == mostRecentPGID){

            printf("+ ");
        }
        else {

            printf("- ");
        }

        if(tempHeadTwo->state == RUNNING){

            printf("Running     ");
        }
        else if(tempHeadTwo->state == STOPPED){

            printf("Stopped     ");
        }
        else if(tempHeadTwo->state == DONE){

            printf("Done        ");
        }

        int i = 0;
        while(tempHeadTwo->argv[i] != NULL){

            printf("%s ",tempHeadTwo->argv[i]);
            if(tempHeadTwo->argv[i+1] == NULL){

                if(tempHeadTwo->isInBackground == 1){

                    printf("&\n");
                }
                else {

                    printf("\n");
                }
            }
            i++;
        }
        tempHeadTwo = tempHeadTwo->next;
    }
}


job* searchForPID(pid_t pid){


    job* temp = head;
    while(temp != NULL && (temp->pgid != pid)){
        temp = temp->next;
    }
    
    if(temp == NULL){

        return NULL;
    }
    
    return temp;
}

job* searchForLeftPID(pid_t pid){

    job* temp = head;
    while(temp != NULL && (temp->leftProcessPID != pid)){

        temp = temp->next;
    }

    if(temp == NULL){

        return NULL;
    }

    return temp;
}

job* searchForRightPID(pid_t pid){

    job* temp = head;
    while(temp != NULL && (temp->rightProcessPID != pid)){

        temp = temp->next;
    }

    if(temp == NULL){

        return NULL;
    }

    return temp;
}

job* search(pid_t pid){

    if(foregroundJob != NULL){

        if(foregroundJob->leftProcessPID == pid){

        return foregroundJob;
    }
    else if(foregroundJob->pipeFound != -1){

            if(foregroundJob->rightProcessPID == pid){

                return foregroundJob;
            }
        }
    }
      
    job* temp = head;
    while(temp != NULL){
        
        if(temp->leftProcessPID == pid){

            return temp;
        }
        else if(temp->pipeFound != -1){

            if(temp->rightProcessPID == pid){

                return temp;
            }
        }
        temp = temp->next;
    }
    return NULL;
}

void sigCHLDHandler(int signo){


    pid_t pid;
    while((pid = waitpid(-1,&(status),WNOHANG|WUNTRACED|WCONTINUED)) > 1){


        //terminated normally or terminated with signal
        if(WIFEXITED(status) || WIFSIGNALED(status)){

            //search for background job
            job* foundJob = search(pid);
            if(foundJob != NULL){

                if(foundJob == foregroundJob){

                    if(foundJob->pipeFound == -1){

                        if(foundJob->leftProcessPID == pid){

                            foundJob->state = DONE;
                            foundJob->noPipeFlag = 1;
                            tcsetpgrp(shellTerminal,getpgid(getpid()));
                            
                        }
                    }
                    else {

                        if(foundJob->leftProcessPID == pid){

                            foundJob->leftPipeFlag = 1;
                        }
                        if(foundJob->rightProcessPID == pid){

                            foundJob->rightPipeFlag = 1;
                        }

                        if(foundJob->leftPipeFlag == 1 && foundJob->rightPipeFlag == 1){

                            foundJob->state = DONE;
                            tcsetpgrp(shellTerminal,getpgid(getpid()));
                        }
                    }
                }
                else {

                    if(foundJob->pipeFound == -1){

                        foundJob->state = DONE;
                    }
                    else {

                        if(foundJob->leftProcessPID == pid){

                            foundJob->leftProcessState = DONE;
                        }
                        if(foundJob->rightProcessPID == pid){

                            foundJob->rightProcessState = DONE;
                        }

                        if(foundJob->leftProcessState == DONE && foundJob->rightProcessState == DONE){

                            foundJob->state = DONE;
                        }
                    }
                }
            }
              
        }
        //stopped by signal(CTRL-Z)
        if(WIFSTOPPED(status)){

            if(foregroundJob != NULL){

                //no pipe
                if(foregroundJob->pipeFound == -1){

                    foregroundJob->state = STOPPED;
                    foregroundJob->noPipeFlag = 1;
                    if(foregroundJob->hasAddedToList == 0){

                        addJob(foregroundJob);
                        foregroundJob->hasAddedToList = 1;
                    }
                    foregroundJob = NULL;
                    tcsetpgrp(shellTerminal,getpgid(getpid()));
                }
                //there is pipe
                else {

                    foregroundJob->state = STOPPED;
                    foregroundJob->leftPipeFlag = 1;
                    foregroundJob->rightPipeFlag = 1;
                    if(foregroundJob->hasAddedToList == 0){

                        addJob(foregroundJob);
                        foregroundJob->hasAddedToList = 1;
                    }
                    foregroundJob = NULL;
                    tcsetpgrp(shellTerminal,getpgid(getpid()));
                }
            }
        }    
    }
}

void processCommand(job* j, int arglength, int pipefd[]){

    //if no pipe,just execute normally
    if(j->pipeFound == -1){

        pid_t pid = fork();
        j->pgid = pid;
        j->leftProcessPID = j->pgid;
        j->rightProcessPID = -1;
        
        if(pid == 0){
            //child process

            setpgid(0,0);


                signal(SIGINT,SIG_DFL);
                signal(SIGTSTP,SIG_DFL);
                signal(SIGCONT,SIG_DFL);
                signal(SIGTTIN,SIG_DFL);

            doFileRedirectionNoPipe(j,arglength);
            
        }
        else {
            //parent process

            if(j->isInBackground == 0){

                signal(SIGTTOU, SIG_IGN);
                tcsetpgrp(shellTerminal,j->pgid);

                while(1)
                {
                    if(j->noPipeFlag == 1)
                    {
                        break;
                    }
                }
            }   
        }
    }
    //if pipe
    else {

        executeWithPipe(j,arglength,pipefd);
    }   
}



void doForeground(){

    //if no jobs,then can't do fg
    if(head == NULL){
        printf("yash: fg: current: no such job\n");
    }
    //first calculate job to send to fg
    else {

        job* temphead = head;
        job* sendToFG = NULL;
        while(temphead != NULL){

            if((temphead->state == STOPPED && temphead->isInBackground == 0) || (temphead->state == RUNNING && temphead->isInBackground == 1)){

                sendToFG = temphead;
            }
            temphead = temphead->next;
        }

        if(sendToFG == NULL){

            printf("No processes to bring to foreground\n");
        }
        else {

            //no pipe
        if(sendToFG->pipeFound == -1){

            int i = 0;
            while(sendToFG->argv[i] != NULL){

                if(strcmp(sendToFG->argv[i],"&") != 0){

                    printf("%s ",sendToFG->argv[i]);
                }

                if(sendToFG->argv[i+1] == NULL){

                    printf("\n");
                }
                i++;
            }
            sendToFG->isInBackground = 0;
            foregroundJob = sendToFG;

            kill(sendToFG->pgid,SIGCONT);
            signal(SIGTTOU,SIG_IGN);
            tcsetpgrp(shellTerminal,sendToFG->pgid);
            sendToFG->noPipeFlag = 0;
            while(1){

                if(sendToFG->noPipeFlag == 1){
                    break;
                }
            }
        }
        //if command has a pipe
        else {
                int i = 0;
                while(sendToFG->argv[i] != NULL){

                    if(strcmp(sendToFG->argv[i],"&") != 0){

                        printf("%s ",sendToFG->argv[i]);
                    }

                    if(sendToFG->argv[i+1] == NULL){

                        printf("\n");
                    }
                    i++;
                }

                sendToFG->isInBackground = 0;
                sendToFG->state = RUNNING;
                foregroundJob = sendToFG;
                kill((-1) * (sendToFG->pgid),SIGCONT);
                //signal(SIGTTOU,SIG_IGN);
                tcsetpgrp(shellTerminal,sendToFG->pgid);
                sendToFG->leftPipeFlag = 0;
                sendToFG->rightPipeFlag = 0;
                while(1){

                    if(sendToFG->leftPipeFlag == 1 && sendToFG->rightPipeFlag == 1){
                        break;
                    }
                }
                        
            }

        }   
    }
    
}

void doBackground(){

    if(head == NULL){

        printf("yash: bg: current: no such job\n");
    }
    else {

        job* temphead = head;
        job* sendToBg = NULL;

        while(temphead != NULL){

            if(temphead->isInBackground == 0 && temphead->state == STOPPED){

                sendToBg = temphead;
            }
            temphead = temphead->next;
        }

        if(sendToBg == NULL){

            printf("No processes to send to background\n");
        }
        else {

            //if no pipe in command
            if(sendToBg->pipeFound == -1){

                printf("[%d]+ Running        ",sendToBg->jobNumber);

                int i = 0;
                while(sendToBg->argv[i] != NULL){

                    printf("%s ",sendToBg->argv[i]);

                    if(sendToBg->argv[i+1] == NULL){

                        printf("&\n");
                    }
                    i++;
                }
                sendToBg->state = RUNNING;
                sendToBg->isInBackground = 1;
                kill(sendToBg->pgid,SIGCONT);
            }
            else {

                printf("[%d]+ Running        ",sendToBg->jobNumber);

                int i = 0;
                while(sendToBg->argv[i] != NULL){

                    printf("%s ",sendToBg->argv[i]);

                    if(sendToBg->argv[i+1] == NULL){

                        printf("&\n");
                    }
                    i++;
                }

                sendToBg->state = RUNNING;
                sendToBg->isInBackground = 1;
                kill((-1) * (sendToBg->pgid),SIGCONT);
            }
        }  
    }
}

void displayDoneJobs(char* input){

    if(head == NULL){
        return;
    }
    else {

        if(strcmp(input,"jobs") == 0){

            job* tempHeadOne = head;
            job* findMostRecent = NULL;
            while(tempHeadOne != NULL){

                if(tempHeadOne->state == RUNNING || tempHeadOne->state == STOPPED){

                    findMostRecent = tempHeadOne;
                }
                tempHeadOne = tempHeadOne->next;
            }
            if(findMostRecent != NULL){

                job* tempHeadTwo = head;
                while(tempHeadTwo != NULL){

                    if(tempHeadTwo->state == DONE && tempHeadTwo->isInBackground == 1){

                        printf("[%d]- Done        ",tempHeadTwo->jobNumber);
                        int i = 0;
                        while(tempHeadTwo->argv[i] != NULL){

                            printf("%s ",tempHeadTwo->argv[i]);
                            if(tempHeadTwo->argv[i+1] == NULL){

                                if(tempHeadTwo->isInBackground == 1){

                                    printf("&\n");
                                }
                                else {

                                    printf("\n");
                                }
                            }
                            i++;
                        }
                    }
                    tempHeadTwo = tempHeadTwo->next;
                }
            }
            else {

                job* tempHeadThree = head;
                int plusDoneFlag = 0;
                while(tempHeadThree != NULL){

                    if(tempHeadThree->state == DONE && tempHeadThree->isInBackground == 1){

                        printf("[%d]",tempHeadThree->jobNumber);
                        if(plusDoneFlag == 0){

                            printf("+ ");
                            plusDoneFlag = 1;
                        }
                        else {

                            printf("- ");
                        }
                        printf("Done        ");
                        int i = 0;
                        while(tempHeadThree->argv[i] != NULL){

                            printf("%s ",tempHeadThree->argv[i]);
                            if(tempHeadThree->argv[i+1] == NULL){

                                if(tempHeadThree->isInBackground == 1){

                                    printf("&\n");
                                }
                                else {

                                    printf("\n");
                                }
                            }
                            i++;
                        }
                    }
                    tempHeadThree = tempHeadThree->next;
                }
            }
        }
        else {

            int plusDoneFlag = 0;
            job* tempHeadFour = head;
            while(tempHeadFour != NULL){

                if(tempHeadFour->state == DONE && tempHeadFour->isInBackground == 1){

                    printf("[%d]",tempHeadFour->jobNumber);
                    if(plusDoneFlag == 0){

                        printf("+ ");
                        plusDoneFlag = 1;
                    }
                    else {

                        printf("- ");
                    }
                    printf("Done        ");
                    int i = 0;
                    while(tempHeadFour->argv[i] != NULL){

                        printf("%s ",tempHeadFour->argv[i]);
                        if(tempHeadFour->argv[i+1] == NULL){

                            if(tempHeadFour->isInBackground == 1){

                                printf("&\n");
                            }
                            else {

                                printf("\n");
                            }
                        }
                        i++;
                    }
                }
                tempHeadFour = tempHeadFour->next;
            }
        }
    }
    
    
}

void removeDoneJobs(){

    if(head == NULL){

        return;
    }
    else {

        job* tempHead = head;
        job* prev;
        //if DONE is at head of list
        while(tempHead != NULL && tempHead->state == DONE){

            head = tempHead->next;
            free(tempHead);
            tempHead = head;
        }

        while(tempHead != NULL){

            while(tempHead != NULL && tempHead->state != DONE){

                prev = tempHead;
                tempHead = tempHead->next;
            }

            if(tempHead == NULL){
                return;
            }

            prev->next = tempHead->next;
            free(tempHead);
            tempHead = prev->next;
        }
    }
}

int main(){


    setSignalsToIgnore();

    signal(SIGCHLD,sigCHLDHandler);

    
    char* input;
    pid_t cpid;

    int pipefd[2];

    shellTerminalPID = getpgid(getpid());
    setpgid(shellTerminalPID,shellTerminalPID);

    shellTerminal = STDIN_FILENO;

    tcsetpgrp(shellTerminal,getpgid(getpid()));

    while(input = readline("# ")){


        foregroundJobNumber = -1;

        //create new process group
        job* j = malloc(sizeof(job));
        j->argv = malloc(sizeof(char*) * maxTokensPerLine);
        allocateProcess(j->argv);
        j->hasAddedToList = 0;
        j->noPipeFlag = 0;
        j->leftPipeFlag = 0;
        j->rightPipeFlag = 0;
        //if CTRL D is pressed,exit shell
        if(input == NULL){  
            job* killHead = head;
            while(killHead != NULL){
                
                kill((-1)*(killHead->pgid),SIGKILL);
                killHead = killHead->next;
            }
            kill(getpgid(getpid()),SIGKILL);
        }
        
        //parse the input into tokens
        int arglength = parseCommand(input, j);
        displayDoneJobs(j->argv[0]);
        removeDoneJobs();
        //arglength is 0 means just pressed enter key with no command
        if(arglength != 0){

            j->argv[arglength] = NULL;

            //see if the command has a pipe
            j->pipeFound = checkForPipe(j->argv);

            if(strcmp(j->argv[0],"jobs") == 0){

                displayJobs();
            }
            else if(strcmp(j->argv[0],"fg") == 0){
                //fg brings the most recent background/stopped process to running in the foreground
                doForeground();
            }
            else if(strcmp(j->argv[0],"bg") == 0){
                //bg brings the stopped foreground process to the background
                doBackground();
            }
            else {
                j->state = RUNNING;
                j->jobNumber = calculateJobNumber();
                
                //do the command in foreground
                if(strcmp(j->argv[arglength-1],"&") != 0){

                    j->isInBackground = 0;
                    foregroundJob = j;
                    processCommand(j, arglength, pipefd);
                }
                //do command in background
                else {
                    j->isInBackground = 1;
                    j->argv[arglength-1] = NULL;
                    if(j->hasAddedToList == 0){

                        addJob(j);
                        j->hasAddedToList = 1;
                    }
                    processCommand(j,arglength-1, pipefd);
                }
                
            }
        }                
    }
}        

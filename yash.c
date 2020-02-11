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

int noPipeFlag = 0;
int leftPipeFlag = 0;
int rightPipeFlag = 0;
int pipeFound = -212;


//enum STATUS {RUNNING,STOPPED, DONE};

//int pipeFound = 0;

//int jobN = 1;
//int mostRecentJobNumber = 1;
int foregroundJobNumber = -1;

int status;

struct processGroup {

    pid_t pgid;
    int jobNumber;
    int state;
    //int status;
    char** argv;

    int leftProcessPID;
    int leftProcessState;
    char** leftProcess;

    int rightProcessPID;
    int rightProcessState;
    char** rightProcess;

    int isInBackground;

    //int jobNumCalculated;
    int hasAddedToList;

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

//add job at tail
void addJob(job* j){

    if(head == NULL){

        head = j;
        j->next = NULL;
    }
    else {

        //traverse to end of list
        job* tempHead = head;
        job* prev;
        while(tempHead != NULL){

            prev = tempHead;
            tempHead = tempHead->next;
        }
        prev->next = j;
        j->next = NULL;
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

        //printf("%d\n",getpgid(getpid()));
        int errorCommand = execvp(j->argv[0],j->argv);
        //printf("%d\n",errorCommand);
        if(errorCommand == -1){

            //printf("ERROR COMMAND\n");
            kill(getpgid(getpid()),SIGINT);
            //tcsetpgrp(shellTerminal,j->pgid);            
        }

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

int doFileRedirectLeftWithPipe(job* j, int startIndex){

    int fileRedirectCount = 0;
    int fileOutputRedirectCount = 0;

    int fileNotExist = 0;

    for(int i = startIndex; i < pipeFound; i++){

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
    for(int i = startIndex; i < pipeFound; i++){

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

int doFileRedirectRightWithPipe(job* j, int endIndex){

    int fileRedirectCount = 0;
    int fileInputRedirectCount = 0;

    int fileNotExist = 0;
    for(int i = pipeFound + 1; i < endIndex; i++){

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
    for(int i = pipeFound + 1; i < endIndex; i++){

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
        //child of left pipe process

        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);
        signal(SIGCONT,SIG_DFL);
        signal(SIGTTIN,SIG_DFL);

        //isShell = 212;

        int fileOutputRedirectCounter = doFileRedirectLeftWithPipe(j,0);
        //printf("%d\n",fileOutputRedirectCounter);
        if(fileOutputRedirectCounter == 0){

            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
        }
        execvp(j->leftProcess[0],j->leftProcess);
        
        //freeProcess(j->leftProcess);
    }
    /*
    else {
        //parent of left pipe process
        if(j->isInBackground == 0){
            signal(SIGTTOU,SIG_IGN);
            tcsetpgrp(shellTerminal,j->leftProcessPID);
            while(1){

                if(leftPipeFlag == 1){
                    // tcsetpgrp(shellTerminal,getpgid(getpid()));
                    break;
                }
            }
        }

    }
    */

    pid_t rightPID = fork();
    j->rightProcessPID = rightPID;
    j->rightProcessState = RUNNING;

    if(j->rightProcessPID == 0){
        //child process of the right pipe

        //isShell = 212;
        
        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);
        signal(SIGCONT,SIG_DFL);
        signal(SIGTTIN,SIG_DFL);

        int fileInputRedirectCounter = doFileRedirectRightWithPipe(j,arglength);
        //printf("%d\n",fileInputRedirectCounter);
        if(fileInputRedirectCounter == 0){

            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
        }
        execvp(j->rightProcess[0], j->rightProcess);   
    }
    /*
    else {
        //parent process of the right pipe
        if(j->isInBackground == 0){

            signal(SIGTTOU,SIG_IGN);
            tcsetpgrp(shellTerminal,j->rightProcessPID);
            while(1){

                if(rightPipeFlag == 1){
                    break;
                }
            }
        }
    }
    */
    close(pipefd[0]);
    close(pipefd[1]);

    setpgid(j->leftProcessPID,0);
    setpgid(j->rightProcessPID,j->leftProcessPID);
    if(j->isInBackground == 0){

        //signal(SIGTTOU,SIG_IGN);
        tcsetpgrp(shellTerminal,j->pgid);
        while(1){

            if(leftPipeFlag == 1 && rightPipeFlag == 1){
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
            //free(head);
        }
        else {
            job* tempHead = head;
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

void displayJobs(){

    if(head == NULL){
        //printf("IT REACHES HERE\n");
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

            printf("RUNNING     ");
        }
        else if(tempHeadTwo->state == STOPPED){

            printf("STOPPED     ");
        }
        else if(tempHeadTwo->state == DONE){

            printf("DONE        ");
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

/*
job* findMostRecentBackgroundProcess(job** findJob){

    job* tempFindJob = *findJob;
    while(tempFindJob->jobNumber != mostRecentJobNumber){

        tempFindJob = tempFindJob->next;
    }
    return tempFindJob;
}
*/

/*
void sigINTHandler(int signo){

    if(foregroundJobNumber != -1){

        printf(" SIGINT signal called!\n");
        kill(foregroundJobNumber,SIGINT);
    }
}
*/
/*
void sigTSTPHandler(int signo){

    if(foregroundJobNumber != -1){

        printf(" SIGTSTP signal called!\n");
        kill(foregroundJobNumber,SIGTSTP);
    }
}
*/

/*
void sigCONTHandler(int signo){

    if(foregroundJobNumber != -1){

        printf(" SIGCONT signal called!\n");
        kill(foregroundJobNumber,SIGCONT);
    }
}
*/

job* searchForPID(pid_t pid){

    //printf("The pid I need to find is %d\n",pid);

    job* temp = head;
    //printf("The left process PID is %d\n",temp->leftProcessPID);
    //printf("The right process PID is %d\n",temp->rightProcessPID);
    //printf("pid I am looking for is %d\n",pid);
    //exit(0);
    while(temp != NULL && (temp->pgid != pid)){
        //printf("%d\n",temp->pgid);
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

void sigCHLDHandler(int signo){

    //WUNTRACED:child stopped
    //WCONTINUED:stopped child transitions to running
    //WNOHANG:NON-BLOCKING;return immediately if no child exited
    //get pid,search thru linked list to find it,then change state
    pid_t pid;
    while((pid = waitpid(-1,&(status),WNOHANG|WUNTRACED|WCONTINUED)) > 1){

        //printf("The pid I am looking for is %d\n",pid);

        //terminated normally
        if(WIFEXITED(status)){

            //printf("WIFEXITTED\n");

            //no pipe in command
            if(pipeFound == -1){

                //it was a background process
                if(foregroundJob == NULL){

                    job* foundJob = searchForPID(pid);
                    //printf("BACKGROUND PROCESS DONE\n");
                    foundJob->state = DONE;

                }
                //it was a foreground process
                else {

                    noPipeFlag = 1;
                    foregroundJob->state = DONE;
                    tcsetpgrp(shellTerminal,getpgid(getpid()));
                }
            }
            //there was a pipe in command
            else {

                //it was a foreground process
                if(foregroundJob != NULL){

                    //if left process finished,set left pipe flag
                    if(foregroundJob->leftProcessPID == pid){

                        leftPipeFlag = 1;
                    }
                    //if right process finished,set right pipe flag
                    if(foregroundJob->rightProcessPID == pid){
 
                        rightPipeFlag = 1;
                    }
                    //if both flags are set,then give control back to shell
                    if(leftPipeFlag == 1 && rightPipeFlag == 1){

                        tcsetpgrp(shellTerminal,getpgid(getpid()));
                    }
                }
                //it was a background process
                else {

                    job* foundJob = searchForLeftPID(pid);
                    //printf("left process PID is %d\n",foundJob->leftProcessPID);
                    if(foundJob != NULL){

                        foundJob->leftProcessState = DONE;
                    }
                    else {

                        foundJob = searchForRightPID(pid);
                        if(foundJob != NULL){

                            foundJob->rightProcessState = DONE;
                        }
                    }

                    if(foundJob->leftProcessState == DONE && foundJob->rightProcessState == DONE){

                        //printf("PIPE DONE\n");
                        foundJob->state = DONE;
                        
                    }
                    //printf("IT REACHES HERE\n");
                    //printf("right process PID is %d\n",foundJob->rightProcessPID);
                    
                    //printf("IT REACHES HERE\n");
                    //exit(0);
                    /*
                    if(foundJob->leftProcessState == DONE && foundJobRight->rightProcessState == DONE){

                        foundJob->state = DONE;
                    }
                    */
                }
            }
               
        }
        //stopped with CTRL-Z
        if(WIFSTOPPED(status)){

            //no pipe in command
            if(pipeFound == -1){

                //it was a background process
                if(foregroundJob == NULL){

                    job* foundJob = searchForPID(pid);
                    foundJob->state = STOPPED;
                }
                //it was a foreground process
                else {

                    noPipeFlag = 1;
                    foregroundJob->state = STOPPED;
                    if(foregroundJob->hasAddedToList == 0){

                        addJob(foregroundJob);
                        foregroundJob->hasAddedToList = 1;
                    }
                    //printf("ADDED STOPPED FOREGROUND PROCESS\n");
                    tcsetpgrp(shellTerminal,getpgid(getpid()));
                    

                }
            }
            //there was a pipe in command
            else {

                leftPipeFlag = 1;
                rightPipeFlag = 1;
                foregroundJob->state = STOPPED;
                if(foregroundJob->hasAddedToList == 0){

                    addJob(foregroundJob);
                }
                
                tcsetpgrp(shellTerminal,getpgid(getpid()));
            }
        }
        //terminated
        if(WIFSIGNALED(status)){

            //no pipe in command
            if(pipeFound == -1){

                //it was a background process
                if(foregroundJob == NULL){

                    job* foundJob = searchForPID(pid);
                    foundJob->state = DONE;

                }
                //it was a foreground process
                else {

                    noPipeFlag = 1;
                    tcsetpgrp(shellTerminal,getpgid(getpid()));
                }
            }
            //there is a pipe in command
            else {

                leftPipeFlag = 1;
                rightPipeFlag = 1;
                tcsetpgrp(shellTerminal,getpgid(getpid()));
            }
            
        }    
    }
}

void processCommand(job* j, int arglength, int pipefd[]){

    //if no pipe,just execute normally
    if(pipeFound == -1){

        pid_t pid = fork();
        j->pgid = pid;
        
        if(pid == 0){
            //child process

            setpgid(0,0);

            //if(j->isInBackground == 0){

                signal(SIGINT,SIG_DFL);
                signal(SIGTSTP,SIG_DFL);
                signal(SIGCONT,SIG_DFL);
                signal(SIGTTIN,SIG_DFL);
            //}

            doFileRedirectionNoPipe(j,arglength);
            
        }
        else {
            //parent process

            if(j->isInBackground == 0){

                signal(SIGTTOU, SIG_IGN);
                tcsetpgrp(shellTerminal,j->pgid);

                while(1)
                {
                    //printf("%d\n",flag);
                    if(noPipeFlag == 1)
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
        if(pipeFound == -1){

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
            //printf("%d\n",sendToFG->pgid);
            sendToFG->isInBackground = 0;
            foregroundJob = sendToFG;

            kill(sendToFG->pgid,SIGCONT);
            signal(SIGTTOU,SIG_IGN);
            tcsetpgrp(shellTerminal,sendToFG->pgid);
            noPipeFlag = 0;
            while(1){

                if(noPipeFlag == 1){
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
                kill(sendToFG->pgid,SIGCONT);
                //signal(SIGTTOU,SIG_IGN);
                tcsetpgrp(shellTerminal,sendToFG->pgid);
                leftPipeFlag = 0;
                rightPipeFlag = 0;
                while(1){

                    if(leftPipeFlag == 1 && rightPipeFlag == 1){
                        break;
                    }
                }
                        
            }

        }   
    }
    
}

void doBackground(){

    //printf("I AM IN BACKGROUND\n");
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
            if(pipeFound == -1){

                //sendToBg->jobNumber = calculateJobNumber();
                printf("[%d]+ RUNNING        ",sendToBg->jobNumber);

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
        }  
    }
}

void displayDoneJobs(){

    if(head == NULL){
        //printf("No jobs in list to display\n");
        return;
    }
    else {

        job* tempHeadOne = head;
        job* findMostRecent = NULL;
        while(tempHeadOne != NULL){

            if(tempHeadOne->state == RUNNING || tempHeadOne->state == STOPPED){

                findMostRecent = tempHeadOne;
            }
            tempHeadOne = tempHeadOne->next;
        }
        //if there are still running/stopped jobs
        if(findMostRecent != NULL){
            
           // printf("%s\n",findMostRecent->argv[0]);
            //printf("lol %p\n",findMostRecent);
            job* tempHeadTwo = head;

            while(tempHeadTwo != NULL){

                if(tempHeadTwo->state == DONE && tempHeadTwo->isInBackground == 1){

                    printf("[%d]- DONE       ",tempHeadTwo->jobNumber);
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

            //printf("ONLY DONE JOBS\n");

            int plusDoneFlag = 0;
            job* tempHeadThree = head;
            while(tempHeadThree != NULL){

                if((tempHeadThree->state == DONE && tempHeadThree->isInBackground == 1)){

                    printf("[%d]",tempHeadThree->jobNumber);
                    if(plusDoneFlag == 0){

                        printf("+ ");
                        plusDoneFlag = 1;
                    }
                    else {

                        printf("- ");
                    }

                    printf("DONE        ");
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
    
}

void removeDoneJobs(){

    if(head == NULL){

        //printf("No jobs in list to remove\n");
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

    //signal(SIGINT,sigINTHandler);
    //signal(SIGTSTP,sigTSTPHandler);
    signal(SIGCHLD,sigCHLDHandler);

    //job* head = malloc(sizeof(job));
    
    char* input;
    pid_t cpid;

    int pipefd[2];

    shellTerminalPID = getpgid(getpid());
    setpgid(shellTerminalPID,shellTerminalPID);
    //printf("TerminalPID is %d\n",shellTerminalPID);

    shellTerminal = STDIN_FILENO;

    tcsetpgrp(shellTerminal,getpgid(getpid()));

    while(input = readline("# ")){

        foregroundJob = NULL;

        noPipeFlag = 0;
        leftPipeFlag = 0;
        rightPipeFlag = 0;

        displayDoneJobs();
        removeDoneJobs(); 

        foregroundJobNumber = -1;

        //create new process group
        job* j = malloc(sizeof(job));
        j->argv = malloc(sizeof(char*) * maxTokensPerLine);
        allocateProcess(j->argv);
        j->hasAddedToList = 0;
        //if CTRL D is pressed,exit shell
        if(input == NULL){  
            //printf("\n");
            kill(getpgid(getpid()),SIGKILL);
        }
        
        //parse the input into tokens
        int arglength = parseCommand(input, j);
        //arglength is 0 means just pressed enter key with no command
        if(arglength != 0){

            j->argv[arglength] = NULL;

            //see if the command has a pipe
            pipeFound = checkForPipe(j->argv);

            //printf("IT REACHES HERE\n");
            if(strcmp(j->argv[0],"jobs") == 0){

                //printf("IT REACHES HERE\n");
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
                //j->isInBackground = 0;
                /*
                if(j->jobNumCalculated == 0){

                    
                    j->jobNumCalculated = 1;
                }
                */
                j->jobNumber = calculateJobNumber();
                
                //do the command in foreground
                if(strcmp(j->argv[arglength-1],"&") != 0){

                    j->isInBackground = 0;
                    foregroundJob = j;
                    processCommand(j, arglength, pipefd);
                    //removeJob(j->jobNumber);
                }
                //do command in background
                else {
                    j->isInBackground = 1;
                    //printf("THIS PROCESS WILL BE RUN IN BACKGROUND\n");
                    j->argv[arglength-1] = NULL;
                    if(j->hasAddedToList == 0){

                        addJob(j);
                        j->hasAddedToList = 1;
                    }
                    processCommand(j,arglength-1, pipefd);
                    //printBackgroundJob(j);
                    //removeJob(j->jobNumber);
                }
                
            }
        }                
    }
}        

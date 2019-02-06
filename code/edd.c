/*************************************************************************
 edd.c daemon.  
 ETSD Data Director (edd) Connects source plugins to ETSD Time Series Database, and possibly to an external database(plugin)
 example source plugins are: ecmR (collecting energy data from an Brultech  ECM-1240), Shared Memory (SHM), named pipes, etc.
 External database could be an RRD database plugin, shared memory plugin, or other custom plugin

Copyright 2018 Peter VanDerWal 
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2.0 as published by
    the Free Software Foundation
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*********************************************************************************/

#define _GNU_SOURCE 

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>     /* va args */
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>		//usleep
#include <sys/stat.h>
//#include <fcntl.h>
#include <dlfcn.h>

//#include "ecmR.h"
#include "etsd.h"
#include "etsdSave.h"
#include "errorlog.h"

// #define AUX5_DC newData/256  // you can specify a data reduction formula if saving aux5 dc to 1/2 stream
// #define AUX5_DC (newData/4)-512 // different dara reduction scheme
// #define AUX5_DC newData>1024?newData-1024:0  // conditional statement

int8_t Interval;
volatile sig_atomic_t Reload;    // reload config file

uint8_t (*Check_ptr[4])(uint16_t timeOut, uint8_t interV);
uint32_t (*Read_ptr[4])(uint8_t chan, uint8_t interV);
uint8_t (*edbSave)(uint8_t elements, uint32_t dataArray[], uint8_t statArray[], uint8_t interval);
void (*ReadLock)(uint8_t lockData);

void sig_handler(int signum) {
    if (SIGUSR2!=signum){
        Log("Received termination signal, attempting to save ETSD block and exiting.\n");
        if (NULL != EtsdInfo.fileName)
            etsdCommit(Interval);
        exit(0);
    }
    Log("Received Reload signal.  Finishing current interval and then reloading configuration");
    Reload=1;
}

#ifdef DAEMON  
void savepid (char *file, pid_t pId){
    FILE *pID;
    pID = fopen (file, "w+");		
    fprintf(pID,"%d \n", pId);
    fclose(pID);
}
#endif

// returns sleep time
uint16_t readConfig( char *fileName, int8_t *sCnt, uint16_t *checkTime, uint8_t *saveEDB, char *xData) {
    FILE *fptr;
    char *ptr;
    void *handle[6]={NULL}; // pointer to dynamically loaded plugins
    char *srcCfg[6]={NULL}; // Array to hold plugin 'config' strings (all plugins)
    char *srcPrt[6]={NULL}; // Array to hold plugin port/filename/?? strings
    char **chanNames;       // Array of string pointers
    
   // uint8_t *xData=NULL;
    uint8_t (*edsSUp)(char *srcPort, char *Config, uint8_t Data);   // reusable Function pointer to Source plugin edsSetup()
    uint8_t (*edbSUp)(char *DBfileName,char *Config, uint8_t chanCnt, char **chanNames, uint8_t totInterv, uint8_t LogData); //*Func ext DB plugin edbSetup()
    uint16_t sleepTime=0;
    uint8_t lp, loadLabels=0;
    char configLine[260];
    int8_t srcCnt=-1;

    if ( NULL == (fptr = fopen(fileName, "r")) ) {
        Log("<3> Error! Can't open config file: %s\n", fileName);
        exit(1);
    }
    

    *saveEDB=0;
//printf("Config file %s\n", fileName);
    while ( fscanf(fptr,"%[^\n]\n", configLine) != EOF){
 //       printf("Config line %s\n", configLine);
        if (*configLine=='#' || *configLine=='\0')  // # = comment, skip to next line
            continue;
        ptr = strchr(configLine,':'); 
        ptr++;
        switch(*configLine){
        case 'T':                       // ETSD DB
            srcPrt[4] = (char*) malloc(strlen(ptr)+1);
            strcpy(srcPrt[4], ptr);
            break;
        case 'S':                       // Source(s)
            if ('N'==configLine[1] ){
                if(3 < ++srcCnt){
                    Log("\n\nError!  Config file contains too many data sources.  ETSD supports a maximum of 4 data sources.\nAborting, please fix config file.\n");
                    exit(1);
                }
                handle[srcCnt] = dlopen (ptr, RTLD_LAZY);
                checkTime[srcCnt]=0;
            } else if ('P'==configLine[1] ){                        
                srcPrt[srcCnt] = (char*) malloc(strlen(ptr)+1);
                strcpy(srcPrt[srcCnt], ptr);       
            } else if ('C'==configLine[1] ){   // even if not used, config file must have at least 1 char E.g.  SC:N
                srcCfg[srcCnt] = (char*) malloc(strlen(ptr)+1);
                strcpy(srcCfg[srcCnt], ptr);                
            } else if ('T'==configLine[1] ){ 
                checkTime[srcCnt] = atoi(ptr);
                sleepTime -= checkTime[srcCnt]/2;
            } 
            break;
        case 'D':                       // External DB
            if ('N'==configLine[1] ){           // plugin name`
                handle[5] = dlopen (ptr, RTLD_LAZY);
            }else if ('F'==configLine[1] ){     // DB FILENAME
                srcPrt[5] = (char*) malloc(strlen(ptr)+1);
                strcpy(srcPrt[5], ptr);
            }else if ('C'==configLine[1] ){     // additonal configuration line
                srcCfg[5] = (char*) malloc(strlen(ptr)+1);
                strcpy(srcCfg[5], ptr);                 
            }else if ('S'==configLine[1] ){
                if ('Y'==*(ptr)){
                    *saveEDB=1;
                }
            }else if ('L'==configLine[1] ){
                if ('Y'==*(ptr)){
                    loadLabels=1;
                }
            } 
            break;
        case 'L':                       // Log file
            LogSetup(ptr);
            break;
        case 'V':                       // Log Level
            LogLvl = atoi(ptr);
            break;  
        case 'X':                       // xData (Extra Data) plugin
            if ('N'==configLine[1] ){
                handle[4] = dlopen (ptr, RTLD_LAZY);
            }else if ('C'==configLine[1] ){
                srcCfg[4] = (char*) malloc(strlen(ptr)+1);
                strcpy(srcCfg[4], ptr);       
            }
            break;  
        }
    }
    fclose(fptr);
        
    if (srcCnt < 0){
        Log("\n\nError! Must specify at least one data source.\n");
        exit(1);
    }
    if(NULL==srcPrt[4]){
        Log("\n\nError!  Must specify the ETSD file.\n");
        exit(1);        
    }else{
        etsdInit(srcPrt[4], loadLabels);         
    }
    srcCnt++;
    for (lp=0;lp<srcCnt;lp++){  // load source plugins
       *(void **)(&edsSUp)=dlsym(handle[lp],"edsSetup");
        edsSUp(srcPrt[lp], srcCfg[lp], LogLvl>2?1:0);
        free(srcPrt[lp]);
        free(srcCfg[lp]);
        *(void **)(&Check_ptr[lp])=dlsym(handle[lp],"edsCheckData");
        *(void **)(&Read_ptr[lp])=dlsym(handle[lp],"edsReadChan");
    }
    if(NULL!=handle[4]){
        *(void **)(&edsSUp)=dlsym(handle[4],"xdSetup");
        edsSUp(xData, srcCfg[4], EtsdInfo.xData);
        *(void **)(&ReadLock)=dlsym(handle[4],"xdReadLock");
        free(srcCfg[4]);
    }
    if(NULL!=handle[5] ){ // load external DB plugin
        *(void **)(&edbSUp)=dlsym(handle[5],"edbSetup");
        *(void **)(&edbSave)=dlsym(handle[5],"edbSave");
        if (loadLabels){
            uint8_t idx = 0;
            chanNames = malloc(EtsdInfo.extDBcnt *  sizeof(char*));
            //ptr=EtsdInfo.labelBlob;
            for(lp=0;lp<EtsdInfo.channels;lp++){
                if(EXT_DB_bit(lp)){
                    chanNames[idx]= malloc(strlen(EtsdInfo.label[lp])+1);
                    strcpy(chanNames[idx++], EtsdInfo.label[lp]);
                }
            }
        } 
        edbSUp(srcPrt[5], srcCfg[5], EtsdInfo.extDBcnt, chanNames, EtsdInfo.blockIntervals, LogLvl>2?1:0);
        if (loadLabels){
            for(lp=0;lp<EtsdInfo.extDBcnt;lp++){
                free(chanNames[lp]);
            }
            free(chanNames);
            free (EtsdInfo.labelBlob);  // don't need the labels anymore
            free (EtsdInfo.label);
        }
    }
    *sCnt = srcCnt;
    return EtsdInfo.intervalTime + sleepTime;
}


int main(int argc, char *argv[])  {
#ifdef DAEMON  
    pid_t process_id = 0;
    pid_t sid = 0;
#endif
//#define sleepTime 8     // Note: there is an additional 1 second used to synchronize SHM data
//#define checkTime 30    // in 1/10 second increments so 30 = 3 seconds

    if (2 > argc){
        printf("\n\nNo config file specified, aborting\n\n");
        exit (1);
    }
    
#ifdef DAEMON    
    process_id = fork(); // Create child process
    if (process_id < 0) { // Indication of fork() failure
        Log("<3> fork failed!\n");
        exit (EXIT_FAILURE);
        //exit(1); // Return failure in exit status
    }
    if (process_id > 0) { // PARENT PROCESS. Need to kill it.
        printf("<5> process_id of child process %d \n", process_id);
        savepid("/var/run/ETSD.pid", process_id);
        exit(0); // return success in exit status
    }
    umask(0);  //unmask the file mode
    sid = setsid();  //set new session
    if(sid < 0)        // Return failure
        exit(1);

    chdir("/");  //change to root directory
    // Close stdin. stdout and stderr
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
#endif
    LogLvl = 1;
    
    signal(SIGTERM, sig_handler);   // systemd sends SIGTERM initially and then SIGKILL if program doesn't exit in 90 seconds
    signal(SIGINT, sig_handler);    // user termination, ctl-C or break
    signal(SIGHUP, sig_handler);    // user closed virtual terminal.  //Pete possibly restart as daemon?
    signal(SIGUSR2, sig_handler);   // reload config file

    //ecmSetup(TTYPort, EcmShmAddr);
    ELog("Main Clear Errors", 1);      //log any errors and zero ErrorCode

    //while (Check_ptr[0](110, 0));   // Pete need a better way to check multiple sources.  // wait for good data, 110 = 11 second 

    Reload = 1;
    while (1) {
        uint32_t data, dataArray[EtsdInfo.extDBcnt]; 
        char *xData;
        uint16_t sleepTime, checkTime[4];
        int8_t srcCnt;
        uint8_t lp, checkstat, edbCnt, saveEDB;
        uint8_t  srcReset=0, status=0, pause=10, statArr[EtsdInfo.extDBcnt];

        if(Reload){
            Reload = 0;
            Interval = 0;
            sleepTime = readConfig(argv[1], &srcCnt, checkTime, &saveEDB, xData);
            if (LogLvl){
                Log("<5> %s starting up with the following settings:\n  EtsdFile = %s \n  logLevel = %d\n", argv[0], EtsdInfo.fileName, LogLvl);
                Log("<5> The unit ID is %d, %d channels with %d Intervals per Block \n", (EtsdInfo.header)>>14, EtsdInfo.channels, EtsdInfo.blockIntervals );
            }
        }

        for(lp=0; lp<srcCnt; lp++){     // check sources
            status |= (Check_ptr[lp](checkTime[lp], Interval)&3)<<(lp*2);
        }
        ELog("Main 1", 1);
        if ( Interval == EtsdInfo.blockIntervals && NULL != Read_ptr[4]) {  // if saving Xdata
            ReadLock(1); // Lock xData
        }
        usleep(pause*100000);       // wait time between checking for new data and reading the data
        
        if(Interval) {
            edbCnt=0;
            for (lp=0; lp<EtsdInfo.channels; lp++){
                checkstat=(status>>(SRC_TYPE(lp)*2))&3;
                if( checkstat ){
                    data = 0xFFFFFFFF;
                    if (checkstat&2){ // source reset
                        srcReset != 1<<(7-lp);
                    }
                } else {
                    data = Read_ptr[SRC_TYPE(lp)](SRC_CHAN(lp), Interval);
 //fprintf(stderr,"Channel %u data = %u \n",lp,data);               
                } 
         
                if(EXT_DB_bit(lp)) {     // save channel to external DB
                    statArr[edbCnt]=checkstat;
                    dataArray[edbCnt++]=data;
                }
                // Save to ETSD
 
                if(ETSD_Type(lp)){    // save channel to etsd
                    saveChan(Interval, lp, checkstat, data);  
                }
            }
            if(saveEDB){
                edbSave(edbCnt, dataArray, statArr, Interval);
            }
            if (srcReset){
                etsdCommit(Interval);
                Interval=0;
            }
        }
//Log("main() Interval = %d and blockIntervals = %d\n", Interval, EtsdInfo.blockIntervals);
        ELog("Main 2", 1);
        if ( Interval == EtsdInfo.blockIntervals ) {
            if (LogLvl > 2) {
                Log("<5> About to write the following to the ETSD file: %s\n", EtsdInfo.fileName);
                LogBlock(&PBlock.byteD, "ETSD", 512);
            }
            if ( NULL != Read_ptr[4]) {  // if saving Xdata
                for(lp=0; lp < EtsdInfo.xData; lp++){
                    PBlock.byteD[EtsdInfo.xDataStart+lp]=xData[lp];
                }
                ReadLock(0); // unlock xData
            }
            etsdCommit(Interval);
            Interval = 0;
        }
        ELog("Main 3", 1);  
        
        if (!Interval){   
            etsdBlockClear(0xffff); // by default 0xffff indicates invalid value
            etsdBlockStart();  
            PBlock.byteD[6] = srcReset;
            for (lp=0; lp<EtsdInfo.channels; lp++){
                if(ETSD_Type(lp)){    // save channel to etsd
                    checkstat=(status>>(SRC_TYPE(lp)*2))&3;
                    saveChan(Interval, lp, checkstat, checkstat?0xFFFFFFFF:(Read_ptr[SRC_TYPE(lp)](SRC_CHAN(lp), Interval)));  
                }
            }
        }
        ELog("Main 4", 1);
        sleep(sleepTime);
        Interval++;
    }
}

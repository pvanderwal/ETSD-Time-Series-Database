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

int8_t Interval;
volatile sig_atomic_t Reload;    // reload config file

void *handle[3]={NULL}; // pointer to dynamically loaded plugins //is NULL redundant?
uint8_t (*edoSave)(uint32_t timeStamp, uint8_t interval, uint32_t *dataArray, uint8_t *statusArray, uint8_t *xData);
void (*xdRead)(uint8_t interval, uint8_t cnt, uint8_t *dataArray);

typedef struct {
    void *handle;
    uint8_t (*Check_src)(uint16_t timeOut, uint8_t interV);
    uint32_t (*Read_src)(uint8_t chan);
} SRC_PLUGIN;

SRC_PLUGIN SrcPlugin[4];

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

// handle[0] = edo, [1]=xData, [2]=readConfig 
// returns number of Sources.  uint8_t *saveEDB,
uint16_t readConfig( char *configFileName, SRC_PLUGIN SrcPlugin[4], uint16_t *checkTime) {
    FILE *fptr;
    char commentChar;  //first character in config file defines the comment character so user can change if needed.
    char *etsdFile=NULL, *ptr;
    char **chanNames=NULL;       // Array of string pointers
    uint16_t *chanDefs=NULL;       

    uint8_t (*srcSUp)(char *config, char *source, char *configFileName, uint16_t etsdHeader, uint16_t intervalTime);  
    //*Func EDO plugin edoSetup()
    uint8_t (*edoSUp)(char *config, char *destination, char *configFileName, uint8_t chanCnt, uint16_t *chanDefs, char **chanNames ); 
    uint8_t lp, loadNames = 0, keepNames=0;
    char configLine[260];
    int8_t srcCnt=-1;

    struct {
        char *config;
        char *pds;          // port/destination/source used for source/edo/xdata
    } cfgStrings[6]={NULL,NULL}; // cfgStrings[0]-[3] source, [4] edo. [5]=xData
    
    *checkTime=0;

    if ( NULL == (fptr = fopen(configFileName, "r")) ) {
        Log("<3> Error! Can't open config file: %s\n", configFileName);
        exit(1);
    }
//printf("Config file %s\n", configFileName);
    fscanf(fptr,"%[^\n]\n", configLine);
    commentChar = *configLine;

    while ( fscanf(fptr,"%[^\n]\n", configLine) != EOF ){
 //       printf("Config line %s\n", configLine);
        if (*configLine==commentChar || *configLine=='\0')  // comment or blank line, skip to next line
            continue;
        if (!strcmp(configLine,"ETSD_END"))  // end of ETSD section
            break;        
        ptr = strchr(configLine,':'); 
        if ( ptr && ptr++ < configLine+3 ){     // did it find ':' in the second or third position 
            switch(*configLine){
            case 'E':                           // ETSD DB
                if (*ptr == '=')                // shared version has both ':' and '='
                    ptr++;                      // skip the '='
                etsdFile = (char*) malloc(strlen(ptr)+1);
                strcpy(etsdFile, ptr);
                break;              
            case 'S':                       // Source(s)
                if ('N'==configLine[1] ){
                    if(3 < ++srcCnt){
                        Log("\n\nError!  Config file contains too many data sources.  ETSD supports a maximum of 4 data sources.\nAborting, please fix config file.\n");
                        exit(1);
                    }

                    SrcPlugin[srcCnt].handle = dlopen (ptr, RTLD_LAZY);
                } else if ('P'==configLine[1] ){                        
                    cfgStrings[srcCnt].pds = (char*) malloc(strlen(ptr)+1);
                    strcpy(cfgStrings[srcCnt].pds, ptr);       
                } else if ('C'==configLine[1] ){   
                    cfgStrings[srcCnt].config = (char*) malloc(strlen(ptr)+1);
                    strcpy(cfgStrings[srcCnt].config, ptr);                
                } else if ('T'==configLine[1] ){ 
                    *checkTime = atoi(ptr);
                } 
                break;
            case 'D':                       // External Data Out
                if ('N'==configLine[1] ){           // plugin name`
                    handle[0] = dlopen (ptr, RTLD_LAZY);
                }else if ('C'==configLine[1] ){     // additonal configuration line
                    cfgStrings[4].config = (char*) malloc(strlen(ptr)+1);
                    strcpy(cfgStrings[4].config, ptr); 
                }else if ('D'==configLine[1] ){     // EDO *destination
                    cfgStrings[4].pds = (char*) malloc(strlen(ptr)+1);
                    strcpy(cfgStrings[4].pds, ptr);
                } else if ('L'==configLine[1] ){ 
                    loadNames = atoi(ptr); 
                } else if ('K'==configLine[1] ){ 
                    keepNames = atoi(ptr);
                }                 
                break;
            case 'L':                       // Log file
                if ('F'==configLine[1] ){
                    LogSetup(ptr);
                } else if ('V'==configLine[1] ){
                    LogLvl = atoi(ptr);
                }
                break; 
            case 'X':                       // xData (Extra Data) plugin
                if ('N'==configLine[1] ){
                    handle[1] = dlopen (ptr, RTLD_LAZY);
                }else if ('C'==configLine[1] ){
                    cfgStrings[5].config = (char*) malloc(strlen(ptr)+1);
                    strcpy(cfgStrings[5].config, ptr);       
                }else if ('S'==configLine[1] ){     // EDO *destination
                    cfgStrings[5].pds = (char*) malloc(strlen(ptr)+1);
                    strcpy(cfgStrings[5].pds, ptr);
                }
                break;  
            }
        } else if (*configLine=='E' && configLine[5]=='E')
            break;  // end of etsd config info, exit loop
    }
    fclose(fptr);

// finished reading config file, now process it        
    if(NULL==etsdFile){    
        Log("\n\nError!  Must specify the ETSD file.\n");
        exit(1);        
    } else {
        etsdInit(etsdFile, loadNames);       
    }

    srcCnt++;
    if (!srcCnt){
        Log("\n\nError! Must specify at least one data source.\n");
        exit(1);
    }

    for (lp=0;lp<srcCnt;lp++){  // load source plugins
        *(void **)(&srcSUp)=dlsym(SrcPlugin[lp].handle,"srcSetup");
        *(void **)(&SrcPlugin[lp].Check_src)=dlsym(SrcPlugin[lp].handle,"srcCheckData");
        *(void **)(&SrcPlugin[lp].Read_src)=dlsym(SrcPlugin[lp].handle,"srcReadChan");

        srcSUp(cfgStrings[lp].config, cfgStrings[lp].pds, configFileName, EtsdInfo.header, EtsdInfo.intervalTime);
    }

    if(NULL!=handle[0] ){ // load EDO plugin
        uint8_t idx = 0;
        *(void **)(&edoSUp)=dlsym(handle[0],"edoSetup");
        *(void **)(&edoSave)=dlsym(handle[0],"edoSave");
        
        chanDefs = malloc(EtsdInfo.edoCnt * 2);
        if (loadNames) 
            chanNames = malloc(EtsdInfo.edoCnt *  sizeof(char*));
            
        for(lp=0;lp<EtsdInfo.channels;lp++){
            if(EDO_BIT(lp)){
                if (loadNames) 
                    strcpy(chanNames[idx], EtsdInfo.label[lp]);
                chanDefs[idx++] = EtsdInfo.source[lp]<<8 + EtsdInfo.destination[lp];
            }
        }
        free(EtsdInfo.label);       // don't need the labels anymore
        free(EtsdInfo.labelBlob); 
        
        edoSUp(cfgStrings[4].config, cfgStrings[4].pds, configFileName, EtsdInfo.edoCnt, chanDefs, chanNames);  // setup external data output

        free(chanDefs);
        if (!keepNames){
            free(chanNames);
        }
    }
    if(NULL!=handle[1]){    // load xData plugin
        *(void **)(&srcSUp)=dlsym(handle[1],"xdSetup");
        *(void **)(&xdRead)=dlsym(handle[1],"xdRead");
        srcSUp(cfgStrings[5].config, cfgStrings[5].pds, configFileName, EtsdInfo.header, EtsdInfo.intervalTime);
    }

    //dlclose(dl_library);
    return srcCnt;
}

int main(int argc, char *argv[])  {

    uint16_t sleepTime, checkTime;
    uint8_t srcCnt, edoCnt, saveEDO;
    
#ifdef DAEMON  
    pid_t process_id = 0;
    pid_t sid = 0;
#endif
//#define sleepTime 8     // Note: there is an additional 1 second used to synchronize SHM data

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

    ELog("Main Clear Errors", 1);      //log any errors and zero ErrorCode

    Reload = 1;
    while (1) {
        uint32_t data, dataArray[EtsdInfo.edoCnt]; 
        uint8_t lp, checkstat;
        uint8_t  srcReset=0, status[4]={0}, pause=10, statArr[EtsdInfo.edoCnt];

        if(Reload){     // reload config file
            Reload = 0;
            Interval = 0;
            srcCnt = readConfig( argv[1], SrcPlugin, &checkTime);
            sleepTime = EtsdInfo.intervalTime - checkTime/2;

            if (LogLvl){
                Log("<5> %s starting up with the following settings:\n  EtsdFile = %s \n  logLevel = %d\n", argv[0], EtsdInfo.fileName, LogLvl);
                Log("<5> The unit ID is %d, %d channels with %d Intervals per Block \n", (EtsdInfo.header)>>14, EtsdInfo.channels, EtsdInfo.blockIntervals );
            }
        }

        for(lp=0; lp<srcCnt; lp++){     // check sources
            status[lp] = SrcPlugin[lp].Check_src(checkTime, Interval);
        }
        ELog("Main 1", 1);
//        if ( Interval == EtsdInfo.blockIntervals && NULL != xDataLock) {  // if saving Xdata
  //          xDataLock(1); // Lock xData
    //    }
//        usleep(pause*100000);       // wait time between checking for new data and reading the data
        
        if(Interval) {
            edoCnt=0;

            for (lp=0; lp<EtsdInfo.channels; lp++){
                // checkstat=(status>>(SRC_TYPE(lp)*2))&3;
                if( status[SRC_TYPE(lp)]){
                    data = 0xFFFFFFFF;
                    if (status[SRC_TYPE(lp)]&2){ // source reset
                        srcReset != 1<<(7-lp);
                    }
                } else {
                    data = SrcPlugin[SRC_TYPE(lp)].Read_src(SRC_CHAN(lp));
 //fprintf(stderr,"Channel %u data = %u \n",lp,data);               
                } 
         
                if(EDO_BIT(lp)) {     // save channel to external Data out
                    statArr[edoCnt]=status[SRC_TYPE(lp)];
                    dataArray[edoCnt++]=data;
                }
 
                if(ETSD_TYPE(lp)){    // save channel to etsd
                    saveChan(Interval, lp, status[SRC_TYPE(lp)], data);  
                }
            }

            if(edoSave){
                edoSave( 0, Interval, dataArray, statArr, &PBlock.byteD[EtsdInfo.xDataStart] );
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
            if (NULL != (*xdRead)) {  // if saving Xdata
            xdRead(Interval, EtsdInfo.xDataSize, &PBlock.byteD[EtsdInfo.xDataStart]);
//                for(lp=0; lp < EtsdInfo.xDataSize; lp++){
  //                  PBlock.byteD[EtsdInfo.xDataStart+lp]=xData[lp];
    //            }
      //          xDataLock(0); // unlock xData
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
                if(ETSD_TYPE(lp)){    // save channel to etsd
                    saveChan(Interval, lp, status[SRC_TYPE(lp)], data);
                }
            }
        }
        ELog("Main 4", 1);
        sleep(sleepTime);
        Interval++;
    }
}
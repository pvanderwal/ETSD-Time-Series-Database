/*************************************************************************
 edd.c application.  Energy Data Director (edd) Connects ecmR (collecting energy data from an Brultech  ECM-1240) 
 and other data collectors via Shared Memory (SHM) to ETSD Time Series Database, RRD database, and outputs data to shared memory

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

#ifndef NO_RRD
#include "/usr/include/rrd.h"
#endif

#include "ecmR.h"
#include "etsdSave.h"
#include "errorlog.h"

#ifndef NO_SHM
#include "eshm.h"
#endif

// in the USA Utilization voltage range is from 108V to 126V
// an AC_OFFSET of 1040 allows measuring from 104.1V to 129.4V
// and marking undervoltage (0x01), overvoltage (0xFE), zero voltage (0x00), and invalid reading (0xFF)
#define AC_OFFSET 1040      // Subtract 104.0V from AC voltage so that it will fit in Half Stream (1 byte)

#define AUX5_DC newData/256  // you can specify a data reduction formula if saving aux5 dc to 1/2 stream
// #define AUX5_DC (newData/4)-512 // different dara reduction scheme
// #define AUX5_DC newData>1024?newData-1024:0  // conditional statement

uint_fast8_t Interval;

char *EcmShmAddr;
char *TTYPort;
char *RRDFile;

void sig_handler(int signum) {
    Log("Received termination signal, attempting to save ETSD block and exiting.\n");
    if (NULL != EtsdInfo.fileName)
        etsdCommit(Interval);
    exit(0);
}

#ifdef DAEMON  
void savepid (char *file, pid_t pId){
    FILE *pID;
    pID = fopen (file, "w+");		
    fprintf(pID,"%d \n", pId);
    fclose(pID);
}
#endif


// if interV = 0, save registers (if applicable)
// pete note:  call on start up after RX first reading from ECM and before interval 1, on last blockinterval, commit, clear etsd, 
// call with interV = 0 at beging of each block to save registers and reset 'relative' value arrays.
void saveData(uint8_t interV, int8_t dataInvalid){
    uint8_t lp, lp2, goodUpdate, missed, extS,  relCh=0, reg=0, extSCh=0, AS=0, FS=0, HS=0;
    uint32_t newData;
#ifndef NO_RRD
//    char rrdValues[EtsdInfo.rrdCnt + 1][12];
    char rrdValues[EtsdInfo.rrdCnt*11+15];
    char tempVal[15];
    char *rrdParams[] = { "rrdupdate", RRDFile, rrdValues, NULL }; 
    
    sprintf(rrdValues,"N");
#endif
    
//    check ecm
    for (lp=0; lp<EtsdInfo.channels; lp++){
        if (SRC_PRIMARY(lp)) {    // source type = ecmR
            if(!dataInvalid){           // Pete need to update if using something other than ECM-1240 as primary
                if (10 > SRC_CHAN(lp)){
                    newData = ecmGetCh(SRC_CHAN(lp));
                } else {
                    newData = ecmGetVolt(SRC_CHAN(lp) - 10);
                }
            }
        } else if (SRC_SHM(lp)) {  // source type = SHM    
            dataInvalid = shmRead(SRC_CHAN(lp), 13, &newData);
            dataInvalid = 0>dataInvalid?1:0;
        } // Pete add "other sources" here
#ifndef NO_RRD  
        if(RRD_bit(lp)) {     // save channel to rrd
            if (dataInvalid)  
                sprintf(tempVal,":U");
            else 
                sprintf(tempVal,":%u", newData);
            strcat(rrdValues, tempVal);
        }
#endif
        // Pete add "other destination" here
        
        // Save to ETSD
//pete  LogBlock(&dataU->byteD, "ECM", 64);
        if(ETSD_Type(lp)){    // save channel to etsd
            if (SRC_PRIMARY(lp) && 6>ETSD_Type(lp) && 9<SRC_CHAN(lp)){  // Pete if using a different source than ECM-1240, probably need to remove this section.
                if ( 11 == SRC_CHAN(lp)){      //ECM AC Voltage
                    if (newData) {                 // zero = power outage during interval
                        if (newData < AC_OFFSET)
                            newData = 1;           // 1 = Brownout
                        else {
                            newData -= AC_OFFSET;
                            if(0xFE < newData)
                                newData = 0xFE;    // 0xFE = over voltage, 0xFF = invalid data
                        }
                    }
                    if (4>ETSD_Type(lp)) // saving to 1/4 stream
                        newData = newData>224?14:newData<32?1:(newData>>4);  // reduce to 4 bits
                } else if ( 10 == SRC_CHAN(lp)){    // ECM Aux5 DC
                    newData = AUX5_DC;
                }
            }
            
            saveChan(interV, lp, dataInvalid, newData);
        }
    }
    

#ifndef NO_RRD 
    if (NULL != RRDFile&&EtsdInfo.rrdCnt){
        if (LogLvl>2)
            Log("<5> RRD data = %s\n", rrdValues);
        optind = opterr = 0; // Because rrdtool uses getopt() 
        rrd_clear_error();
        rrd_update(3, rrdParams);  
    }
#endif
}


void readConfig( char *fileName) {
    //char *ptr;
    char configLine[256];
    FILE *fptr;
      
    if ( NULL == (fptr = fopen(fileName, "r")) ) {
        Log("<3> Error! opening config file: %s\n", fileName);
        exit(1);
    }

    while ( fscanf(fptr,"%[^\n]\n", configLine) != EOF){
        if (*configLine=='#' || *configLine=='\0')  // # = comment, skip to next line
            continue;
        //ptr = strchr(configLine,':'); 
        switch(*configLine){
        case 'L':
            LogSetup(configLine+2);
            break;
        case 'M':       //shM address
            EcmShmAddr = (char*) malloc(strlen(configLine+1));
            strcpy(EcmShmAddr,configLine+2);
            break;
        case 'P':
            TTYPort = (char*) malloc(strlen(configLine+1));
            strcpy(TTYPort,configLine+2);
            break;
#ifndef NO_RRD 
        case 'R':
            RRDFile = (char*) malloc(strlen(configLine+1));
            strcpy(RRDFile,configLine+2);
            break;
#endif
        case 'T':
            etsdInit(configLine+2, 0);
            break;
        case 'V':
            LogLvl = atoi(configLine+2);
            break;
#ifndef NO_SHM
        case 'Z':
            shmInit(atoi(configLine+2));
            break;
#endif
        }
    }
    fclose(fptr);
}


int main(int argc, char *argv[])  {
    uint_fast8_t lp, chkErr=0;
#define sleepTime 8     // Note: there is an additional 1 second used to synchronize SHM data
#define checkTime 30    // in 1/10 second increments so 30 = 3 seconds
    
   const char *help = "\n Command Line options:\n\t -C /path/to/configFile  | read configuration from filename\n\t -l /path/to/LogFile\n\t -v [0-4]  Log Level\n\t -s /shmName             | Shared Memory Address/name \n\t -p /path/to/tty         | TTY port for ECM-1240\n\t -r /path/to/RRDFile\n\t -t /path/to/EtsdFile    | Petes Time Series dB file \n\n";

#ifdef DAEMON  
    pid_t process_id = 0;
    pid_t sid = 0;
#endif

    LogLvl = 1;
    
    signal(SIGTERM, sig_handler);   // systemd sends SIGTERM initially and then SIGKILL if program doesn't exit in 90 seconds
    signal(SIGINT, sig_handler);    // user termination, ctl-C or break
    signal(SIGHUP, sig_handler);    // user closed virtual terminal.  //Pete possibly restart as daemon?

    //Process command line arguments if any
    if (1 < argc) {  // we have command line arguments
        for ( lp = 1; lp < argc; lp++ ){
             if ('-'==*argv[lp]){
                switch(*(argv[lp++]+1)){
                    case 'C':
                        readConfig(argv[lp]);
                        break;
                    case 'P':
                        TTYPort = argv[lp];
                        break;
                    case 'T':
                        if(etsdInit(argv[lp], 0)){
                            ELog(__func__, 1);
                            exit(1);
                        }
                        break;
                    case 'L':
                        LogSetup(argv[lp]);
                        break;
                    case 'V':
                        LogLvl = atoi(argv[lp]);
                        break;
#ifndef NO_RRD 
                    case 'R':
                        RRDFile = argv[lp];
                        break;
#endif
                    case 'H':
                        printf("%s",help);
                        exit(0);
                        break;
                    default:
                        Log("Unknown command line option '%s' \n", argv[lp]);
                        exit(1);    
                }
            }
        }
    } else {
        printf("%s",help);
        exit (1);
    }

    if (NULL == TTYPort) {
        Log("<3> No tty port specified, exiting.\n");
        exit(1);
    }

    if (LogLvl){
// pete remove the following in final version?
#ifndef NO_RRD 
        Log("<5> %s starting up with the following settings:\n  TTYPort = %s \n  RRDFile = %s \n  EtsdFile = %s \n  logLevel = %d\n", argv[0], TTYPort, RRDFile, EtsdInfo.fileName, LogLvl);
#else
        Log("<5> %s starting up with the following settings:\n  TTYPort = %s \n   EtsdFile = %s \n  logLevel = %d\n", argv[0], TTYPort, EtsdInfo.fileName, LogLvl);
#endif
        Log("<5> The unit ID is %d, %d channels with %d Intervals per Block \n", (EtsdInfo.header)>>12, EtsdInfo.channels, EtsdInfo.blockIntervals );
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
        savepid("/var/run/ecmETSD.pid", process_id);
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
    if (NULL==EcmShmAddr) {
        EcmShmAddr = (char*)malloc(2);
        EcmShmAddr = "";
    }
    ecmSetup(TTYPort, EcmShmAddr);
    ELog("Main Clear Errors", 1);      //log any errors and zero ErrorCode
      
    while (4>ecmR(10, 0));  // Clear out any data in buffers
    while (ecmR(110, 0));   // wait for good data, 110 = 11 second 

    while (1) {

        if(!Interval) {
            etsdBlockClear(0xffff); // by default 0xffff indicates invalid value
            etsdBlockStart();  
            saveData(0, chkErr);  // save registers
        }
        Interval++;
        sleep(sleepTime);
        chkErr = ecmR(checkTime, Interval);
        if(chkErr)
            Log("Main loop chkErr #%d\n", chkErr);
        ELog("Main after EcmR", 1);  //log any errors and zero ErrorCode
        if (LogLvl>3)
            LogBlock(&dataU->byteD, "ECM", 64);
        sleep(1); // give external programs time to update SHM data before storing it  
        saveData(Interval, chkErr);
//Log("main() Interval = %d and blockIntervals = %d\n", Interval, EtsdInfo.blockIntervals);
        if ( Interval == EtsdInfo.blockIntervals ) {
            if (EtsdInfo.fileName != NULL){
                if (LogLvl > 2) {
                    Log("<5> About to write the following to the ETSD file: %s\n", EtsdInfo.fileName);
                    LogBlock(&PBlock.byteD, "ETSD", 512);
                }
                if( etsdCommit(Interval) )
                    ELog(__func__, 1);  
            }
            Interval = 0;
        }        
    }
}

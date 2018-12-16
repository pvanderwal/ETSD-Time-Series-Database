/*************************************************************************
 errorLog.c library ETSD time series database 

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
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>     /* va args */
#include <time.h>
#include <string.h>

#include "errorlog.h"

char *LogFile;
int8_t LogLvl;
uint32_t ErrorCode;

// you can just change loglvl by calling with fName=""
void LogSetup(const char *fName){
    LogFile = (char *) malloc(strlen(fName)+1);
    strcpy(LogFile,fName); 
    ErrorCode = 0;
}

// cfnm = calling function name + format string
// Clear 1 = zero ErrorCode before returning
// Returns current timestamp
void ELog(const char *cfnm, uint8_t clear){
    uint8_t idx=0;
    uint32_t eCode=ErrorCode;
    char *summary[]={
        "End of file reached",
        "Check Sum error",
        "Timed out waiting on serial data",
        "Device specified is not a TTY device",
        "Can't get status of tty device",
        "Can't open/create shared memory object",
        "File header block(0) not an ETSD file",
        "Target time is BEFORE ETSD starts",
        "Target  time is AFTER ETSD ends",
        "Can't open file for reading",
        "Can't open file for writing",
        "Error seeking sector",
        "Unspecified coding error",
        "Invalid date/time format"};
    
    while(eCode){
        if(eCode&1)
            Log("<3>Function %s() reports %s \n", cfnm, summary[idx]);
        eCode >>= 1;
        idx++;
    }
    if(clear)
        ErrorCode = 0;
}

void Log(const char *format, ...){
    time_t now;
    char *currentTime;
    FILE *pLog = 0;
    va_list args;
    
    time(&now);

    if (NULL != LogFile) 
        pLog=fopen(LogFile,"a");
    va_start(args, format);
    
    if (pLog > 0) {
        currentTime = ctime(&now);
        currentTime[strlen(currentTime)-1] = '\0';
        fprintf(pLog, "%s ", currentTime);
        vfprintf (pLog, format, args);
    } else {
        //printf("%s ", currentTime);
        vfprintf (stderr, format, args);
    }
    va_end(args);
    if (pLog > 0)
        fclose (pLog);
}



//1 print ecm, 2 print etsd
void LogBlock(void *ptr, uint16_t size){
    char scratch[70], sblock[2000];
    uint8_t lp, lp2, *array = (uint8_t*)ptr; 
    uint16_t addr=0;
    
    sprintf(sblock, "<6> %4s block", size > 70?"ETSD":"ECM");
    for (lp=0; lp < size/16; lp++){
        sprintf(scratch, "\n[%2d] ", lp);
        strcat(sblock, scratch);
        for (lp2=0; lp2<16; lp2++){
            sprintf(scratch, "%02X ", array[lp*16+lp2]);
            strcat(sblock, scratch);
            if (7==lp2)
                strcat(sblock, "| ");
        }
    }
    strcat(sblock,"\n");
    Log(sblock);
}

/*************************************************************************
 edsRRD.c ETSD External DataBase plugin for RRDTool

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
#include <string.h>
//#include <stdarg.h>     /* va args */
//#include <signal.h>
//#include <sys/stat.h>
//#include <fcntl.h>

#include "edbRRD.h"
#include "errorlog.h"
#include "/usr/include/rrd.h"

uint8_t ChannelCnt;
uint8_t LogData;
char *RRDFile;

uint8_t edbSetup (char *DBfileName, char *Config, uint8_t chanCnt, char **chanNames, uint8_t totInterv, uint8_t LogD){
fprintf(stderr,"got to checkpoint 1\n");
    RRDFile = (char*) malloc(strlen(DBfileName)+1);  // Pete should test to see if this fails
fprintf(stderr,"got to checkpoint 1\n");
    strcpy(RRDFile, DBfileName);
    ChannelCnt = chanCnt;
    LogData = LogD; 
    return 0;
}

// cnt is the number of channels to save, dataArray is an array of those channels, 
// *status array matches *dataArray  per channel:  0 = ok, 1 = data invalid, 2=src_reset
uint8_t edbSave(uint8_t cnt, uint32_t *dataArray, uint8_t *status, uint8_t interval){
    uint8_t lp;
    uint32_t newData;
//    char rrdValues[EtsdInfo.rrdCnt + 1][12];
    char rrdValues[cnt*11+15];
    char tempVal[15];
    char *rrdParams[] = { "rrdupdate", RRDFile, rrdValues, NULL }; 
    
    rrdValues[0] = 'N';
    rrdValues[1] = '\0';
//    sprintf(rrdValues,"N");
    
    for (lp=0; lp<cnt; lp++){
       if (status[lp] || dataArray[lp] == 0xFFFFFFFF){  // if source, or data, is invalid
            sprintf(tempVal,":U");
       }else{
            sprintf(tempVal,":%u", dataArray[lp]);
       }
        strcat(rrdValues, tempVal);

    }  
    if (LogData)
        Log("<5> RRD data = %s\n", rrdValues);
    optind = opterr = 0; // Because rrdtool uses getopt() 
    rrd_clear_error();
    rrd_update(3, rrdParams);  // Pete how do I determine if rrd_update had errors?
}


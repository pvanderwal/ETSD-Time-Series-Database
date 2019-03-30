/*************************************************************************
 edoRRD.c ETSD External DataBase plugin for RRDTool

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

#include "edoRRD.h"
#include "errorlog.h"
#include "/usr/include/rrd.h"

uint8_t ChannelCnt;
char *RRDFile;

// not using *Config, *cfgfn(configFileName), chanCnt, ChanDefs, **chanNames or xdSize
uint8_t edoSetup(char *Config, char *DBfileName, char *cfgfn, uint8_t chanCnt, uint16_t ChanDefs, char **chanNames, uint8_t xdSize){
    RRDFile = (char*) malloc(strlen(DBfileName)+1);  // Pete should test to see if this fails
    strcpy(RRDFile, DBfileName);
    ChannelCnt = chanCnt;
    return 0;
}

// not using interval or xData
// *status array matches *dataArray  per channel:  0 = ok, 1 = data invalid, 2=src_reset
uint8_t edoSave(uint32_t timeStamp, uint8_t interval, uint32_t *dataArray, uint8_t *status, uint8_t *xData){
    uint8_t lp;
    uint32_t newData;
//    char rrdValues[EtsdInfo.rrdCnt + 1][12];
    char rrdValues[ChannelCnt*11+15];
    char tempVal[15];
    char *rrdParams[] = { "rrdupdate", RRDFile, rrdValues, NULL }; 
    
    if (timeStamp){
        sprintf(rrdValues, "%u", timeStamp);
    } else {
        rrdValues[0] = 'N';
        rrdValues[1] = '\0';
    }
//    sprintf(rrdValues,"N");
    
    for (lp=0; lp<ChannelCnt; lp++){
       if (status[lp] || dataArray[lp] == 0xFFFFFFFF){  // if source, or data, is invalid
            sprintf(tempVal,":U");
       }else{
            sprintf(tempVal,":%u", dataArray[lp]);
       }
       strcat(rrdValues, tempVal);

    }  
    if (LogLvl>2)
        Log("<5> RRD data = %s\n", rrdValues);
    optind = opterr = 0; // Because rrdtool uses getopt() 
    rrd_clear_error();
    rrd_update(3, rrdParams);  // Pete how do I determine if rrd_update had errors?
}


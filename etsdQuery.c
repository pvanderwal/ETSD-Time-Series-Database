/*************************************************************************
etsdQuery.c library.  Used to create an ETSD time series database, convert an ETSD to RRD database, and display info on an ETSD. 

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
#define _GNU_SOURCE     // needed to keep strcasestr() for throwing an error

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>      // for isalnum()

#ifndef NO_RRD
#include "/usr/include/rrd.h"
#endif

#include "errorlog.h"
#include "etsdSave.h"
#include "etsdRead.h"
#include "etsdQuery.h"

// converts strings decribing +/- time and returns the number of seconds (positive or negative) they represent
// valid forms are 10s, -356S, 4hours, -12h, 3.5minutes, etc.
int parseT(char *val){
    char *ptr=strpbrk(val,"sSmMhHdDyY");
    float secs=atof(val);
    
//    switch(val[strlen(val)-1]){
    switch(*ptr){
        case 's':
        case 'S':
            /* secs already = secs */
            break;
        case 'm':
        case 'M':
            secs *= 60;
            break;
        case 'h':
        case 'H':
            secs *=3600;
            break;
        case 'd':
        case 'D':
            secs *=86400;
            break;
        case 'y':
        case 'Y':
            secs *=31536000;
            break;
        default:        
            if (48 > val[strlen(val)-1] || val[strlen(val)-1] > 57 ) { // if last character is not a number
                ErrorCode = E_DATE;
                ELog(__func__, 0);  // log the error
                secs = 0;  //calling functions need to check ErrorCode 
            }
    }
    return secs;
}

//returns epoch time represented by dt or zero if there was an error parsing dt
int etsdParseTime(char *dt){
    char *ptr, *ptr2;
    time_t now=time(NULL);
    struct tm *t=localtime(&now);
    int rval=0;

    char junk[35];
    //time(&now);

    ELog("etsdParseTime start", 1);  // report any previous errors and zero out ErrorCode

    if((ptr=strchr(dt,'+'))){
        rval = parseT(ptr);
    } else if (ptr=strchr(dt,'-')) {
        ptr2=ptr;
        ptr2++;
        if (!strchr(ptr2,'-')){
            rval = parseT(ptr);
        }
    }
    
    if(!rval && ErrorCode){
        ELog(__func__, 0);
        return 0;
    }
    
    if(strcasestr(dt,"now")){
        if( strchr(dt,'+')){         // date/time in the future is not valid
            ErrorCode = E_DATE;
            ELog(__func__, 0);
            rval = 0;
        }
        rval = now + rval;
    } else if(strcasestr(dt,"mid")){
        t->tm_hour = 0;
        t->tm_min = 0;
        t->tm_sec = 0;
        rval = mktime(t) + rval;          
    } else if(strcasestr(dt,"begin")){
        ELog(__func__, 1);
        rval = (int)etsdTimeS(1);    
        if (0>rval && ErrorCode){
            ELog(__func__, 0);
            rval = 0;
        }
    } else { 
        if(ptr=strchr(dt,'/')){    
            if(ptr2=strchr(dt,':')){
                ptr2=strchr(ptr,' ');
                if(7>ptr2-ptr){
                    strptime(dt, "%D %T", t);      //  mm/dd/yy hh:mm:ss   american style
                } else {
                    strptime(dt, "%m/%d/%Y %T", t); //  mm/dd/yyyy hh:mm:ss   
                }
                rval = mktime(t);
            } else {
                ErrorCode = E_DATE;
                ELog(__func__, 0);
                rval = 0;                
            }
        } else if(ptr=strchr(dt,'-')) {
            if(ptr=strchr(dt,':')){
                strptime(dt, "%F %T", t); // yyyy-mm-dd hh:mm:ss   ISO 8601 date format.
                rval = mktime(t);                  
           }           
        } else if(ptr=strchr(dt,':')) {
            strptime(dt, "%T", t); // today? hh:mm:ss
            rval = mktime(t);   
            if(rval > now)
                rval -= 86400;
        } else {
            //rval = parseT(ptr) ;
        }
        // convert specific time to epoch)
    }
    return rval;
}

// (following are optional ->) t=(interval time, default 10s) r=(rrd) d=(datapoints default 25hrs worth of interval Time) u=(unit id default 0) x=(extra data bytes default 0)
// /path/to/new/file.tsd (optional)[/path/to/new/file.rrd] (unit id)[U=2] (interval time)[T=10s|T=10m|etc] (If blank defaults to 10 seconds)
// Name:Stream Type[1-15]:[C|COUNTER|G|GUAGE](default C):SourceType[E|M]SrcChan[1-63]:Save Register[S]:Signed Integer[I]:Save to RRD[R] <space>
// 
// /var/db/garage.tsd /var/db/garage.rrd u=2 T=10s Water_Heater:6:E4:R:S MiniSplit:8:E3:R:S
//
//#define labelSize block[8]

/*
rrdtool create garage2.rrd --step 10 DS:Ch1A:COUNTER:12:0:26215 DS:Ch2A:COUNTER:12:0:26215 DS:Aux1:COUNTER:12:0:7000 DS:Aux2:COUNTER:12:0:7000 DS:Aux3:COUNTER:12:0:7000 DS:Aux4:COUNTER:12:0:7000 DS:Aux5:COUNTER:12:0:7000 RRA:LAST:0.8:1:8700 RRA:AVERAGE:0.65:6:2900 RRA:AVERAGE:0.65:45:1500 RRA:AVERAGE:0.65:180:1500 RRA:MAX:0.65:180:1500 RRA:MIN:0.65:180:1500 RRA:AVERAGE:0.65:2160:1500 

DS:ds-name:{GAUGE | COUNTER }:heartbeat:min:max  (25 + name)
 
RRA:{AVERAGE | MIN | MAX | LAST}:xff:steps:rows  30 * 5 = 150
*/


//  totalTime(zero = unlimited), datapoints(0 = automatic), rraC=# ellements in the array rraV[](first value = type)
int buildRRD(char *fName, uint8_t channels, uint8_t chanMap[], int totalTime, int datapoints, int rraC, char *rraV[]){
    uint8_t lp, cnt=4;
    char tempVal[60];
//    char step[15];
    char rrdValues[EtsdInfo.channels*40+175+strlen(fName)]; // needs to be large enough for worse case
 //   char *rrdParams[4] = { fName, step, rrdValues, NULL };
    char **rrdParams;
    char *ptr;
    int maxVal, maxDP, rraSteps[6]={1,60,420,1800,21600,86400}; //1 minute steps for daily graph, 7 minute for weekly, 30 minute for monthly, 6 hour for annual, 1 day for ~4 years

    if (!datapoints)
        datapoints = 1500; // datapoints = 0 = auto

//    sprintf(step,"--step=%d",EtsdInfo.intervalTime);
    if ('\0' == *fName)
        sprintf(rrdValues, "create /path/to/rrd --step=%d", EtsdInfo.intervalTime); 
    else 
       sprintf(rrdValues, "%s --step=%d", fName, EtsdInfo.intervalTime); 
    
    /* first build the DS names, etc. */
    for (lp=0; lp<channels; lp++){         // create Data Sources (DS)
        if(64>(chanMap[lp])){   
             switch ETSD_Type(chanMap[lp]) {
            case 10:             // AutoScaling
                maxVal = 524287;
                break;
            case 9:     // Extended Full Stream
                maxVal = 262143;
                break;
            case 8:     // Full Stream
                maxVal = 65535;
                break;
            case 5:     // Extended Half Stream
                maxVal = 1023;
                break;
            case 4:     // Half Stream
                maxVal = 255;
                break;
            case 3:     // Extended Quarter Stream
                maxVal = 63;
                break;
            case 2:     //  Quarter Stream
                maxVal = 15;
                break;
            case 1:     //  Two bit Stream
                maxVal = 3;
                break;
        }
            sprintf(tempVal, " DS:%s:%s:%d:0:%d", EtsdInfo.label[chanMap[lp]], CNT_bit(chanMap[lp])?"COUNTER":"GAUGE", EtsdInfo.intervalTime+2, maxVal);
            strcat(rrdValues, tempVal);
            cnt++;
        }
    }

    /* Then build the RR Archives */
    if(strcasestr(rraV[0],"auto")){
        if(!totalTime)              // if totalTime = unlimited
            totalTime = 86400*datapoints + 1;   // minimum amount of total time to insure creating RRA with 24 hour steps
            
        sprintf(tempVal, " RRA:AVERAGE:0.2:1:%d", 90000/EtsdInfo.intervalTime); //build enough datapoints to hold 25 hrs worth of full resolution data
        strcat(rrdValues, tempVal);
        rraSteps[0]=90000/datapoints;   
        for (lp=1;lp<6;lp++){
            if (datapoints*rraSteps[lp-1]<totalTime){
                sprintf(tempVal, " RRA:AVERAGE:0.2:%d:%d", rraSteps[lp]/EtsdInfo.intervalTime, datapoints);
                strcat(rrdValues, tempVal);
                cnt++;
            } else
                break;
        }
    }
    if ('\0' == *fName){         // if there is no filename then output string
        Log("rrdtool create /path/to/file.rrd %s \n", rrdValues);
    } else {                       // otherwise create the file
        rrdParams=malloc((cnt+1)*sizeof(char *));
        ptr=rrdValues;
        rrdParams[0]="rrdCreate";
        rrdParams[1]=ptr;
        for(lp=2; lp<cnt;lp++){
            ptr=strchr(ptr, ' ');
            *ptr++='\0';
            rrdParams[lp]=ptr;
        }
        rrdParams[cnt]=NULL;
        if ( rrd_create(cnt, rrdParams) ){
            Log("RRD Create error: %s\n", rrd_get_error());
        }
        /*  else rrd_create was successful  */
    }
}


// etsdtool recover /path/to/etsd.tsd /path/to/rrd S[tart]=start_time E[nd]=end_time n=#_of_datapoints[default 1500] 
//  M[ap] = a[ll]|e[tsd]|RRD:ETSD stream to save to DS#1;etsd# for RRD DS2; RRD DS3; etc  zero = don't save to RRD
//      Map is optional unless using an existing RRD database that doesn't match the chan layout of ETSD database
//      or you wish to build an RRD with fewer DS than the number of ETSD streams
//      Note: default is only channels marked as rrd are saved to RRD and saved in the same order as they appear in ETSD
// R[ra] = s:[seconds per PDP]:[number of PDPs]   m:[minutes per PDP]:[#of PDPs]   h:[hours per PDP]:[# of PDPs] 
//      Note: RRA= specifies the RR Archives and is only used when creating a new RRD database
int recoverRRD(int argc, char *argv[], char *rrdFName){
    uint8_t lp, lp2, idx, etsdChan, rraC=0, dsAll=0, seRel=0, dsMapped=0, *chanMap; 
    char *etsdFile=argv[2];
    char *ptr, *ptr2, *archVal;
    char tempVal[15];
    char rrdValues[EtsdInfo.channels*11+50];
    char *rrdParams[4] = { "rrdupdate", rrdFName, rrdValues, NULL };
    char *rraV[10];         // Pete is this enough arguements to hold RRA definitions?

//    uint32_t etsdValues[EtsdInfo.channels]; // do I need this?  Just a temporary place to store values from etsd in channel order.
    uint32_t timeStamp, totalTime, sector, datapoints=1500, sourceDP, start=0, end=0;
    int data, pdp;
    time_t now=time(NULL);
    
    chanMap=malloc(EtsdInfo.channels);
    for (lp=0; lp<EtsdInfo.channels; lp++){
        chanMap[lp]=255;        //default all channels to 'not mapped'
    }
    
    
    for(lp=0; lp< argc; lp++){
        if(ptr=strchr(argv[lp],'=')){
            ptr2=ptr;
            ptr++;
            switch(*argv[lp]) { // looking at first character of arguement
                case 's':
                case 'S':
                    start = etsdParseTime(ptr);
                    if(strcasestr(ptr,"end")){  // start time relative to end time
                        if(!end){               // if no end time yet
                            if(seRel) {         // this means end time was ALSO waiting on start time
                                ErrorCode |= E_STARTSTOP;
                                ELog(__func__, 0);
                                return -1;
                            }
                            seRel = 1;
                        } else {
                            if( 0 > start) {        // if start is negative
                                start = end + start;    // note: start = end - timevalue
                                seRel = 0;
                            } else {                // else start is positive or zero
                                ErrorCode |= E_STARTSTOP;
                                ELog(__func__, 0);
                                return -1;                            }
                        }    
                    } else if(strcasestr(ptr,"start")){  //  Error Invalid, start can't be relative to start
                        ErrorCode |= E_STARTSTOP;
                        ELog(__func__, 0);
                        return -1;
                    } else {
                        if(seRel)
                            end = start + end;
                    }
                    break;
                case 'e':
                case 'E':
                    end = etsdParseTime(ptr);
                    if(strcasestr(ptr,"start")){
                        if(!start) {
                            if(seRel) {         // this means start time was ALSO waiting on end time
                                ErrorCode |= E_STARTSTOP;
                                ELog(__func__, 0);
                                return -1;
                            }
                            seRel = 1;
                        }else{
                            if( 0 < end) {        // if end is postive
                                end = start + end;  // note: end = start + timevalue
                                seRel = 0;
                            } else {                // else start is negative or zero
                                ErrorCode |= E_STARTSTOP;
                                ELog(__func__, 0);
                                return -1;                                
                            }
                        }   
                    } else if(strcasestr(ptr,"end")){  //  Error Invalid, end can't be relative to end
                        ErrorCode |= E_STARTSTOP;
                        ELog(__func__, 0);
                        return -1;
                    } else {
                        if(seRel)
                            start = start + end;
                    }
                    break;
                case 'm':
                case 'M':
                    if(strcasestr(ptr,"all")){
                        dsAll=1;
                        dsMapped = EtsdInfo.channels;
                        for(lp2=0;lp2<EtsdInfo.channels;lp2++)
                            chanMap[lp2]=lp2;     
                    } else if(!strcasestr(ptr,"etsd")){
                        do { 
                            ptr2++;
                            if (etsdChan = atoi(ptr2))
                                etsdChan--;
                            else
                                etsdChan = 255;
                            chanMap[dsMapped++] = etsdChan;
                        } while(ptr2=strchr(ptr2, ';'));        // Pete program will fail if mapping to an RRD with more channels than the ETSD
                    }                                           // fix by changing this to a counter and realloc()
                    break;
                case 'n':
                case 'N':
                    datapoints = atoi(ptr);
                    break;
                case 'a':
                case 'A':
                    // pass through arguement intact as rra statement
                    break;
                case 'r':
                case 'R':
                    idx = 0;
                    while(ptr2=strchr(ptr2, ':')){
                        ptr2++;
                        idx++;
                        ptr2=strchr(ptr2, ':'); // each archive spec has two ':' 
                        ptr2++;
                    }
                    //archVal = malloc(idx * ??? );
                    //rrA(ptr, &archVal);

                    while(ptr2=strchr(ptr, ':')) { 
                        ptr2++;
                        pdp = atoi(ptr2);
//                        switch(
                        
                    }                     
                    break;
            }
        }
    }
    if (end>now)
        end=now;

    if (!dsMapped++){                               // default is to map only etsd channels marked as 'rrd'
        for(lp2=0;lp2<EtsdInfo.channels;lp2++){
            if (RRD_bit(lp2))
                chanMap[dsMapped++]=lp2;     
        }
        
    }
    totalTime = end - start;
        
    //pete  build rrd if needed
    rraV[rraC++] = "auto";  // pete for test purposes only

    buildRRD(rrdFName, dsMapped, chanMap, totalTime, datapoints, rraC, rraV);
 
// once we have an RRD file, we can populate it with data from the ETSD file
    if (0 > (sector=etsdFindBlock(start))){ // find block with start time 
        ELog(__func__, 0);
        return -1;
        // Pete for now just exit, in future search archived ETSD files
    }
    timeStamp = PBlock.longD[0];
    
    while(timeStamp < end){
        for(lp2=0;lp2 <= EtsdInfo.blockIntervals; lp2++){
            sprintf(rrdValues,"%u",timeStamp+lp*EtsdInfo.intervalTime);
            for(lp=0;lp<EtsdInfo.channels;lp++){
                data=readChan(lp2, lp);
                if(dsAll||RRD_bit(lp)){
                    if (lp2) {  // interV 0, read registers if available
                        if (E_DATA & ErrorCode){
                            sprintf(tempVal,":U");
                        } else {
                            if(CNT_bit(lp)) {
                                sprintf(tempVal,":%u", lastReading[lp]);
                            }else {
                                sprintf(tempVal,":%u", data);
                            }
                        }
                        strcat(rrdValues, tempVal);
                    }
                }
            }
            if (NULL != rrdFName){
                //if (LogLvl>2)
                  //  Log("<5> RRD data = %s\n", rrdValues);
                optind = opterr = 0; // Because rrdtool uses getopt() 
                rrd_clear_error();
                rrd_update(3, rrdParams);  
            }
        }
        timeStamp=etsdTimeS(++sector);
    }
}

//cmdArray: {Cmd, ChanName, Start, Stop, val}  Cmd, ChanName and Start are used by all commands. Minimum 3 arguments, maximum 5
uint32_t etsdQ(char *cmd, uint8_t chan, uint32_t start, uint32_t stop, uint32_t val){
   /* nada */ 
    
}


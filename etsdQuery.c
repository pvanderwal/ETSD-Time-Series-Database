/*************************************************************************
etsdQuery .c library.  Used to create an ETSD time series database, convert an ETSD to RRD database, and display info on an ETSD. 

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
#include <inttypes.h>
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

#define EARLIEST_TIME 1000187190    // RRD seems to have a minimum start time, any time prior to that defaults to midnight of the current day 
                                    // I can't be botherd to figure exactly what it is, so I picked an abitrary value.

// Pete figure out a way to handle fractions, float won't work because it screws up epoch values 
// converts strings decribing +/- time and returns the number of seconds (positive or negative) they represent
// valid forms are 10s, -356S, 4hours, -12h, 3minutes, etc.
// converted string value must fit within a 32bit integer
// Returns total +/- seconds or sets ErrorCode and returns zero
int32_t parseT(char *val){
    char *ptr=strpbrk(val,"sSmMhHdDyY");
    char *ptr2;     
    long secs=strtol(val, &ptr2, 10);
    uint8_t err=0;
    while(strchr(ptr2, ' '))
        ptr2++;     // remove leading spaces
    
    if(ptr && ptr==ptr2){ // if the first character after the digits matchs our strpbrk() list...
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
        }
    } else {
        if (48 > val[strlen(val)-1] || val[strlen(val)-1] > 57 ) { // if last character is not a number
            ErrorCode = E_DATE;
            secs = 0;  //calling function needs to check ErrorCode 
            err=1;
        }
    }
    if ( 2147483647<secs || -2147483648>secs){  // resulting value doesn't fit in an 32bit int
        ErrorCode |= E_ARG;
        secs = 0;  //calling function needs to check ErrorCode 
        err=1;
    } 
    if (err){
        ELog(__func__, 0);  // log the error
    }
    return secs;
}

// returns (etsd style) epoch time represented by dt or zero if there was an error parsing dt
// or zero and sets ErrorCode
uint32_t etsdParseTime(char *dt){
    char *ptr, *ptr2;
    time_t now=time(NULL);
    struct tm *t=localtime(&now);
    long rval=0;
    uint8_t err=0;

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
// Log("etsdParseTime called with %s rval = %d\n",dt, rval);
    if(strcasestr(dt,"now")){
        if( strchr(dt,'+')){         // date/time in the future is not valid
            ErrorCode = E_DATE;
            err = 1;
            rval = 0;
        }
        rval = now + rval;
    } else if(strcasestr(dt,"mid")){
        t->tm_hour = 0;
        t->tm_min = 0;
        t->tm_sec = 0;
        rval = mktime(t) + rval;          
    } else if(strcasestr(dt,"begin")){
        rval = etsdTimeS(1);    
        if (0>rval && ErrorCode){
            err = 1;
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
                err = 1;
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
            if(!rval)
                rval = parseT(dt) ;
        }
    }
    if ( 4294967295<rval || 0>rval){  // resulting value doesn't fit in an uint32_t
        ErrorCode |= E_ARG;
        rval = 0;  //calling function needs to check ErrorCode 
        err = 1;
    }
    if(err){
        ELog(__func__, 0);  // log error(s)
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


//  totalTime(zero = unlimited), datapoints(0 = automatic), rraC= number of ellements in the array rraV[](first value = type)
void buildRRD(char *fName, uint8_t channels, uint8_t chanMap[], uint32_t totalTime, uint32_t datapoints, int rraC, char *rraV[]){
    uint8_t lp, cnt=5;
    char tempVal[60];
//    char step[15];
    char rrdValues[EtsdInfo.channels*40+175+strlen(fName)]; // needs to be large enough for worse case
 //   char *rrdParams[4] = { fName, step, rrdValues, NULL };
    char **rrdParams;
    char *ptr;
    uint32_t maxVal, maxDP, rraSteps[6]={1,60,420,1800,21600,86400}; //1 minute steps for daily graph, 7 minute for weekly, 30 minute for monthly, 6 hour for annual, 1 day for ~4 years

    if (!datapoints)
        datapoints = 1500; // datapoints = 0 = auto

    if ('\0' == *fName)
        sprintf(rrdValues, "create /path/to/rrd --start=0 --step=%d", EtsdInfo.intervalTime); 
    else 
       sprintf(rrdValues, "%s --start=EARLIEST_TIME --step=%u", fName, EtsdInfo.intervalTime); 
    
    /* first build the DS names, etc. */
    for (lp=0; lp<channels; lp++){         // create Data Sources (DS)
        if(128>(chanMap[lp])){   
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
        optind = opterr = 0; // Because rrdtool uses getopt() 
        rrd_clear_error();
        if ( rrd_create(cnt, rrdParams) ){
            Log("RRD Create error: %s\n", rrd_get_error());
            exit(1);
        }
        /*  else rrd_create was successful  */
    }
}


// etsdtool recover /path/to/etsd.tsd /path/to/rrd S[tart]=start_time E[nd]=end_time n=#_of_datapoints[default 1500] 
//  M[ap] = a[ll]|e[tsd]|RRD:ETSD stream to save to DS#1;etsd# for RRD DS2; RRD DS3; etc  zero = don't save to RRD
//      Map is optional unless using you wish to build an RRD with fewer DS than the number of ETSD streams
//      Note: default is only save to RRD those channels in the ETSD that are marked as 'rrd' and save them in the same order as they appear in ETSD
// R[ra] = s:[seconds per PDP]:[number of PDPs]   m:[minutes per PDP]:[#of PDPs]   h:[hours per PDP]:[# of PDPs] 
//      Note: RRA= specifies the RR Archives and is only used when creating a new RRD database
int32_t recoverRRD(int argc, char *argv[], char *rrdFName, uint8_t overwrite){
    uint8_t lp, lp2, idx, txt=0, tspec=1, etsdChan, err=0, rraC=0, dsAll=0, seRel=0, dsMapped=0, *chanMap; 
    uint8_t last=0, first=0, lastLoop;
    char *etsdFile=argv[2];
    char *ptr, *ptr2, *archVal;
    char tempVal[15];
    char rrdValues[EtsdInfo.channels*11+50];
    char *rrdParams[4] = { "rrdupdate", rrdFName, rrdValues, NULL };
    char *rraV[10];         // Pete is this enough arguements to hold RRA definitions?
    uint32_t timeStamp, endTime, totalTime, sector, datapoints=1500, sourceDP, start=0, end=0, lastTime=EARLIEST_TIME;
    int32_t data, pdp, status;
    time_t now=time(NULL);
    struct tm *info;
    FILE *fd;
    
    if( strcasestr(rrdFName, ".txt") )
        txt=1;
    if(overwrite){
        if (fd = fopen(rrdFName, "r")) { //test to see if file already exists
            fclose(fd);
            status = remove(rrdFName);
            if (status == 0){
                Log("<5>%s deleted successfully.\n", rrdFName);
            }else{
                Log("<3>Unable to delete %s\n", rrdFName);
                perror("Following error occurred");
                ErrorCode |= E_DELETE;
                ELog(__func__, 0);
                return DATA_INVALID;
            }
        }   
    }
    chanMap=malloc(EtsdInfo.channels);
    for (lp=0; lp<EtsdInfo.channels; lp++){
        chanMap[lp]=255;        //default all channels to 'not mapped'
    }
    
    
    for(lp=0; lp< argc; lp++){
        if(ptr=strchr(argv[lp],'=')){
            ptr2=ptr;
            ptr++;
            switch(*argv[lp]) { // looking at first character of arguement
                case 'a':
                case 'A':
                    // pass through arguement intact as rra statement
                    break;
                case 'e':
                case 'E':
                    end = etsdParseTime(ptr);
                    if(strcasestr(ptr,"start")){
                        if(!start) {
                            if(seRel) {         // this means start time was ALSO waiting on end time
                                ErrorCode |= E_STARTSTOP;
                                err=1;
                            }
                            seRel = 1;
                        }else{
                            if( 0 < end) {        // if end is postive
                                end = start + end;  // note: end = start + timevalue
                                seRel = 0;
                            } else {                // else start is negative or zero
                                ErrorCode |= E_STARTSTOP;
                                err=1;                                
                            }
                        }   
                    } else if(strcasestr(ptr,"end")){  //  Error Invalid, end can't be relative to end
                        ErrorCode |= E_STARTSTOP;
                        err=1;
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
                case 'r':
                case 'R':
                    idx = 0;
                    while(ptr2=strchr(ptr2, ':')){
                        ptr2++;
                        idx++;
                        ptr2=strchr(ptr2, ':'); // each archive spec has two ':' 
                        ptr2++;
                    }
                    while(ptr2=strchr(ptr, ':')) { 
                        ptr2++;
                        pdp = atoi(ptr2);
                    }                     
                    break;
                case 's':
                case 'S':
                    start = etsdParseTime(ptr);
                    if(strcasestr(ptr,"end")){  // start time relative to end time
                        if(!end){               // if no end time yet
                            if(seRel) {         // this means end time was ALSO waiting on start time
                                ErrorCode |= E_STARTSTOP;
                                err=1;
                            }
                            seRel = 1;
                        } else {
                            if( 0 > start) {        // if start is negative
                                start = end + start;    // note: start = end - timevalue
                                seRel = 0;
                            } else {                // else start is positive or zero
                                ErrorCode |= E_STARTSTOP;
                                err=1;                            }
                        }    
                    } else if(strcasestr(ptr,"start")){  //  Error Invalid, start can't be relative to start
                        ErrorCode |= E_STARTSTOP;
                        err=1;
                    } else {
                        if(seRel)
                            end = start + end;
                    }
                    break;
                case 't':
                case 'T':

                    if(strcasestr(ptr,"epo")){
                        tspec=0;
                    }else if(strcasestr(ptr,"iso")){
                        tspec=2;
                    }else if(strcasestr(ptr,"us")){
                        tspec=1;
                    }
                    break;
            }
            if(err){
                ELog(__func__, 0);
                return DATA_INVALID;
            }
        }
    }
    if(!end || end>now)
        end=now;

    if (!dsMapped++){                               // default is to map only etsd channels marked as 'rrd'
        for(lp2=0;lp2<EtsdInfo.channels;lp2++){
            if (RRD_bit(lp2))
                chanMap[dsMapped++]=lp2;     
        }
        
    }
    
    if( !(sector=etsdFindBlock(end)) ){
        etsdRW("r", -1);        // Pete check for errors
        last = VALID_INTERVALS;
        endTime = TIME_STAMP;
        end=TIME_STAMP+VALID_INTERVALS*EtsdInfo.intervalTime;
    } else {
        last = (end-TIME_STAMP)/EtsdInfo.intervalTime;
        if (last<VALID_INTERVALS){
            last++;
        } else {
            if (etsdTimeS(sector++)){
                last=1;
            } else {
                etsdTimeS(--sector);
                last=VALID_INTERVALS;
                end=TIME_STAMP+VALID_INTERVALS*EtsdInfo.intervalTime; 
            }
        }
    }
    endTime = TIME_STAMP;

    if(!start){
        sector=1;
        start=etsdTimeS(1);
    } else {
        sector=etsdFindBlock(start);
        if (!sector) {
            sector=1;
            start=etsdTimeS(1);
        }
    }
    timeStamp=TIME_STAMP;

    if(timeStamp < start){
        first = (start-timeStamp)/EtsdInfo.intervalTime;
        if(first){
            first--;
        } else {
            if(1<sector){
                sector--;
                timeStamp=etsdTimeS(sector);
                first=VALID_INTERVALS;
            }
        }
    } else {    //timeStamp === start
        if(1<sector){
            sector--;
            timeStamp=etsdTimeS(sector);
            first=VALID_INTERVALS;
        } else {
            first=0;
        }
    }

    ErrorCode &= (E_BEFORE | E_AFTER);   // Pete check for other errors before clearing?
    
    totalTime = end - start;
        
    if(txt){
        fd = fopen(rrdFName, "w");  //Pete add error checking
    }else{
        // Pete  build rrd 
        rraV[rraC++] = "auto";  // Pete for test purposes only
        buildRRD(rrdFName, dsMapped, chanMap, totalTime, datapoints, rraC, rraV);
    }
    
    while(timeStamp <= endTime){
        if(timeStamp == endTime){
            lastLoop = last;
        } else {
            lastLoop = VALID_INTERVALS;
        }
        if( lastTime > timeStamp ){  
            Log("Error!! Bad timestamp: %u in %s, previous timestamp was %u\n", timeStamp, rrdFName, lastTime);
            exit(1);
        }
        lastTime = timeStamp;
        for(lp=first;lp <= lastLoop; lp++){
            if(txt && tspec){
                now=timeStamp+lp*EtsdInfo.intervalTime;
                info=localtime(&now);
                if(1==tspec){
                    strftime(rrdValues,19,"%D %T ", info);     // American Style
                }else{
                    strftime(rrdValues,21,"%F %T ", info);     // ISO style
                }
            }else{
                sprintf(rrdValues,"%u",timeStamp+lp*EtsdInfo.intervalTime);
            }
            for(lp2=0;lp2<EtsdInfo.channels;lp2++){
                data=readChan(lp, lp2);
                if(dsAll||RRD_bit(lp2)){
                    if (lp) {  // interV 0, read registers if available
                        if (E_DATA & ErrorCode){
                            ErrorCode &= ~E_DATA;
                            sprintf(tempVal,":U");
                        } else {
                            if(CNT_bit(lp2)) {
                                sprintf(tempVal,":%u", lastReading[lp2]);
                            }else {
                                sprintf(tempVal,":%d", data);
                            }
                        }
                        strcat(rrdValues, tempVal);
                    }
                }
            }
            if (NULL != rrdFName){
                if (lp){
                    if(txt){
                        fprintf(fd, "RRD data = %s\n", rrdValues);
                    }else{
                        optind = opterr = 0; // Because rrdtool uses getopt() 
                        rrd_clear_error();
                        if ( rrd_update(3, rrdParams) ){
                            Log("RRD Update error: %s\n", rrd_get_error());
                            Log("When using: %s from sector %d\n", rrdValues, sector);
                            err++;
                        } else {
                            if(err)
                                err--;
                        }
                    }
                }
            }
        }
        if(50<err){
            ErrorCode |= E_RRD; // excessive rrd errors, try to avoid filling the log with thousands of error messages
        }
        if(!(timeStamp=etsdTimeS(++sector))){
            if(E_EOF & ErrorCode){
                ErrorCode &= ~E_EOF;
                break;
            } else {
                ELog(__func__, 1);
                return DATA_INVALID;        
            }
        }
        first=0;
    }
    if(err){
        ELog(__func__, 0);
        return DATA_INVALID;
    }    
    if(txt)
        fclose(fd);
    return 0;
} // recoverRRD

// returns the channel number of chanName, or 255 if not found
uint8_t etsdChanNum(char *chanName){
    uint8_t lp;
    if(strlen(chanName)){  // return not found if chanName = ""
        for(lp=0;lp<EtsdInfo.channels;lp++){
            if(strcasestr(EtsdInfo.label[lp], chanName)){
                return lp;
            }
        }
    }
    return 255;
}

uint32_t etsdVAT(uint8_t chan, uint32_t tTime){
    uint32_t data, timeStamp, sector = etsdFindBlock(tTime);

    if(sector){
        timeStamp = TIME_STAMP;
    } else { // can't find tTime in ETSD
        data=DATA_INVALID;
    }
    return data;
}
    
// note: returning int64_t because unit32_t maxes out Total at 1,193 kWh
// Total is meaningless with Guage channels
int64_t etsdAMT(char *cmd, uint8_t chan, uint32_t start, uint32_t end){
    int32_t data, Max=0, Min = 2147483647;
    int64_t Tot; 
    uint32_t timeStamp, prevReading, lastTime=EARLIEST_TIME, endTime, bump=0, intvCnt=0, sector;
    uint8_t last=0, first=0, lastLoop, lp;
    
    if(!(EtsdInfo.channels)){
        ErrorCode = E_NO_ETSD;
        ELog(__func__, 1);
        exit(1);
    }
    
    
    if( !(sector=etsdFindBlock(end)) ){
        etsdRW("r", -1);        // Pete check for errors
        last = VALID_INTERVALS;
        endTime = TIME_STAMP;
        end=TIME_STAMP+VALID_INTERVALS*EtsdInfo.intervalTime;
    } else {
        endTime = TIME_STAMP;
        last = (end-endTime)/EtsdInfo.intervalTime;
    }
    
    if( !(sector=etsdFindBlock(start)) ){
        sector=1;
        start=etsdTimeS(sector);
    }
    timeStamp=TIME_STAMP;

    if(timeStamp < start){
        first = (start-timeStamp)/EtsdInfo.intervalTime;
        for(lp=0;lp<first;lp++){
            readChan(lp, chan); // populate lastReading 
        }
        // Pete  need to add interpolation if start is between two readings
    } else {
        first=0;
        readChan(0, chan);
    }

    ErrorCode &= ~E_DATA;  // Pete need to handle error before clearing??
    
    if(CNT_bit(chan)){
        Tot=lastReading[chan];      
        prevReading = Tot;
        
        if(DATA_INVALID==Tot){
            // Pete need to figure a fix for no valid last reading
        }
    } else {
        Tot=0;  // Pete will this work for figuring average on Guage channels?
        prevReading = DATA_INVALID;
    }

    if(CNT_bit(chan))
        intvCnt = (end-start)/EtsdInfo.intervalTime;  // Pete ECM-1240 clock runs ~10 a day fast
    
    while(timeStamp <= endTime){
        if(timeStamp == endTime){
            lastLoop = last;
        } else {
            lastLoop = VALID_INTERVALS;
        }
        if( lastTime > timeStamp ){  
            Log("Error!! Bad timestamp: %u previous timeStamp was %u\n", timeStamp, lastTime);
            exit(1);
        }
        lastTime = timeStamp;
        for(lp=first;lp <= lastLoop; lp++){
            data=readChan(lp, chan);
            if(lp){
                if( ErrorCode & E_DATA ){
                    ErrorCode &= ~E_DATA;
                } else {
                    if(data<Min){
                        Min=data;
                    }
                    if(data>Max){
                        Max=data;
                    }
                    if(!CNT_bit(chan)){
                        Tot += data;
                        intvCnt++;
                    }
                    if (lastReading[chan] < prevReading){  
                        prevReading = lastReading[chan]; 
                        bump++;
                    }
                } 
            }
        }
        if(!(timeStamp=etsdTimeS(++sector))){
            if(ErrorCode & E_EOF){
                ErrorCode = 0;
                break;
            } else {
                ELog(__func__, 1);
            }
        }
        first=0;
    }
    if(CNT_bit(chan)){
        if(lastReading[chan]<Tot)
            bump--;
        Tot=(lastReading[chan]-Tot)+bump*4294967296;
    }
    if (strcasestr(cmd, "ave")) {
        Tot /= intvCnt;
    } else if (strcasestr(cmd, "min")) {
        Tot = Min;
    } else if (strcasestr(cmd, "max")) {
        Tot = Max;
    }  // defaults to returning Total
    if(CNT_bit(chan) && !(strcasestr(cmd, "tot")) ){
        Tot = (Tot+((EtsdInfo.intervalTime+1)/2))/EtsdInfo.intervalTime;  //Round to nearest whole number
    }
    return Tot;
        
} // end etsdAMT


// 
uint32_t etsdQ(int argc, char *argv){
   /* nada */ 
    
}


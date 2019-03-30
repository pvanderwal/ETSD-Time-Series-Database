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
//#include <ctype.h>      // for isalnum()

#include "errorlog.h"
#include "etsd.h"
#include "etsdRead.h"
#include "etsdQuery.h"

#ifndef EARLIEST_TIME
#define EARLIEST_TIME 1000187190    // An abitrary value, it's unlikely that ETSD will be used prior to this date/time.
#endif

// Pete figure out a way to handle fractions, float won't work because it screws up epoch values 
// converts strings decribing +/- time and returns the number of seconds (positive or negative) they represent
// valid forms are 10s, -356S, 4hours, -12h, 3minutes, etc.
// converted string value must fit within a 32bit integer
// Returns total +/- seconds or sets ErrorCode and returns zero
int32_t parseT(char *val){
    char *ptr=strpbrk(val,"sSmMhHdDyY");
//    char *ptr2;     
//    long secs=strtol(val, &ptr2, 10);
    double secs = atof(val);
    uint8_t err=0;
//    while(strchr(ptr2, ' '))
//        ptr2++;     // remove leading spaces
    
//    if(ptr && ptr==ptr2){ // if the first character after the digits matchs our strpbrk() list...
    if(ptr){ // did strpbrk() find a matching char?
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
        
//       if (48 > val[strlen(val)-1] || val[strlen(val)-1] > 57 ) { // if last character is not a number
        if ( (uint8_t)val[strlen(val)-1] - 48 > 9 ) { // if last character is not a number
            ErrorCode = E_DATE;
            secs = 0;  //calling function needs to check ErrorCode 
            err=1;
        }
    }
//    if ( 2147483647<secs || -2147483648>secs){  // resulting value doesn't fit in an 32bit int
    if ((uint64_t)(secs+2147483648.5)>4294967295){  // resulting value doesn't fit in an 32bit int
        ErrorCode |= E_ARG;
        secs = 0;  //calling function needs to check ErrorCode 
        err=1;
    } 
    if (err){
        ELog(__func__, 0);  // log the error
    }
    return (int32_t)secs;
}

// returns ETSD timestamp represented by dt or zero if there was an error parsing dt
// or zero and sets ErrorCode
uint32_t etsdParseTime(char *dt){
    char *ptr, *ptr2;
    time_t now=time(NULL);
    struct tm *t=localtime(&now);
    long rval=0;
    uint8_t err=0;
//    char junk[35];
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
    rval -= ETSD_EPOCH;
//    if ( 4294967295<rval || 0>rval){  // resulting value doesn't fit in an uint32_t
    if ( 4294967295<(uint64_t)rval ){  // resulting value doesn't fit in an uint32_t
        ErrorCode |= E_ARG;
        rval = 0;  //calling function needs to check ErrorCode 
        err = 1;
    }
    if(err){
        ELog(__func__, 0);  // log error(s)
    }
    return (uint32_t)rval;
}

// (following are optional ->) t=(interval time, default 10s) r=(rrd) d=(datapoints default 25hrs worth of interval Time) u=(unit id default 0) x=(extra data bytes default 0)
// /path/to/new/file.tsd (optional)[/path/to/new/file.rrd] (unit id)[U=2] (interval time)[T=10s|T=10m|etc] (If blank defaults to 10 seconds)
// Name:Stream Type[1-15]:[C|COUNTER|G|GUAGE](default C):SourceType[E|M]SrcChan[1-63]:Save Register[S]:Signed Integer[I]:Save to RRD[R] <space>
// 
// /var/db/garage.tsd /var/db/garage.rrd u=2 T=10s Water_Heater:6:E4:R:S MiniSplit:8:E3:R:S
//
//#define labelSize block[8]

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

// Returns value stored at tTime
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
int64_t etsdAMT(char *cmd, uint8_t chan, uint32_t start, uint32_t end){
    int32_t data, Max=0, Min = 2147483647;
    // head & tail are seconds before/after first/last readings.  before & after are interpolated data from before/after first/last readings
    int64_t Tot; 
    uint32_t before=0,  after=0, head=0, tail=0, prevReading = 0, timeStamp, lastTime=EARLIEST_TIME, endTime, bump=0, intvCnt=0, sector;
    uint8_t last=0, first=0, shortBlock=0, lastLoop, lp;
    
    if(!(EtsdInfo.channels)){
        ErrorCode = E_NO_ETSD;
        ELog(__func__, 1);
        exit(1);
    }
    
    
    if( !(sector=etsdFindBlock(end)) ){
        etsdRW("r", -1);        // Pete check for errors
        last = VALID_INTERVALS-1;
        endTime = TIME_STAMP;
        end=endTime+(VALID_INTERVALS)*EtsdInfo.intervalTime;  // end = end of ETSD data
    } else {
        endTime = TIME_STAMP;
        last = (end-endTime)/EtsdInfo.intervalTime;
        if (VALID_INTERVALS==last){
            if(!(endTime = etsdFindBlock(endTime+VALID_INTERVALS*EtsdInfo.intervalTime + 1))){
                etsdRW("r", -1);        // Pete check for errors
                last = VALID_INTERVALS-1;
                endTime = TIME_STAMP;
                end=endTime+(VALID_INTERVALS)*EtsdInfo.intervalTime;  // end = end of ETSD data
            } else {
                data = readChan(1, chan);
                tail = TIME_STAMP+EtsdInfo.intervalTime - end;
            }
        } else {
            tail = end - (endTime+last * EtsdInfo.intervalTime);
            data = readChan(last+1, chan);
        }
        after = (data*tail + EtsdInfo.intervalTime/2)/EtsdInfo.intervalTime;
    }

    
    if( !(sector=etsdFindBlock(start)) ){  // Pete check and handle E_CODING error?
        sector=1;
        start=etsdTimeS(sector);  
    }
    // Pete check (adjusted?) start time and end time to make sure they are different.
    //   do we need code to handle them both being in the same sector?
    
     
    // Note: each stored reading covers the PREVIOUS interval.  I.e inv#1 is value from zero to 1
    timeStamp=TIME_STAMP;
    head=start-timeStamp;    
    if(head){
        first = (head)/EtsdInfo.intervalTime+1;   // first = first full reading after start
        if(first > VALID_INTERVALS){
            timeStamp=etsdTimeS(++sector);  // Pete test if sector is available?  see above start vs end
            first = 1;
            head = timeStamp - start ;
        } else {
            head -= (first-1)*EtsdInfo.intervalTime;
        }
        data = readChan(first, chan);
        before =( (data*head + EtsdInfo.intervalTime/2) / EtsdInfo.intervalTime );
        for(lp=0;lp<first;lp++){
            data=readChan(lp, chan); // populate LastReading[chan] 
        }
        Tot = LastReading[chan];        // what if not saving registers?
            
    } else {    // head=0
        Tot=readChan(0, chan);  // before=0;
    }
    
    if(DATA_INVALID==Tot){
        // Pete need to figure a fix for no valid last reading
    }
    
    if(! CNT_BIT(chan)){    // gauge channel   
        Tot=0;  
    }
    prevReading = Tot;
    
//fprintf(stderr,"Timestamp: %u Start: %u First: %u Total: %" PRId64 " Data: %d Head: %u Before: %u Tail: %u After: %u\n", timeStamp, start, first, Tot, data, head, before, tail, after); 

    ErrorCode &= ~E_DATA;  // Pete do I need to handle error before clearing??

    // Pete  Need to Check for ErrorCode==E_DATA, DATA_INVALID, source reset, VALID_INTERVALS < EtsdInfo.blockIntervals, and missing data
    //
    while(timeStamp <= endTime){
        if(timeStamp == endTime){
            lastLoop = last;
        } else {
            lastLoop = VALID_INTERVALS;
        }
//printf("\nTimestamp: %d :",timeStamp);
        if( lastTime > timeStamp ){  
            Log("Error!! Bad timestamp: %u previous timeStamp was %u\n", timeStamp, lastTime);
            exit(1);
        }

        for(lp=first;lp <= lastLoop; lp++){
            data=readChan(lp, chan);
            if(lp){
                intvCnt++;
                if( ErrorCode & E_DATA ){
                    ErrorCode &= ~E_DATA;  // Pete handle effect on Tot
                    if(!CNT_BIT(chan))
                        intvCnt--;          //only count valid intervals on non-counter streams
                } else {
//printf("%d :", data);
                    if(data<Min){
                        Min=data;
                    }
                    if(data>Max){
                        Max=data;
                    }
                    if(CNT_BIT(chan)){
                        if (LastReading[chan] < prevReading){  
                            prevReading = LastReading[chan]; 
                            bump++;
                        }
                    } else {
                        Tot += data;
                    }
                } 
            } else {  // Pete  Need to Check for source reset, VALID_INTERVALS < EtsdInfo.blockIntervals, and missing data
                uint32_t timeDiff = lastTime + (EtsdInfo.blockIntervals-shortBlock)*EtsdInfo.intervalTime;
                
                if (shortBlock){ // did the last block end early?
                   // timeDiff //
                }
                if (BLOCK_RESET){
                    
                }
                
                if(CNT_BIT(chan)){              // counter stream
                    if (data < prevReading){  //lp==0 then data==read registers
                        prevReading = data; 
                        bump++;
                    }
                }
                shortBlock = EtsdInfo.blockIntervals-VALID_INTERVALS;
   
            }
        }
        lastTime = timeStamp;
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
printf("Lastreading: %u before: %u after: %u intvCnt: %u \n", LastReading[chan], before, after, intvCnt);

    if (strcasestr(cmd, "min")) {
        Tot = Min;
    } else if (strcasestr(cmd, "max")) {
        Tot = Max;
    } else {
        if(CNT_BIT(chan)){
            if(LastReading[chan]<prevReading)
                bump--;
            
            Tot=(LastReading[chan]-Tot)+bump*4294967296 - before + after;

            before = end - start;
            after = intvCnt*EtsdInfo.intervalTime + tail - head;
            Tot = (Tot*before+1) / after;
            if (strcasestr(cmd, "ave")) {
                Tot = (Tot + before/2) / before;
            }
        } else {
            if (strcasestr(cmd, "ave")) {
                Tot /= intvCnt;
            }
        }
    }
  // defaults to returning Total

    return Tot;
        
} // end etsdAMT

/*
uint32_t etsdKS(char *fName, uint32_t start, uint32_t end, ETSD_KS *ks){
    //nada
    
}
 */ 
//  Cmd, ChanName, Start, Stop, val, val}  Cmd, ChanName and Start are used by all commands. Minimum 2 arguments, maximum 6
uint32_t etsdQ(int argc, char *argv){
   /* nada */ 
    
}


/*************************************************************************
etsdCmd.c command line tool.  
Used to create an ETSD time series database, convert an ETSD to RRD database, and display info on an ETSD. 

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
#define _GNU_SOURCE     // needed to keep strcasestr() fROM throwing an error

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>      // for isalnum()
#include <unistd.h>     //sleep() usleep()
#include "errorlog.h"
#include "etsdSave.h"
#include "etsdRead.h"
#include "etsdQuery.h"

#define AC_OFFSET 1040

#if BLOCKSIZE==512
#if MAX_CHANNELS>100
#undefine MAX_CHANNELS
#endif
#ifndef MAX_CHANNELS
#define MAX_CHANNELS 100
#endif
#else
#if MAX_CHANNELS>127
#undefine MAX_CHANNELS
#endif
#ifndef MAX_CHANNELS
#define MAX_CHANNELS 127
#endif    
#endif

/*
./etsdCmd create /var/db/garage.tsd /var/db/garage.rrd u=1 T=10s GarageMain:9:E1:r Servers:10:E2:r Fridge_Freezer:8:E5:r AC_Voltage:4:E11:G Water_Heater:8:E7:r TV_Entertainment:8:E6:r Evap_Solar:8:E8:r Mini_Split:8:E9:r

./etsdCmd create /var/db/garage.tsd u=1 T=10s GarageMain:9:E1:r Servers:10:E2:r Fridge_Freezer:8:E5:r AC_Voltage:4:E11:G Water_Heater:8:E7:r TV_Entertainment:8:E6:r Evap_Solar:8:E8:r Mini_Split:8:E9:r

if user specifies an rrd file, then automatically create rrd file, otherwise output rrdtool string that user can edit and/or use to create file
t= Interval Time [optional]last character S,M,H  for seconds, minutes, hours
u= UID
x= extra data

Channel Definitions =  ChanName:StreamType:Source&Channel:  I=Intiger(Signed) : G=Gauge(default counter) : R=RRD : S=Save Register(force on) <or> s=Register(force off)
32bit Registers are saved by default on 'counter' channels and off by degault on Gauge channels S/s can be used to change that behavior. 

Source&Channel E# = ECM chan #, M# = shared Memory chan #.
*/
// Must specify new ETSD file, will also create an rrd file if there is an RRD cmd line arguement, otherwise it won't create RRD
int32_t createETSD(int argc, char *argv[]){
    FILE *fd;
    uint8_t block[BLOCKSIZE] = {0};
//    uint8_t *bPtr;
//    char labels[550]= {'\0'};

    char *chanDef[MAX_CHANNELS];
    char *sorted[MAX_CHANNELS];
    char *ptr, *ptr2, *etsd, *rrd, *rraV[10];
    uint8_t rraC, channels=0, uID=0, registers=0, cdx=0, gauge, source, destination, *chanMap;
//pete create help variable
    uint16_t streams = 0, xData=0, intervals=0, intTime=10;
    int32_t lp, lp2, labelSize=0, idx=3; 
    union{
        uint32_t time;
        char TIME[5];
    } ht = {ETSD_HEADER}; // Ugly code but it works, Ensures that headers created will match ETSD_HEADER constant defined in etsdSave.h
    strncpy(block, ht.TIME, 4);  
    //strcpy(block, "ETSD");  
   
    if (1 < argc) {  // we have command line arguments

        etsd=argv[2];
        
        if (strchr(argv[3], '/') || strcasestr(argv[3], ".rrd")){
            rrd=argv[3];
            idx=4;
        } else {
            rrd="";
        }

        for ( lp = idx; lp < argc; lp++ ){
            if (ptr = strchr(argv[lp],'=')){ 
                switch(ptr++[-1]){   //*(ptr-1)
//don't need?                    case 'o':
//don't need?                    case 'O':
//don't need?                       output=ptr;
//don't need?                        break;
                    case 't':
                    case 'T':
                        intTime = atoi(ptr);
                        switch(ptr[strlen(ptr)-1]){
                            case 'm':
                            case 'M':
                                intTime *=60;
                                break;
                            case 'h':
                            case 'H':
                                intTime *=3600;
                                break;
                        }
                        break;
                    case 'u':
                    case 'U':
                        uID  = atoi(ptr)&3;
                        break;
                    case 'x':
                    case 'X':
                        xData = atoi(ptr);
                        break;
                }
            } else { // no equals sign so must be a channel definition
                chanDef[cdx++]=argv[lp];
            }
        }       
    } else {
        //printf("%s",help);
        exit (1);
    }

    for(lp2=10; lp2; lp2--){ // sort channels starting with large streams and work down to small streams
        for(lp=0;lp<cdx;lp++){
            ptr2=chanDef[lp];
            ptr=strchr(ptr2,':');  
            idx = ptr-ptr2;
            if (19<idx){
                    fprintf(stderr,"Error: Channel name is longer than 19 characters.\n");
                    exit(1);                
            }
            for (rraC=0;rraC<idx;rraC++){
                if(!isalnum(ptr2[rraC]) && '_'!=ptr2[rraC]){
                    fprintf(stderr,"Error, Bad channel name: %.*s\nChannel names can only contain alphanumeric characters and underscores '_' .\n", idx, ptr2);
                    exit(1);
                }
            }
            if(atoi(ptr+1) == lp2){
                sorted[channels++]=ptr2;
                labelSize += idx;
            }
        }
    }

    if (MAX_CHANNELS<channels){
        printf("Error: Total channels = %d, but the maximum allowes is %d.\n", channels, MAX_CHANNELS);
        exit(1);
    }
    
    if (labelSize > BLOCKSIZE-10-channels*2){
        fprintf(stderr,"Error. Labels exceed available space by: %d characters.\n", labelSize-(BLOCKSIZE-10-channels*3));
        exit(1);
    }
    idx = 10; // set index to start of channel data
    cdx=10+2*channels;
    for(lp=0;lp<channels;lp++){
        ptr=strchr(sorted[lp],':'); 
        memcpy(block+cdx,sorted[lp],ptr-sorted[lp]);
        cdx += ++ptr-sorted[lp];
        destination = atoi(ptr); 
        source=64;  
        if (destination <10){
            streams += destination;
        } else {
            streams += 8;
        }
 // RRD:G/C:Reg:SignedInt:StreamType(4bits)
        gauge = 0;
        destination |= 96; // default stream type = counter & saved Registers
        registers++;
        while(ptr=strchr(ptr,':')){
            switch(ptr++[1]){
                case 'e':
                case 'E':
                    source = atoi(ptr+1);
                    break;
                case 'g':
                case 'G':
                    destination &= 159;  //toggle off counter and save register
                    registers--;
                    gauge=1;
                    break;
                case 'i':
                case 'I':
                    destination |= 16;
                    break;
                case 'm':
                case 'M':
                    source = 128|atoi(ptr+1);
                    break;
                case 'r':
                case 'R':
                    destination |= 128;
                    break;
                case 's':
                    if(!gauge){
                        destination &= 223;  // turn off saved registers
                        registers--;
                    }
                    break;
                case 'S':
                    if(gauge){
                        destination |= 32;
                        registers++;
                    }
                    break;
            }
        }
        block[idx++]=source;
        block[idx++]=destination;
    }

    intervals = (BLOCKSIZE-8-xData-registers*4) / (streams/4.0);

    if(127<intervals){
        intervals = 127 ;
    }  
    
    printf(" Saving %d registers | channels = %d | intervals = %d | interval time = %d seconds | bytes per interval = %.2f\n Wasted space = %d bytes.\n\n", registers, channels, intervals, intTime, streams/4.0, (BLOCKSIZE-8-xData-registers*4-(int)((intervals*streams+3)/4)));

    block[4] = intervals<<7 | channels;  // little endian
    block[5] = uID<<6 | intervals>>1;
    block[6] = intTime & 255;
    block[7] = intTime>>8;
    block[8] = (labelSize+channels+1)/2;
    block[9] = xData;
    
    // Pete test to see if file already exists and prompt user to overwrite
 
    if (fd = fopen(etsd, "w")) {
        fwrite(&block, BLOCKSIZE, 1, fd);
        fclose(fd);
    } else{
        fprintf(stderr,"Error: Can't open %s for writing.\n", etsd);
        exit(1);
    }

    etsdInit(etsd, 1); // open newly created etsd so we can use it to create rrd

    chanMap=malloc(EtsdInfo.channels);
    for (lp=0; lp<EtsdInfo.channels; lp++){
        chanMap[lp]=255;        //default all channels to 'not mapped'
    }
 
    channels = 0;
    for(lp2=0;lp2<EtsdInfo.channels;lp2++){
        if (RRD_bit(lp2))
            chanMap[channels++]=lp2;     
    }

// Pete test to see if file already exists and prompt user to overwrite ?
    if(NULL != rrd && NULL == (fd=fopen(rrd, "r")) ) {    //pete  build rrd if needed
        rraC=0;
        rraV[rraC++] = "auto";  // pete for test purposes only
        buildRRD(rrd, channels, chanMap, 0, 0, rraC, rraV);
    } else {
        if(fd){
            printf("Caution!!  File %s already exists!\n", rrd);
            fclose(fd);
        }
    }
}

//fName, start= end= output=[input|etsd/raw]
int32_t queryETSD(int argc, char *argv[]){
    uint8_t lp, lp2, chan=0, seRel=0;
 //   uint8_t *chanMap;
    char *ptr, *ptr2, *cmd;
    uint32_t start=0, end=0;
    time_t now=time(NULL);

    if (1 < argc) {  // we have command line arguments

        if (etsdInit(argv[2], 1)){
            fprintf(stderr, "Error: can't open ETSD file %s \n", argv[2] );  // Log Error
            exit(1);
        }    
//        Line=malloc( EtsdInfo.channels*11);
//        chanMap=malloc(EtsdInfo.channels);
        for(lp=3; lp< argc; lp++){
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
                                    return -1;
                                }
                            }    
//                        } else if(strcasestr(ptr,"begin")){  
//                            start = etsdTimeS(1);
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
                    case 'c':
                    case 'C':
                        if(!(chan=atoi(ptr))){
                            if (255==(chan=etsdChanNum(ptr)) ){
                                printf("Invalid channel name or number Chan=%s\n",ptr);
                                exit(1);
                            }
                        }
                        // Pete add code for selective channel output
                        break;
                    case 'q':
                    case 'Q':
                        cmd=ptr;
                        break;
                }
            }
        }

        
        // done with argv, dump file
        if(!end || end>now)
            end=now;        
        if(!start){
            start==etsdTimeS(1);
            ELog(__func__, 1);
        }
        printf("Query result = %" PRId64 " \n", etsdAMT(cmd, chan, start, end));
    } else {
        printf(" The 'Query' command requires at least the name of the ETSD to dump, Q=Type(tot/ave/min/max), C=Channel name/number\n");
        printf("        S[tart]=<start time> and E[nd]=<end time>\n ");
        printf(" Example: etsdCmd query /path/to/file.tsd q=ave c=5 s=now-4h e=now\n");
        printf("          etsdCmd dump /path/to/file.tsd Channel=Main Query=Total Start=midnight-4days End=midnight+3h\n");
    }

}

int32_t examinETSD(int argc, char *argv[]){
    uint8_t lp, reg=0;
    char sType[10];
    if (etsdInit(argv[2], 1)){
        fprintf(stderr, "Error: can't open %s \n", argv[2] );  // Log Error
        //Pete for now just exit, in future search archived ETSD files
        exit(1);
    }

    printf("\n  Channel                  Source     Stream    Counter    Save   Save to  Save As\n");
    printf ( " #   Name                type  Chan    Type     /Guage   Register   RRD?   Integer?\n\n");
    for (lp=0;lp<EtsdInfo.channels;lp++){
        switch(ETSD_Type(lp)){
            case 10:
                sprintf(sType,"AutoScale");
                break;
            case 9:
                sprintf(sType,"ExtFull");
                break;
            case 8:
                sprintf(sType,"Full");
                break;
            case 5:
                sprintf(sType,"ExtHalf");
                break;
            case 4:
                sprintf(sType,"Half");
                break;
            case 3:
                sprintf(sType,"ExQuarter");
                break;
            case 2:
                sprintf(sType,"Quarter");
                break;
            case 1:
                sprintf(sType,"2 Bits");
                break;
        }
        printf("%2d %-20s  %s   %2u    %-9s  %-7s     %c        %c        %c\n", lp, EtsdInfo.label[lp], SRC_TYPE(lp)?"SHM":"ECM", SRC_CHAN(lp), sType, CNT_bit(lp)?"Counter":"Guage", REG_bit(lp)?'Y':'N', RRD_bit(lp)?'Y':'N', INT_bit(lp)?'Y':'N');
        if (REG_bit(lp))
            reg++;
    }
    printf("\nETSD has %u channels and is saving registers on %u of them.\n     Each block consists of %u intervals, each interval lasts %u seconds.\n\n", EtsdInfo.channels, reg, EtsdInfo.blockIntervals, EtsdInfo.intervalTime);
}



// main arguements Create Examin RecoverRRD
int main(int argc, char *argv[]){
    char *rrd, *nada, *ptr, **argp;
    char inp[20];
    int argpc;
    FILE *fd;
    LogLvl=5;
    if (1 < argc) {  // we have command line arguments
        if (!strcasestr(argv[2], ".tsd")) { 
            fprintf(stderr,"Error: second argument MUST be a ETSD file with a .tsd extention.\n You entered = %s.\n", argv[2]);
            exit(1);
        }        

            
        switch(argv[1][0]){   
            case 'c':
            case 'C':
                createETSD(argc, argv);
                break;
            case 'q':
            case 'Q':
                queryETSD(argc, argv);
                break;
            case 'e':
            case 'E':
                examinETSD(argc, argv);
                break;
            case 'r':
            case 'R':
                if(etsdInit(argv[2],1)){
                    ELog(__func__, 0);
                    exit(1);
                }
                rrd=argv[3];
                if ( strcasestr(rrd, ".rrd") || strcasestr(rrd, ".txt") ){
                    argp=argv+4;
                    argpc=argc-4;
                } else {
                    nada = malloc(strlen(argv[2]+5));
                    strcpy(nada, argv[2]);
                    ptr = strcasestr(nada, ".tsd");
                    if(ptr){
                        strcpy(ptr, ".rrd");
                    } else {
                        strcat(nada,".rrd");
                    }
                    rrd=nada;
                    argp=argv+3;
                    argpc=argc-3;
                }
                if (fd = fopen(rrd, "r")) { //test to see if file already exists
                    fclose(fd);
                    printf("File %s already exists.  Can't restore to an existing file.\nDelete existing file(y/n)? ", rrd);
                    scanf("%s",&inp);
                    if(strchr(inp,'y') || strchr(inp,'Y')){

                    } else {
                        exit(0);
                    }
                    
                }
                recoverRRD(argpc, argp, rrd, 1);
                break;
        }
    
    } else {
        //printf("%s",help);
        exit (1);
    }
}

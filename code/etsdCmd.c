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
#define _GNU_SOURCE     // needed to keep strcasestr() from throwing an error

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
//#include <ctype.h>      // for isalnum()
#include <unistd.h>     //sleep() usleep()
#include <termios.h>    //_getch*/
#include "errorlog.h"
#include "etsd.h"
#include "etsdRead.h"
#include "etsdQuery.h"

#define AC_OFFSET 1040

// returns zero(success) if string only contains alpha numerics and/or '_'
int stralnum(char *str, uint8_t size){
    uint8_t lp;
    for (lp=0;lp<size;lp++){
        uint8_t cmp = str[lp] - 48;  // valid values start at 48('0') and go to 122('z') with some invalid characters inbetween
        if (47==cmp) continue;  // '_'
        if (cmp<10) continue;   // numbers are now 0-9
        if (cmp>74) return 1;   // must be outside range 48-122(original)
        if (cmp<17) return 1;   // original values 58-64 are invalid
        if (cmp<43) continue;   // A-Z original values valid
        if (cmp<49) return 1;   // original values 91-96 are invalid
    }
    return 0;   // if we get to here, then string must be valid
}

char getch(){
    char buf=0;
    struct termios old={0};
    fflush(stdout);
    if(tcgetattr(0, &old)<0)
        perror("tcsetattr()");
    old.c_lflag&=~ICANON;
    old.c_lflag&=~ECHO;
    old.c_cc[VMIN]=1;
    old.c_cc[VTIME]=0;
    if(tcsetattr(0, TCSANOW, &old)<0)
        perror("tcsetattr ICANON");
    if(read(0,&buf,1)<0)
        perror("read()");
    old.c_lflag|=ICANON;
    old.c_lflag|=ECHO;
    if(tcsetattr(0, TCSADRAIN, &old)<0)
        perror ("tcsetattr ~ICANON");
    printf("%c\n",buf);
    return buf;
 }

/*
./etsdCmd create /var/db/garage.tsd /var/db/garage.rrd u=1 T=10s GarageMain:9:E1:r Servers:15:E2:r Fridge_Freezer:8:E5:r AC_Voltage:4:E11:G Water_Heater:8:E7:r TV_Entertainment:8:E6:r Evap_Solar:8:E8:r Mini_Split:8:E9:r

./etsdCmd create /var/db/garage.tsd u=1 T=10s GarageMain:9:E1:r Servers:15:E2:r Fridge_Freezer:8:E5:r AC_Voltage:4:E11:G Water_Heater:8:E7:r TV_Entertainment:8:E6:r Evap_Solar:8:E8:r Mini_Split:8:E9:r

if user specifies an rrd file, then automatically create rrd file, otherwise output rrdtool string that user can edit and/or use to create file
t= Interval Time [optional]last character S,M,H  for seconds, minutes, hours
u= UID
x= extra data

Channel Definitions =  ChanName:StreamType:Source&Channel:  I=Intiger(Signed) : G=Gauge(default counter) : R=RRD : S=Save Register(force on) <or> s=Register(force off)
32bit Registers are saved by default on 'counter' channels and off by degault on Gauge channels S/s can be used to change that behavior. 

Source&Channel E# = ECM chan #, M# = shared Memory chan #.
*/
// Must specify new ETSD file, if there is an RRD cmd line arguement it will create the rrd, 
// otherwise it will output the text for creating an rrd
int32_t createETSD(int argc, char *argv[]){
    FILE *fd;
    uint8_t order[]={1, 2, 3, 6, 7, 10, 11, 4, 5, 12, 8, 9, 13, 14, 15};  // channel sort reverse order
    uint8_t block[BLOCKSIZE] = {0};
    char *chanDef[MAX_CHANNELS];
    char *sorted[MAX_CHANNELS];
    char *ptr, *ptr2, *etsd, *rrd, *rraV[10];
    uint8_t rraC, channels=0, uID=0, registers=0, cdx=0, source, destination, *chanMap;
//pete create help variable
    uint16_t streams = 0, QS=0, xData=0, intervals=0, intTime=10;
    int32_t lp=0, lp2, labelSize=0, idx=3; 
    uint32_t Time = ETSD_HEADER;
    block[lp++]=Time;
    block[lp++]=Time>>8;
    block[lp++]=Time>>16;
    block[lp]=Time>>24;
    
/*
    for (lp=0;lp<5;lp++){
        block[lp]=Time;
        Time >>=8;
    }

    union{
        uint32_t time;
        char TIME[5];
    } ht = {ETSD_HEADER}; // Ugly code but works, creates a header that matches ETSD_HEADER constant defined in etsdSave.h
    strncpy(block, ht.TIME, 4);  
*/
   
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
//GarageMain:9:E1:r
    for(lp2=15; lp2; lp2--){ // sort channels starting with large streams and work down to small streams
        for(lp=0;lp<cdx;lp++){
            ptr2=chanDef[lp];
            ptr=strchr(ptr2,':');  
            idx = ptr-ptr2;
            if (19<idx){
                    fprintf(stderr,"Error: Channel name is longer than 19 characters.\n");
                    exit(1);                
            }
            if (stralnum(ptr2, idx)){
                fprintf(stderr,"Error, Bad channel name: %.*s\nChannel names can only contain alphanumeric characters and underscores '_' .\n", idx, ptr2);
                exit(1);
            }
            if(atoi(ptr+1) == order[lp2]){
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
        uint8_t gauge = 0;
        
        ptr=strchr(sorted[lp],':'); 
        memcpy(block+cdx,sorted[lp],ptr-sorted[lp]);
        cdx += ++ptr-sorted[lp];
        destination = atoi(ptr); 
        source=64;
/*
        switch(destination){
            case 13:
                streams += 4;
                QS += 2;
            case 12:
                streams += 2;
                QS += 1;
            case 11:
                streams += 2;            
                QS += 1;
            case 10:
                streams += 8;
                QS += 4;
                break;
            default:
                streams += destination;  // Pete may need to update if adding type 6 and/or 7
                QS += (destination&14)/2;
        }
*/
        switch(destination){
            case 14:
            case 13:
                streams += 8;
                QS += 4;
            case 15:
                streams += 8;
                QS += 4;
                break;
            default:
                streams += destination;  
                QS += (destination&14)/2;
        }        

        // RRD:G/C:Reg:SignedInt:StreamType(4bits)
#if BLOCKSIZE==512
       if (QS>256)
            printf("Too many channels, ran out of Quarter Streams.  Please reduce number of channels to use 255 Quarter Streams or less.\n");
#endif
        
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
        if( 13==destination&15 || 14==destination&15){ 
            if (destination & 32)
                registers--;
            destination &= 159;  // force 'counter' and save register off
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
    uint32_t start=0, end=0; // modular arithmetic and integer promotion make this work even if we temporarily store a negative value in start
    time_t now=time(NULL);

    if (1 < argc) {  // we have command line arguments

        if (etsdInit(argv[2], 1)){
            fprintf(stderr, "Error: can't open ETSD file %s \n", argv[2] );  // Log Error
            //exit(1);
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
                                start = end + start;
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

        
        // done with argv, run query
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

} // end of queryETSD()

int32_t dumpETSD(int argc, char *argv[]){
    uint8_t lp, reg=0;
    uint32_t Time=0, sector=0, end;
    char *ptr, temp[100], tTime[25];
    time_t now=time(NULL);
    struct tm *t=localtime(&now);
    FILE* fp = fopen(argv[2], "r");
   
        // checking if the file exist or not 
    if (fp == NULL) { 
        fprintf(stderr,"Error: File %s Not Found!\n", argv[2]); 
        exit(1); 
    } else {
        printf("\n\n");
        fseek(fp, 0L, SEEK_END);
        end = (ftell(fp)+BLOCKSIZE-1) / BLOCKSIZE -1;
        fclose(fp);
    }
    etsdInit(argv[2], 1);
        
    for ( lp = 3; lp < argc; lp++ ){
        if(ptr = strchr(argv[lp],'=')){
            ptr++;
            switch(*argv[lp]){
                case 's':       // Sector = negative numbers start from end
                case 'S':
                    sector = atoi(ptr);
                    break;
                case 't':
                case 'T':
                    Time = etsdParseTime(ptr);
                    break;
            }
        } else { 
            switch(*argv[lp]){
                case 'b':   // begining       
                case 'B':
                    sector=1;
                    break;
                case 'e':
                case 'E':
                    sector = end;
                    break;
            }
        }
    }
    if(Time){
        if(!(sector=etsdFindBlock(Time))){   
            Log("Can't find target time\n");
            exit(1);
        }
    }
//fprintf(stderr,"time: %u sector: %u \n", Time, sector);
    while(1){
        char c;
        Time=etsdTimeS(sector);
        now=Time;
        t=localtime(&now);
        strftime(tTime,22,"%D %T ", t);
        printf("Block: #%u of %u    Time Stamp: %s  (%u)\n", sector, end, tTime,Time);
        
        LogBlock(&PBlock.byteD, "", BLOCKSIZE);
        printf("Display (N)ext block, (P)revious block, or (Q)uit (N/P/Q) ");
        c = getch( );
        if(c=='n' || c=='N'){
            sector++;
            if (end<sector){
                sector=end;
                printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n     You have reached the end of the file \n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
                sleep(3);
            }
        }
        if(c=='p' || c=='P'){
            if(sector)
                sector--;
        }
        if(c=='q' || c=='Q'){
            exit(0);
        }
        

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
            case 15:
                sprintf(sType,"AutoScale");
                break;
            case 14:
                sprintf(sType,"Float");
                break;
            case 13:
                sprintf(sType,"Double");
                break;
            case 12:
                sprintf(sType,"24 bit");
                break;
            case 11:
                sprintf(sType,"22 bit");
                break;
            case 10:
                sprintf(sType,"20 bit");
                break;
            case 9:
                sprintf(sType,"ExtFull");
                break;
            case 8:
                sprintf(sType,"Full");
                break;
            case 7:
                sprintf(sType,"14 bit");
                break;
            case 6:
                sprintf(sType,"12 bit");
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
        printf("%2d %-20s  %s   %2u    %-9s  %-7s     %c        %c        %c\n", lp, EtsdInfo.label[lp], SRC_TYPE(lp)?"SHM":"ECM", SRC_CHAN(lp), sType, CNT_bit(lp)?"Counter":"Guage", REG_bit(lp)?'Y':'N', RRD_bit(lp)?'Y':'N', SIGNED(lp)?'Y':'N');
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
        if (!strchr(argv[1], 'd') && !strchr(argv[1], 'D') && !strcasestr(argv[2], ".tsd")) { 
            fprintf(stderr,"Error: second argument MUST be a ETSD file with a .tsd extention.\n You entered = %s.\n", argv[2]);
            exit(1);
        }        

            
        switch(argv[1][0]){   
            case 'c':
            case 'C':
                createETSD(argc, argv);
                break;
            case 'd':
            case 'D':
                dumpETSD(argc, argv);
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


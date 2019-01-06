/*************************************************************************
etsdQuery.h library for saving data to an ETSD time series database 

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

#ifndef __etsdquery_h__
#define __etsdquery_h__

#ifdef __cplusplus
extern "C" {
#endif


// calculates total seconds when presented with time as +/- ########S, ########M, #######H, #####d, or ##Y representing seconds, minutes, hours, days, or years
int32_t parseT(char *val);

// converts input time specification to epoch time
uint32_t etsdParseTime(char *dt);

// creates an RRD database that matches ETSD as specified in arguments.  rraV specifies the RRA to build
void buildRRD(char *fName, uint8_t channels, uint8_t chanMap[], uint32_t totalTime, uint32_t datapoints, int rraC, char *rraV[]);

int32_t recoverRRD(int argc, char *argv[], char *rrdFName, uint8_t overwrite);

// returns the channel number of chanName, or 255 if not found
uint8_t etsdChanNum(char *chanName);

int64_t etsdAMT(char *cmd, uint8_t chan, uint32_t start, uint32_t stop);

uint32_t etsdQ(int argc, char *argv);

#ifdef __cplusplus
}
#endif

#endif 

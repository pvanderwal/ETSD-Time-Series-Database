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


// calculates total seconds when presented with time as ##S or ##M or ##H, or ##Y.
int parseT(char *val);

int etsdParseTime(char *dt);

// converts an etsd time string into Epoch time
int etsdTime(char *dt);

int buildRRD(char *fName, uint8_t channels, uint8_t chanMap[], int totalTime, int datapoints, int rraC, char *rraV[]);

int recoverRRD(int argc, char *argv[], char *rrdFName);

uint32_t etsdQ(char *cmd, uint8_t chan, uint32_t start, uint32_t stop, uint32_t val);


#ifdef __cplusplus
}
#endif

#endif 

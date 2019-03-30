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

typedef struct {
//inputs
    uint8_t  chan;
    uint32_t start;
    uint32_t end;
    uint8_t  rate;  // set rate true for 'rate' counters i.e watt-sec versus quantity counters i.e. gallons, etc.
    uint32_t over;  // if you don't set over/under/equal before execution, then results for over/under/equal will be random/invalid
    uint32_t under; // ... this does not effect min/man/ave/total/etc.
    uint32_t equal;
//results    
    uint32_t intvCnt;
    uint32_t errCnt;    // number of intervals with E_DATA, missing data, etc.
    uint32_t iMin;      // Interval Minimum i.e. not divided by EtsdInfo.intervalTime  i.e. widgets per interval versus widgets per second 
    uint32_t iMax;      // Interval Maximum i.e. not divided by EtsdInfo.intervalTime  i.e. widgets per interval versus widgets per second 
    uint32_t iAve;      // Interval Ave is NOT adjusted for clock skew
    uint32_t min;       // Minimum widgets per second.  Min/Max are not adjusted for clock skew
    uint32_t max;       // Maximum widgets per second.  Min/Max are not adjusted for clock skew
    uint32_t ave;       // Average widgets per second adjusted for clock skew    
    uint32_t tMin;      // time of minimum value    (first minimum)
    uint32_t tMax;      // time of maximum value    (first maximum)
    uint32_t fOver;     // first interval that is over
    uint32_t fUnder;    // first interval that is under
    uint32_t fEqual;    // first interval that is equal
    uint32_t nOver;     // number of intervals when over
    uint32_t nUnder;    // number of intervals when under
    uint32_t nEqual;    // number of intervals equal
    uint32_t AWO;       // average value when over
    uint32_t AWU;       // average value when under
    int64_t RTot;       // Raw Total, not adjusted for clock skew.  
    int64_t Tot;        // Total, adjusted for clock skew ONLY if rate=1;
} ETSD_KS;

// calculates total seconds when presented with time as +/- ########S, ########M, #######H, #####d, or ##Y representing seconds, minutes, hours, days, or years
int32_t parseT(char *val);

// converts input time specification to epoch time
uint32_t etsdParseTime(char *dt);

// returns the channel number of chanName, or 255 if not found
uint8_t etsdChanNum(char *chanName);

int64_t etsdAMT(char *cmd, uint8_t chan, uint32_t start, uint32_t stop);

uint32_t etsdQ(int argc, char *argv);

#ifdef __cplusplus
}
#endif

#endif 
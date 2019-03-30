/*************************************************************************
 edoRRD.h ETSD External DataBase plugin for RRDTool
 
 Note:  ALL external db header files should have exactly the same two functions listed below 

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

#ifndef __edbRRD_h__
#define __edbRRD_h__

#ifdef __cplusplus
extern "C" {
#endif

uint8_t edoSetup(char *Config, char *DBfileName, char *cfgfn, uint8_t chanCnt, uint16_t ChanDefs, char **chanNames);

uint8_t edoSave(uint32_t timeStamp, uint8_t interval, uint32_t *dataArray, uint8_t *status, uint8_t *xData);

#ifdef __cplusplus
}
#endif

#endif

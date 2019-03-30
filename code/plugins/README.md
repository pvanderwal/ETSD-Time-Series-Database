<pre>
Three types of plugins for the ETSD Data Director (edd)
    src  ETSD Data Source, source of input data to be stored in ETSD database and/or in external database
    edo  External Data Output  plugin to handle sending data to an external database, website, etc.
    xd   Plugin for 'Extra Data'  ETSD can store a limited ammount (generally a few bytes) of unstructured data once per block


====================================================================================================================
ETSD Source plugin API for ETSD Data Director (edd)
 // Note: up to 4 simultaneous source plugins supported
 // first source controls timing, all other sources need to return immediately with either new data, or 'data not ready' error

// Functions are not required to use variables pass to them
// Each source plugin MUST contain at least the following functions:

// srcSetup is only called during initial configuration 
uint8_t srcSetup(char *config, char *port, char *configFileName, uint16_t etsdHeader, uint16_t intervalTime)  
    returns 0 on success, any other value on failure

//check for new data. Runs once per interval.  Intended to prepare data for srcReadChan()
uint8_t srcCheckData(uint16_t timeOut, uint8_t interV)  
    checks for new data returns when data recieved or timed out,
    timeOut is in 1/10 of a second, function can return sooner, but MUST return by end of timeOut.
    returns 0 = rx good data, 1 = checksum/CRC error, 2=source reset, 5 = timed out/data not ready, 
            9 = unspecified error, 128=wait/updating 
    Note: when using &3 to select two bits, 1 = data error(checksum, timeout, etc.) and 2 = Source Reset
            
//runs once per interval shortly after esdCheckData()
uint32_t srcReadChan (uint8_t chan) 
    only executed if srcCheckData() returned success
    Returns current data from source channel 'chan'
    Note: channels are not necessarily accessed in order.

====================================================================================================================
External Data Output plugin API  
  // edd supports 1 external data out plugin.  
  // Data Can be simultaneously stored in either ETSD, external database, or both
  
// edoSetup is only called during initial configuration of edd
uint8_t edoSetup(char *config, char *destination, char *configFileName, uint8_t xDataSize, uint8_t chanCnt, uint16_t *chanDefs, char **chanNames )
    chanDefs = {etsdSource, etsdDestination}
    returns 0 on success, any other value on failure   
        
// called once per interval if saving to external db 
uint8_t edoSave(uint32_t timeStamp, uint8_t interval, uint32_t *dataArray, uint8_t *statusArray, uint8_t *xData)
    used by both edd to save data in realtime and etsdCmd to 'restore' previously recorded data
    timeStamp == 0 indicates function should use the current time
    *dataArray channel data per interval(0xFFFFFFFF = invalid data), in the same order as *chanDefs in edoSetup()
    *statusArray contains channel status that matches *dataArray elements :  0 = ok, 1 = data invalid, 2=src_reset
    interval is the ETSD block interval when data was saved
    returns zero on success, any other value indicates some kind of failure

====================================================================================================================
Extra Data  API
    //Note: ETSD supports including a limited amount of external data that is saved once per block

// xdSetup is only called during initial configuration of edd
uint8_t xdSetup (char *config, char *source, char *configFileName, uint16_t etsdHeader, uint16 intervalTime)
    modified etsdHeader = (header<<1) & 0xFF00 + xData size
    
void xdRead(uint8_t interval, uint8_t cnt, uint8_t *dataArray)  

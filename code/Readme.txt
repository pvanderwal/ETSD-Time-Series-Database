// Instructions for building ETSD on Linux

//********************** Build ETSD shared libraries *************************
//build errorlog shared library
gcc errorlog.c -c -fpic
gcc *.o -shared -o /usr/local/lib/libelog.so
rm *.o

//build eshm plugin
gcc eshm.c -lelog -c -fpic
gcc *.o -shared -o /usr/local/lib/libeshm.so
rm *.o

//build etsd base shared library
gcc etsd.c -lelog -c -fpic
gcc *.o -shared -o /usr/local/lib/libetsd.so
rm *.o

//build etsd save shared library
gcc etsdSave.c -lelog -letsd -c -fpic
gcc *.o -shared -o /usr/local/lib/libetsdSave.so
rm *.o

//build etsd read shared library
gcc etsdRead.c -letsd -lelog -c -fpic
gcc *.o -shared -o /usr/local/lib/libetsdRead.so
rm *.o

//build etsd query shared library
gcc etsdQuery.c -letsd -letsdRead -lelog -lrrd -c -fpic
gcc *.o -shared -o /usr/local/lib/libetsdQ.so
rm *.o

//********************** Build plugins *************************
//build ecmR plugin
gcc srcECM.c -lelog -c -fpic
gcc *.o -shared -Wl,/usr/local/lib/libelog.so -o /usr/local/lib/libsrcECM.so
rm *.o

//build edoRRD plugin
gcc edoRRD.c -lelog -lrrd -c -fpic
gcc -shared -Wl,/usr/lib/arm-linux-gnueabihf/librrd.so -o /usr/local/lib/libedoRRD.so *.o
rm *.o

//********************** Build applications **************************

// build etsdCmd
gcc -o etsdCmd etsdCmd.c -lelog -letsd -letsdRead -letsdQ -lrrd

//build edd
//gcc -o edd edd.c -lelog -lecmR -leshm -letsdSave -letsd -lrrd -lrt  
gcc -o edd edd.c -lelog -letsdSave -letsd -lrt -ldl

/usr/local/sbin/edD 


// update ldconfig cache
ldconfig

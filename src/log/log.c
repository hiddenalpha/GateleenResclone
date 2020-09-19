/* By using this work you agree to the terms and conditions in 'LICENCE.txt' */

#include "log.h"

/* System */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


void log_asdfghjklqwertzu( const char*level, const char*cLvl, const char*file, int line, const char*fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    int isTTY = isatty(2);
    const char *cRst = isTTY ? "\033[0m" : "";
    char *cTxt = isTTY ? "\033[90m" : "";
    cLvl = isTTY ? cLvl : "";
    char tBuf[20];
    const time_t t = time(0);
    const char *tfmt = "%Y-%m-%d_%H:%M:%S";
    if( isTTY ){ tfmt += 9; }
    strftime( tBuf,sizeof(tBuf), tfmt, localtime(&t) );
    const char *fileOnly = strrchr(file, '/') +1;
    fprintf( stderr, "[%s%s%s %s%s%s %s%s:%d%s] ",
        cTxt,tBuf,cRst, cLvl,level,cRst , cTxt,fileOnly,line,cRst );
    vfprintf( stderr, fmt, args );
    va_end( args );
}


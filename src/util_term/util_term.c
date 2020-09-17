
#include "commonbase.h"

/* System */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>


#ifdef _WIN32
static HANDLE
fdToHandle( int fd ){
    switch( fd ){
    case 1: return GetStdHandle(STD_OUTPUT_HANDLE);
    case 2: return GetStdHandle(STD_ERROR_HANDLE );
    default: assert(0); return NULL;
    }
}
#endif


#ifdef _WIN32
static void
termColorsEnable( int fd )
{
    HANDLE hOut = fdToHandle( fd );
    if( hOut==INVALID_HANDLE_VALUE ){ return; }
    DWORD dwMode = 0;
    if( ! GetConsoleMode(hOut,&dwMode) ){ return; }
    dwMode |= 0x0004; /* <- ENABLE_VIRTUAL_TERMINAL_PROCESSING */
    if( ! SetConsoleMode(hOut, dwMode) ){ return; }
}
#endif


void
util_term_init()
{
    #ifdef _WIN32
    if( isatty(1) ){ termColorsEnable(1); }
    if( isatty(2) ){ termColorsEnable(2); }
    #endif
}


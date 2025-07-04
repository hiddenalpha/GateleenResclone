/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

#include <assert.h>

/* Project */
#include "gateleen_resclone.h"
#include "util_term.h"


#if _WIN32
int _setmode(int,int);
#endif


int
main( int argc, char**argv )
{
    int err;
#if _WIN32 /* [source](https://git.hiddenalpha.ch/UnspecifiedGarbage.git/tree/src/main/c/common/snippets.c) */
	{char a=0;for(;!(a&10);){_setmode(a++,32768);}}
    switch( WSAStartup(1, &(WSADATA){0}) ){
    case 0: break;
    case WSASYSNOTREADY    : assert(!"WSASYSNOTREADY"    ); break;
    case WSAVERNOTSUPPORTED: assert(!"WSAVERNOTSUPPORTED"); break;
    case WSAEINPROGRESS    : assert(!"WSAEINPROGRESS"    ); break;
    case WSAEPROCLIM       : assert(!"WSAEPROCLIM"       ); break;
    case WSAEFAULT         : assert(!"WSAEFAULT"         ); break;
    default                : assert(!"ERROR"             ); break;
    }
#endif
    util_term_init();
    err = gateleenResclone_run( argc, argv );
#if _WIN32 /* [source](https://git.hiddenalpha.ch/UnspecifiedGarbage.git/tree/src/main/c/common/snippets.c) */
    switch( WSACleanup() ){
    case 0: break;
    case WSANOTINITIALISED : assert(!"WSANOTINITIALISED" ); break;
    case WSAENETDOWN       : assert(!"WSAENETDOWN"       ); break;
    case WSAEINPROGRESS    : assert(!"WSAEINPROGRESS"    ); break;
    default                : assert(!"ERROR"             ); break;
    }
#endif
    if( err<0 ){ err = 0-err; }
    return err>127 ? 1 : err;
}


/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

/* Project */
#include "gateleen_resclone.h"
#include "util_term.h"


int
main( int argc, char**argv )
{
    int err;
    util_term_init();
    err = gateleenResclone_run( argc, argv );
    if( err<0 ){ err = 0-err; }
    return err>127 ? 1 : err;
}


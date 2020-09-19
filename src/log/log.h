/* By using this work you agree to the terms and conditions in 'LICENCE.txt' */

#ifndef INCGUARD_540e2cd36c1ba21909b922d45a94b7f7
#define INCGUARD_540e2cd36c1ba21909b922d45a94b7f7

#include "commonbase.h"


#define LOG_FATAL( ... ) log_asdfghjklqwertzu("FATAL","\033[31m",__FILE__,__LINE__,__VA_ARGS__)
#define LOG_ERROR( ... ) log_asdfghjklqwertzu("ERROR","\033[31m",__FILE__,__LINE__,__VA_ARGS__)
#define LOG_WARN( ... )  log_asdfghjklqwertzu("WARN ","\033[33m",__FILE__,__LINE__,__VA_ARGS__)
#define LOG_INFO( ... )  log_asdfghjklqwertzu("INFO ","\033[36m",__FILE__,__LINE__,__VA_ARGS__)
#ifndef NDEBUG
    #define LOG_DEBUG( ... )  log_asdfghjklqwertzu("DEBUG","\033[35m",__FILE__,__LINE__,__VA_ARGS__ )
    #define LOG_TRACE( ... )  log_asdfghjklqwertzu("TRACE","\033[94m",__FILE__,__LINE__,__VA_ARGS__ )
#else
    #define LOG_DEBUG( ... )  /* Debugging not enabled. Therefore ignore that log level. */
    #define LOG_TRACE( ... )  /* Debugging not enabled. Therefore ignore that log level. */
#endif


void log_asdfghjklqwertzu( const char*level, const char*cLvl, const char*file, int line, const char*fmt, ... );


#endif /* INCGUARD_540e2cd36c1ba21909b922d45a94b7f7 */

/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

#ifndef INCGUARD_d16bcf26aca7174fb8aae7641c828007
#define INCGUARD_d16bcf26aca7174fb8aae7641c828007

#define Garbage_Closure void*

#include "commonbase.h"
#include <regex.h>

#include <stdint.h>
#include <sys/types.h>

#include <Garbage.h>


#define LOGF(...) fprintf(stderr, __VA_ARGS__)
#define LOGW(...) fprintf(stderr, __VA_ARGS__)
#define LOGD(...) fprintf(stderr, __VA_ARGS__)
#define LOGT(...) fprintf(stderr, __VA_ARGS__)


#define Mallocator_realloc(A, B, C, D) (*A)->reallocBlocking(A, B, C, D)
#define FN_ThreadPool_enque(A, B, C) (*A)->enque(A, B, C)
#define FN_HttpClientReq_closeSnk(A) (*A)->closeSnk(A)
#define FN_HttpClientReq_resume(A) (*A)->resume(A)
#define FN_Env_enque(A, B, C) (*A)->enqueBlocking(A, B, C)
#define FN_Env_runUntilDone(A) (*A)->runUntilDone(A)


/** Operation mode. */
typedef enum OpMode {
    MODE_NULL =0,
    MODE_FETCH=1,
    MODE_PUSH =2
} OpMode;


/** Main module handle representing an instance. */
#define Resclone_mAGIC 0xA5450000
typedef struct Resclone {
    unsigned mAGIC;
    enum OpMode mode;
    /** Base URL where to upload to / download from. */
    char *url;
    /** Array of regex patterns to use per segment. */
    regex_t *filter;
    size_t filter_len;
    /** Says if only start or whole path needs to match the pattern. */
    int isFilterFull;
    /* Path to archive file to use. Using stdin/stdout if NULL. */
    char *file;
    /**/
    struct Garbage_Env **env;
    struct Garbage_Mallocator **mallocator;
    struct Garbage_SocketMgr **socketMgrTls;
    struct Garbage_IoMultiplexer **ioMultiplexer;
    struct Garbage_ThreadPool **ioWorker;
    struct Garbage_Networker **networker;
    /**/
    uintptr_t envMem[64];
    /**/
} Resclone;


/** @return
 *      Zero on success, negative values otherwise. Positive values are
 *      reserved. */
int
gateleenResclone_run( int argc , char**argv );


int initEnv( struct Resclone* );


struct Garbage_HttpClientReq** newHttpsClientReq(
    struct Resclone*,
    char const*mthd,
    char const*host,
    uint_least16_t port,
    char const*url,
    struct Garbage_HttpMsg_Hdr*,
    int hdrs_cnt,
    struct Garbage_HttpClientReq_Mentor*,
    void*mentorCls
);


char const*strerrname(int);


#endif /* INCGUARD_d16bcf26aca7174fb8aae7641c828007 */

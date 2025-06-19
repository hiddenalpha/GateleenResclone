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
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#define LOGW(...) fprintf(stderr, __VA_ARGS__)
#define LOGI(...) fprintf(stderr, __VA_ARGS__)
#define LOGD(...) fprintf(stderr, __VA_ARGS__)
#define LOGT(...) fprintf(stderr, __VA_ARGS__)


#define Mallocator_realloc(A, B, C, D) (*A)->reallocBlocking(A, B, C, D)
#define FN_ThreadPool_enque(A, B, C) (*A)->enque(A, B, C)
#define FN_HttpClientReq_closeSnk(A) (*A)->closeSnk(A)
#define FN_HttpClientReq_pause(A) (*A)->pause(A)
#define FN_HttpClientReq_resume(A) (*A)->resume(A)
#define FN_Env_enque(A, B, C) (*A)->enqueBlocking(A, B, C)
#define FN_Env_runUntilDone(A) (*A)->runUntilDone(A)
#define FN_JsonTreeParser_write(A, B, C, D, E, F) (*A)->write(A, B, C, D, E, F)


/** Operation mode. */
typedef  enum OpMode  OpMode; /*<- TODO del*/
enum OpMode {
    MODE_NULL =0,
    MODE_FETCH=1,
    MODE_PUSH =2
};


struct EnvAndDeps {
	struct Garbage_Env **env;
	struct Garbage_Mallocator **mallocator;
	struct Garbage_MemoryArena **mainArena;
	struct Garbage_SocketMgr **socketMgrTls;
	struct Garbage_IoMultiplexer **ioMultiplexer;
	struct Garbage_ThreadPool **ioWorker;
	struct Garbage_Networker **networker;
};


/** @return
 *      Zero on success, negative values otherwise. Positive values are
 *      reserved. */
int
gateleenResclone_run( int argc , char**argv );


int initEnv( struct EnvAndDeps*, void*, int );


struct Garbage_HttpClientReq** newHttpsClientReq(
    struct EnvAndDeps*,
    char const*mthd,
    char const*host,
    uint_least16_t port,
    char const*url,
    struct Garbage_HttpMsg_Hdr*,
    int hdrs_cnt,
    struct Garbage_HttpClientReq_Mentor*,
    void*mentorCls
);


struct Garbage_JsonTreeParser** newJsonTreeParser(
	struct EnvAndDeps*,
	void(*onJsonResult)( void*, int err, void*theJsonTreeParser_JsonNode ),
	void*onJsonResultCls
);

struct Garbage_MemoryArena** newArenaLinkedList( struct EnvAndDeps* );


struct Garbage_TarEnc** newTarEnc( struct EnvAndDeps*, int(*)(void*,const char*,int,int), void* );


char const*strerrname(int);


#endif /* INCGUARD_d16bcf26aca7174fb8aae7641c828007 */


#include "gateleen_resclone.h"

#include <assert.h>

#include <Garbage_Bootstrap.h>


typedef  struct Resclone  Resclone;


int initEnv( Resclone*resclone ){
    resclone->mallocator = Garbage_newMallocator();
    assert(resclone->mallocator);
    resclone->env = Garbage_newEnv(&(struct Garbage_Env_Opts){
        .memBlockToUse = resclone->envMem,
        .memBlockToUse_sz = sizeof resclone->envMem,
        .mallocator = resclone->mallocator,
    });
    assert(resclone->mallocator);
    resclone->ioWorker = Garbage_newThreadPool(&(struct Garbage_ThreadPool_Opts){
        .mallocator = resclone->mallocator,
        .numThrds = 2, /*TODO config via environ */
    });
    assert(resclone->env);  assert(resclone->mallocator);  assert(resclone->ioWorker);
    resclone->ioMultiplexer = Garbage_newIoMultiplexer(resclone->env, &(struct Garbage_IoMultiplexer_Opts){
        .mallocator = resclone->mallocator,
        .ioWorker = resclone->ioWorker,
    });
    assert(resclone->env);  assert(resclone->mallocator);  assert(resclone->ioMultiplexer);
    assert(resclone->ioWorker);
    resclone->socketMgrTls = Garbage_newSocketMgr(resclone->env, &(struct Garbage_SocketMgr_Opts){
        .mallocator = resclone->mallocator,
        .ioMultiplexer = resclone->ioMultiplexer,
        .blockingIoWorker = resclone->ioWorker,
    });
    assert(resclone->mallocator);  assert(resclone->ioWorker);
    resclone->networker = Garbage_newNetworker(&(struct Garbage_Networker_Opts){
        .mallocator = resclone->mallocator,
        .ioWorker = resclone->ioWorker,
    });
    assert(resclone->socketMgrTls);
    assert(resclone->ioWorker);
    assert(resclone->networker);
    return 0;
}


struct Garbage_HttpClientReq** newHttpsClientReq(
    Resclone*resclone,
    char const*mthd,
    char const*host,
    uint_least16_t port,
    char const*url,
    struct Garbage_HttpMsg_Hdr *hdrs,
    int hdrs_cnt,
    struct Garbage_HttpClientReq_Mentor*mentor,
    void*mentorCls
){
    return Garbage_newHttpClientReq(
        resclone->env, mentor, mentorCls,
        &(struct Garbage_HttpClientReq_Opts){
            .mallocator = resclone->mallocator,
            .socketMgr = resclone->socketMgrTls,
            .ioWorker = resclone->ioWorker,
            .networker = resclone->networker,
            .mthd = mthd,
            .host = host,
            .url = url,
            .port = port,
            .hdrs = hdrs,
            .hdrs_cnt = hdrs_cnt,
        }
    );
}



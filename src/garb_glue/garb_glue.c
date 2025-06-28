
#include "gateleen_resclone.h"

#include <assert.h>
#include <string.h>

#include <Garbage_Bootstrap.h>


typedef  struct EnvAndDeps  EnvAndDeps;


int initEnv( EnvAndDeps*deps, void*envMem, int envMem_sz ){
    deps->mallocator = Garbage_newMallocator();
    assert(deps->mallocator);
    deps->env = Garbage_newEnv(&(struct Garbage_Env_Opts){
        .memBlockToUse = envMem,
        .memBlockToUse_sz = envMem_sz,
        .mallocator = deps->mallocator,
    });
    assert(deps->mallocator);
	deps->mainArena = newArenaLinkedList(deps);
    deps->ioWorker = Garbage_newThreadPool(&(struct Garbage_ThreadPool_Opts){
        .mallocator = deps->mallocator,
        .numThrds = 2, /*TODO config via environ */
    });
    assert(deps->env);  assert(deps->mallocator);  assert(deps->ioWorker);
    deps->ioMultiplexer = Garbage_newIoMultiplexer(deps->env, &(struct Garbage_IoMultiplexer_Opts){
        .mallocator = deps->mallocator,
        .ioWorker = deps->ioWorker,
    });
    assert(deps->env);  assert(deps->mallocator);  assert(deps->ioMultiplexer);
    assert(deps->ioWorker);
    deps->socketMgrTls = Garbage_newSocketMgr(deps->env, &(struct Garbage_SocketMgr_Opts){
        .mallocator = deps->mallocator,
        .ioMultiplexer = deps->ioMultiplexer,
        .blockingIoWorker = deps->ioWorker,
    });
    assert(deps->mallocator);  assert(deps->ioWorker);
    deps->networker = Garbage_newNetworker(&(struct Garbage_Networker_Opts){
        .mallocator = deps->mallocator,
        .ioWorker = deps->ioWorker,
    });
	/**/
	assert(deps->socketMgrTls);
	assert(deps->ioWorker);
	assert(deps->networker);
	/**/
	(*deps->ioMultiplexer)->start(deps->ioMultiplexer);
	(*deps->ioWorker)->start(deps->ioWorker);
	/**/
	return 0;
}


struct Garbage_MemoryArena** newArenaLinkedList( EnvAndDeps*deps ){
	struct Garbage_ArenaLinkedList **impl;
	impl = Garbage_newArenaLinkedList(&(struct Garbage_ArenaLinkedList_Opts){
		.mallocator = deps->mallocator,
		.blkSz = 64*1024*1024,
	});
	return (*impl)->asMemoryArena(impl);
}


struct Garbage_HttpClientReq** newHttpsClientReq(
    EnvAndDeps*deps,
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
        deps->env, mentor, mentorCls,
        &(struct Garbage_HttpClientReq_Opts){
            .mallocator = deps->mallocator,
            .socketMgr = deps->socketMgrTls,
            .ioWorker = deps->ioWorker,
            .networker = deps->networker,
            .mthd = mthd,
            .host = host,
            .url = url,
            .port = port,
            .hdrs = hdrs,
            .hdrs_cnt = hdrs_cnt,
        }
    );
}


struct Garbage_JsonTreeParser** newJsonTreeParser(
	EnvAndDeps*deps,
	void(*onJsonResult)( void*, int err, void*theJsonTreeParser_JsonNode ),
	void*onJsonResultCls
){
	return Garbage_newJsonTreeParser(&(struct Garbage_JsonTreeParser_Opts){
		.env = deps->env,
		/*
		 * TODO pass-in from args an arena, WITH CORRECT LIFETIME . */
		.scratchArena = deps->mainArena,
		.jsonArena = deps->mainArena,
		.cpuWorker = deps->ioWorker, /*TODO fix mismatch*/
		.onJsonResult = (void*)onJsonResult, /*TODO fuck cast to deadh*/
		.cls = onJsonResultCls,
	});
}


struct Garbage_TarEnc** newTarEnc(
	EnvAndDeps*deps,
	void (*onChunk)(void*,const char*,int,int,void(*)(int,void*),void*),
	void*cls
){
	return Garbage_newTarEnc(&(struct Garbage_newTarEncOpts){
		.mallocator = deps->mallocator,
		.onChunk = onChunk,
		.cls = cls,
	});
}



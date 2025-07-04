
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
    deps->socketMgr = Garbage_newSocketMgr(deps->env, &(struct Garbage_SocketMgr_Opts){
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
	assert(deps->socketMgr);
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


struct Garbage_HttpClientReq** newHttpClientReq(
    EnvAndDeps*deps,
    char const*mthd,
    char const*host,
    uint_least16_t port,
    int useTls,
    char const*url,
    struct Garbage_HttpMsg_Hdr *hdrs,
    int hdrs_cnt,
    struct Garbage_HttpClientReq_Mentor*mentor,
    void*mentorCls
){
	static struct Garbage_TlsClient_Mentor socketMgrTlsMentor = {
#if 0
	void (*pushIoTask)( void(*task)(Garbage_Closure arg), Garbage_Closure arg, Garbage_Closure cls );
	void (*sockAcquire)( void*sockaddr, int sockaddr_len, Garbage_Closure cls, void(*)(int retval, Garbage_Closure sock, Garbage_Closure arg), Garbage_Closure arg );
	void (*sockRelease)( Garbage_Closure sock, int mustClose, Garbage_Closure cls );
	void (*sockSend)( Garbage_Closure sock, const void*buf, int buf_len, Garbage_Closure cls, void(*onDone)(int retval, Garbage_Closure arg), Garbage_Closure arg );
	void (*sockFlush)( Garbage_Closure sock, Garbage_Closure cls, void(*onDone)(int,Garbage_Closure arg), Garbage_Closure arg );
	void (*sockRecv)( Garbage_Closure sock, void*buf, int buf_len, Garbage_Closure cls, void(*onDone)(int,Garbage_Closure arg), Garbage_Closure arg );
	void (*onError)( int eno, Garbage_Closure cls );
#endif
	};
	/* TODO: WARN we're abusing static here */
	socketMgrTlsMentor.onError = mentor->onError;
	/* TODO: WARN fix this memory-leak! */
	struct Garbage_TlsClient **tlsClient = NULL;
	if( useTls ){
		tlsClient = Garbage_newTlsClient(
			deps->env, &socketMgrTlsMentor, NULL,
			&(struct Garbage_TlsClient_Opts){
				.peerHostname = host,
				.mallocator = deps->mallocator,
				.socketMgr = deps->socketMgr,
				.ioWorker = deps->ioWorker,
			}
		);
	}
    return Garbage_newHttpClientReq(
        deps->env, mentor, mentorCls,
        &(struct Garbage_HttpClientReq_Opts){
            .mallocator = deps->mallocator,
            .socketMgr = (useTls)
				? (*tlsClient)->asSocketMgr(tlsClient)
				: deps->socketMgr,
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
	void(*onParseError)( void*, uintptr_t errOff ),
	void*onJsonResultCls
){
	return Garbage_newJsonTreeParser(&(struct Garbage_JsonTreeParser_Opts){
		.env = deps->env,
		/*
		 * TODO pass-in from args an arena, WITH CORRECT LIFETIME . */
		.scratchArena = deps->mainArena,
		.jsonArena = deps->mainArena,
		.cpuWorker = deps->ioWorker, /*TODO fix mismatch*/
		.cls = onJsonResultCls,
		.onJsonResult = (void*)onJsonResult, /*TODO fuck cast to deadh*/
		.onError = onParseError,
	});
}



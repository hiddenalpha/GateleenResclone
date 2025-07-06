
#include "gateleen_resclone.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <Garbage_Bootstrap.h>


typedef  struct EnvAndDeps  EnvAndDeps;


/* TODO get rid of this shit! */
struct Cls45A3C95F/* quickNDirtySocketMgr TODO makeMePretty */{
	unsigned mAGIC;
	char peerHostname[128];
	EnvAndDeps *deps;
};


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


/* TODO move this IMPL to where IMPLs belong, away from GLUE. */
static void socketAcquire(
	void*cls_, int flg, void*sockaddr, int sockaddr_len,
	void(*onDone)(int err,struct Garbage_Socket**sock,Garbage_Closure arg), Garbage_Closure onDoneArg
){
	struct Cls45A3C95F*const cls = cls_;  assert(cls->mAGIC == 0x45A3C95F);
	static struct Garbage_TlsClient_Mentor socketMgrTlsMentor = { 0 };
	//socketMgrTlsMentor.onError = mentor->onError; /* <- TODO: WARN we're abusing static here */
	struct Garbage_TlsClient **tlsClient = NULL;
	int const useTls = (flg & 8);
	if( useTls ){
		/* TODO: WARN fix this resource-leak! */
		tlsClient = Garbage_newTlsClient(
			cls->deps->env, &socketMgrTlsMentor, NULL,
			&(struct Garbage_TlsClient_Opts){
				.peerHostname = cls->peerHostname,
				.mallocator = cls->deps->mallocator,
				.socketMgr = cls->deps->socketMgr,
				.ioWorker = cls->deps->ioWorker,
			}
		);
		(*(*tlsClient)->asSocketMgr(tlsClient))->sockAcquireConnect(
			(*tlsClient)->asSocketMgr(tlsClient), sockaddr, sockaddr_len, onDone, onDoneArg);
	}else{
		(*cls->deps->socketMgr)->sockAcquireConnect(cls->deps->socketMgr, sockaddr, sockaddr_len,
			onDone, onDoneArg);
	}
}


/* TODO move this IMPL to where IMPLs belong, away from GLUE. */
static void releaseSocketOfWhateverImpl( void*cls_, struct Garbage_Socket**sock, int flg ){
	assert(flg == 0 || flg == 1);
	if( flg & 1 ){
		/* MUST NOT be ReUsed! No pooling allowed! */
		(*sock)->close(sock, flg, noopVoid, NULL);
	}else{
		//LOGD("ENOTSUP: TODO impl connection pooling\n\t@ %s:%d\n", __FILE__, __LINE__);
		(*sock)->close(sock, flg, noopVoid, NULL);
	}
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
	static struct Cls45A3C95F clsSock_, *clsSock = &clsSock_;
	assert(sizeof clsSock->peerHostname > strlen(host));
	*clsSock = (struct Cls45A3C95F){
		.mAGIC = 0x45A3C95F,
		.deps = deps,
	};
	int const a = sizeof clsSock->peerHostname, b = strlen(host);
	memcpy(clsSock->peerHostname, host, (a<b)?a:b);
	return Garbage_newHttpClientReq(&(struct Garbage_HttpClientReq_Opts){
		.env = deps->env,
		.mentor = mentor,
		.mentorCtx = mentorCls,
		.mallocator = deps->mallocator,
		.ioWorker = deps->ioWorker,
		.networker = deps->networker,
		/**/
		.socketCtx = clsSock,
		.socketAcquire = socketAcquire,
		.socketRelease = releaseSocketOfWhateverImpl,
		/**/
		.mthd = mthd,
		.host = host,
		.url = url,
		.port = port,
		.hdrs = hdrs,
		.hdrs_cnt = hdrs_cnt,
	});
}


struct Garbage_JsonTreeParser**
newJsonTreeParser(
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




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
	{
		struct Garbage_ThreadPool_Opts opts = {
			.mallocator = deps->mallocator,
			.numThrds = 2, /*TODO config via environ */
		};
		deps->ioWorker = Garbage_newThreadPool(&opts);
		opts.start(deps->ioWorker);
		assert(deps->ioWorker);
	}{
		assert(deps->env);  assert(deps->mallocator);  assert(deps->ioWorker);
		struct Garbage_IoMultiplexer_Opts opts = {
			.env = deps->env,
			.mallocator = deps->mallocator,
			.ioWorker = deps->ioWorker,
		};
		deps->ioMultiplexer = Garbage_newIoMultiplexer(&opts);
		opts.start(deps->ioMultiplexer);
	}
    assert(deps->env);  assert(deps->mallocator);  assert(deps->ioMultiplexer);
    assert(deps->ioWorker);
	/**/
    assert(deps->mallocator);  assert(deps->ioWorker);
    deps->networker = Garbage_newNetworker(&(struct Garbage_Networker_Opts){
        .mallocator = deps->mallocator,
        .ioWorker = deps->ioWorker,
    });
	/**/
	assert(deps->networker);
	/**/
	return 0;
}


struct Qntan_MemArena**
newArenaLinkedList( EnvAndDeps*deps ){
	return Garbage_newArenaLinkedList(&(struct Garbage_ArenaLinkedList_Opts){
		.mallocator = deps->mallocator,
		.blkSz = 64*1024*1024,
	});
}


struct Qntan_File**
newFileStdlib(
	struct EnvAndDeps*deps, FILE*file, int takeOwnership, void(**unref)(struct Qntan_File**)
){
	struct Garbage_FileStdlib_Opts opts = {
		.mallocator = deps->mallocator,
		.ioMux = deps->ioMultiplexer,
		.file = (uintptr_t)file,
		.takeOwnership = takeOwnership,
	};
	struct Qntan_File **ret = Garbage_newFileStdlib(&opts);
	*unref = opts.unref;  assert(opts.unref);
	return ret;
}


struct Qntan_JsonTreeDec**
newJsonTreeParser(
	EnvAndDeps*deps,
	void(*onJsonResult)( CLOSURE, int err, void*theJsonTreeParser_JsonNode, uint64_t errOff ),
	void (**unref)(struct Qntan_JsonTreeDec**),
	CLOSURE onJsonResultCls
){
	struct Garbage_JsonTreeDec_Opts opts = {
		.env = deps->env,
		.mallocator = deps->mallocator,
		/*
		 * TODO pass-in from args an arena, WITH CORRECT LIFETIME . */
		.scratchArena = deps->mainArena,
		.jsonArena = deps->mainArena,
		.cpuWorker = deps->ioWorker, /*TODO fix mismatch*/
		.cls = onJsonResultCls,
		.onJsonResult = (void*)onJsonResult, /*TODO fuck cast to deadh*/
	};
	struct Qntan_JsonTreeDec **ret = Garbage_newJsonTreeDec(&opts);
	*unref = opts.unref;
	return ret;
}


struct Qntan_TarEnc**
newTarEnc(
	struct EnvAndDeps*deps,
	void(*onChunk)(CLOSURE,const char*,int,int,void(*onDone)(int,CLOSURE),CLOSURE),
	CLOSURE onChunkArg
){
	assert(deps->mallocator);
	return Garbage_newTarEnc(&(struct Garbage_TarEnc_Opts){
		.mallocator = deps->mallocator,
		.cls = onChunkArg,
		.onChunk = onChunk,
	});
}


struct Qntan_TarDec**
newTarDec( struct EnvAndDeps*deps, struct Qntan_File**archive ){
	return Garbage_newTarDec(&(struct Garbage_TarDec_Opts){
		.mallocator = deps->mallocator,
		.archive = archive,
	});
}



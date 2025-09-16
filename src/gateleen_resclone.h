/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

#ifndef INCGUARD_d16bcf26aca7174fb8aae7641c828007
#define INCGUARD_d16bcf26aca7174fb8aae7641c828007

#define _POSIX_C_SOURCE 200809L

#include <regex.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include <Garbage.h>

#define CLOSURE uintptr_t

#define STR_QUOT_(S) #S
#define STR_QUOT(S) STR_QUOT_(S)

#define LOGF(...) fprintf(stderr, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#define LOGW(...) fprintf(stderr, __VA_ARGS__)
#define LOGI(...) fprintf(stderr, __VA_ARGS__)
#define LOGD(...) fprintf(stderr, __VA_ARGS__)
#define LOGT(...) do{if( 0 )fprintf(stderr, __VA_ARGS__);}while(0)

/* [Source](https://git.hiddenalpha.ch/UnspecifiedGarbage.git/tree/src/main/c/common/snippets.c) */
#define container_of(P, T, M) \
     ((T*)( ((size_t)P) - ((size_t)((ptrdiff_t)&((T*)0)->M - (ptrdiff_t)0) )))
#if _WIN32
#	define FUCKWINDOOFLONG (long unsigned)
#else
#	define FUCKWINDOOFLONG /*no BS needed on sane systems*/
#endif
#ifndef FALL
#	define FALL do{}while(0)
#endif

#if NDEBUG /* <- aka release mode */
#	define REGISTER register
#else
#	define REGISTER
#endif

#define MIN(A, B) ((((A) < (B)) * (A)) + (((A) >= (B)) * (B)))

#define FN_EvLoop_delAwaitToken(A, ...) (*A)->delAwaitToken(A, __VA_ARGS__)
#define FN_EvLoop_enque(A, ...) (*A)->enque(A, __VA_ARGS__)
#define FN_EvLoop_runUntilDone(A) (*A)->runUntilDone(A)
#define FN_HttpClientReq_pause(A) (*A)->pause(A)
#define FN_HttpClientReq_resume(A) (*A)->resume(A)
#define FN_HttpClientReq_write(A, B, C, D, E, F) (*A)->write(A, B, C, D, E, F)
#define FN_JsonTreeDec_write(A, B, C, D, E, F) (*A)->write(A, B, C, D, E, F)
#define FN_Mallocator_realloc(A, ...) (*A)->realloc(A, __VA_ARGS__)
#define FN_TarDec_nextHdr(A, ...) (*A)->nextHdr(A, __VA_ARGS__)
#define FN_TarDec_readBody(A, ...) (*A)->readBody(A, __VA_ARGS__)
#define FN_TarEnc_nextEntry(A, ...) (*A)->nextEntry(A, __VA_ARGS__)
#define FN_TarEnc_write(A, ...) (*A)->write(A, __VA_ARGS__)
#define FN_TarEnc_getLastErrorStr(A) (*A)->getLastErrorStr(A)
#define FN_ThreadPool_enque(A, B, C) (*A)->enque(A, B, C)










struct EnvAndDeps {
	struct Qntan_EvLoop **evLoop;
	struct Garbage_Env **env; /* <- TODO OBSOLETE! */
	struct Qntan_Mallocator **mallocator;
	struct Qntan_MemArena **mainArena;
	struct Qntan_IoMux **ioMultiplexer;
	struct Qntan_Executor **ioWorker;
	struct Qntan_Networker **networker;
};










/*
 * Returns ptr to statically allocated mimetype.  */
char* fileExtToMime( char const*ext );










int initEnv( struct EnvAndDeps* );










struct Qntan_File**
newFileStdlib( struct EnvAndDeps*, FILE*, int takeOwnership, void(**unref)(struct Qntan_File**) );










struct HttpClientReq {
	/**/
	void (*write)( struct HttpClientReq**, char const*buf, int len, int flgs,
		void (*onDone)(int ret,CLOSURE), CLOSURE);
	/**/
	void (*awaitResponseComplete)( struct HttpClientReq**, void(*onDone)(int,CLOSURE), CLOSURE );
	/**/
	void (*pause)( struct HttpClientReq** );
	/**/
	void (*resume)( struct HttpClientReq** );
	/**/
};










struct HttpClientReq_Hdr {
	char *key, *val;
};
struct HttpClientReq_Opts {
	struct EnvAndDeps *deps;
	char const *mthd;
	char const *host;
	uint16_t port;
	int useTls;
	char const *url;
	struct HttpClientReq_Hdr *hdrs;
	int hdrs_cnt;
	/**/
	CLOSURE cls;
	void (*onRspHdr)( CLOSURE, char*proto, int code, char*phrase,
		struct HttpClientReq_Hdr*hdrs, int hdrs_cnt );
	void (*onRspBodyChunk)( CLOSURE, char*buf, int len, int flgs );
	/*
	 * OUTPUT PARAMETER! Will be set by callee. */
	void (*unref)( struct HttpClientReq** );
};
struct HttpClientReq** newHttpClientReq( struct HttpClientReq_Opts* );










struct Qntan_JsonTreeDec** newJsonTreeParser(
	struct EnvAndDeps*,
	void(*onJsonResult)( CLOSURE, int err, void*structQntan_JsonTreeDec_JsonNode, uint64_t errOffs ),
	void (**unref)(struct Qntan_JsonTreeDec**),
	CLOSURE onJsonResultCls
);










struct Qntan_MemArena** newArenaLinkedList( struct EnvAndDeps* );










struct Qntan_TarEnc** newTarEnc( struct EnvAndDeps*, void(*)(CLOSURE,const char*,int,int,void(*)(int,CLOSURE),CLOSURE), CLOSURE);










struct Qntan_TarDec** newTarDec( struct EnvAndDeps*, struct Qntan_File** );










char const*strerrname(int);










#endif /* INCGUARD_d16bcf26aca7174fb8aae7641c828007 */

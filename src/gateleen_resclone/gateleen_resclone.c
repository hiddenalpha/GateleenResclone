/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

/* This Unit */
#include "gateleen_resclone.h"

/* System */
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#if __WIN32
#else
#	include <unistd.h>
#endif

/* Libs */
#include <Garbage.h>

/* Project */
#include "array.h"
#include "mime.h"
#include "util_string.h"


#if __WIN32
#   define PRIuz "%llu"
#   define isatty(FD) 1  /* TODO FUCK stupid systems */
#else
#   define PRIuz "%lu"
#endif

#define FLG_printPath (1<<0)
#define FLG_isTls (1<<1)
#define FLG_isFilterFull (1<<2)
#define FLG_tarEntryIsEof (1<<3)


#define WATCH_STACK_HEIGHT() do{ \
	int mystackframe; \
	/* TODO TODO TODO TODO assert(stackframeofmain - ((uintptr_t)&mystackframe) < 256*1024);*/ \
}while(0)


#define DloadNode_mAGIC 0xBC9DF815
#define DEFINE_DloadNode(D, S) struct DloadNode*const D=(void*)S;do{ \
	assert(D); assert(D->mAGIC == DloadNode_mAGIC); }while(0)
typedef struct DloadNode {
	unsigned mAGIC;
	int coroState;
	struct Pull *pull;
	struct Resclone *resclone;
	struct Qntan_Mallocator **mallocator;
	struct Qntan_JsonTreeDec **jsonParser;  void (*jsonParser_unref)(struct Qntan_JsonTreeDec**);
	/**/
	char *path;  int path_len;
	struct HttpClientReq **req;  void (*req_unref)(struct HttpClientReq**);
	int httpRspCode;
	char *rspBody; int rspBody_len, rspBody_cap;
	struct Qntan_JsonTreeDec_JsonNode *json;
	int iChild;
	/**/
	void (*onDone)(int,CLOSURE);  CLOSURE onDoneArg;
	/**/
} DloadNode;


#define Put_mAGIC 0x5DBCA443
#define DEFINE_Put(D, S) struct Put*const D=(void*)S;do{ \
	assert(D); assert(D->mAGIC == Put_mAGIC); }while(0)
typedef struct Put {
	unsigned mAGIC;
	int coroState;
	int flg;
	struct Push *push;
	struct Qntan_Mallocator **mallocator;
	/**/
	void (*onDone)(int,Qntan_Cls);  Qntan_Cls onDoneArg;
	/**/
	struct HttpClientReq **req;  void (*req_unref)(struct HttpClientReq**);
	int httpRspCode;
	int buf_cap, buf_len;
	char buf[65000];
} Put;


#define Push_mAGIC 0x2732F801
#define DEFINE_Push(D, S) struct Push*const D=(void*)S;do{ \
	assert(D); assert(D->mAGIC == Push_mAGIC); }while(0)
typedef struct Push {
	unsigned mAGIC;
	int coroState;
	/**/
	struct Resclone *resclone;
	struct Qntan_Mallocator **mallocator;
	struct Qntan_File **file;  void(*file_unref)(struct Qntan_File**);
	struct Qntan_TarDec **tar;
	struct Qntan_TarDec_Hdr *tarHdr;
} Push;


#define Pull_mAGIC 0x2106C38D
#define DEFINE_Pull(D, S) struct Pull*const D=(void*)S;do{ \
	assert(D); assert(D->mAGIC == Pull_mAGIC); }while(0)
typedef struct Pull {
	unsigned mAGIC;
	int coroState;
	/**/
	struct DloadNode rootDloadNode;
} Pull;


#define Resclone_mAGIC 0x0390B548
#define DEFINE_Resclone(D, S) struct Resclone*const D=(void*)S;do{ \
	assert(D); assert(D->mAGIC == Resclone_mAGIC); }while(0)
typedef struct Resclone {
	unsigned mAGIC;
	int flg;
	int argc; char**argv;
	int exitCode;
	enum OpMode mode;
	/* the url specified in argv */
	char *url;
	/* url, but the parsed parts. */
	char *urlStorage; int urlStorage_len;
	char *host, *path;
	int port;
	/** Array of regex patterns to use per segment. */
	regex_t *filter;
	size_t filter_len;
	/* Path to archive file to use. Using stdin/stdout if NULL. */
	char *file;
	FILE *fileHdl;
	union {
		struct Pull pull;
		struct Push push;
	};
	struct Qntan_TarEnc **tar;
	/**/
	struct EnvAndDeps deps;
	/**/
	uintptr_t envMem[64];
	/**/
} Resclone;


static void downloadDirNode( int, CLOSURE, void(*)(int,CLOSURE), CLOSURE );
static void downloadFileNode( int, CLOSURE, void(*)(int,CLOSURE), CLOSURE );
static void putFile_kontinue( int, Qntan_Cls );
static void pushTar( int, CLOSURE );


static uintptr_t stackframeofmain;


static void
printHelp( void ){
    printf("%s%s%s",
        "  \n"
        "  ", strrchr(__FILE__,'/')+1, " - " STR_QUOT(PROJECT_VERSION) "\n"
        "  \n"
        "  Options:\n"
        "  \n"
        "    --pull|--push\n"
        "        Choose to download or upload.\n"
        "  \n"
        "    --url <url>\n"
        "        Root node of remote tree.\n"
        "  \n"
        "    --filter-part <path-filter>\n"
        "        Regex pattern applied as predicate to the path starting after\n"
        "        the path specified in '--url'. Each path segment will be\n"
        "        handled as its individual pattern. If there are longer paths to\n"
        "        process, they will be accepted, as long they at least\n"
        "        start-with specified filter.\n"
        "        Example:  /foo/[0-9]+/bar\n"
        "  \n"
        "    --filter-full <path-filter>\n"
        "        Nearly same as '--filter-part'. But paths with more segments\n"
        "        than the pattern, will be rejected.\n"
        "  \n"
        "    --file <path.tar>\n"
        "        (optional) Path to the archive file to read/write. Defaults to\n"
        "        stdin/stdout if ommitted. Special value \"-\" means to use\n"
        "        stdin/stdout, even a tty got detected.\n"
        "  \n"
        "    --print-path\n"
        "        Print path for every entry actually taken. This does NOT\n"
        "        include intermediates like collections or those which responded\n"
        "        non-200 codes.\n"
        "  \n"
    );
}


static int
parseArgs(
	Resclone *resclone,
	char **url,
	regex_t **filter,
	size_t *filter_cnt
){
	int argc = resclone->argc;  resclone->argc = 0;
	char **argv = resclone->argv; resclone->argv = NULL;
    ssize_t err;
    char *filterRaw = NULL;
	char *urlArg = NULL;
    if( argc == -1 ){ // -1 indicates the call to free our resources. So simply jump
        err = 0; goto fail;    // to 'fail' because that has the same effect.
    }
    *url = NULL;
    *filter = NULL;
    *filter_cnt = 0;

    for( int i=1 ; i<argc ; ++i ){
        char *arg = argv[i];
        if( !strcmp(arg,"--help") ){
            printHelp();
            err = -1; goto fail;
        }else if( !strcmp(arg,"--pull") ){
            if( resclone->mode ){
                fprintf(stderr,"%s\n","EINVAL: Mode already specified. Won't set '--pull'.");
                err = -1; goto fail;
            }
            resclone->mode = MODE_FETCH;
        }else if( !strcmp(arg,"--push") ){
            if( resclone->mode ){
                fprintf(stderr,"%s\n","EINVAL: Mode already specified. Won't set '--push'.");
                err = -1; goto fail;
            }
            resclone->mode = MODE_PUSH;
        }else if( !strcmp(arg,"--url") ){
            if(!( arg=argv[++i]) ){
                fprintf(stderr,"%s\n","EINVAL: Arg '--url' needs a value.");
                err = -1; goto fail;
            }
            urlArg = arg;
        }else if( !strcmp(arg,"--filter-full") ){
            if(!( arg=argv[++i] )){
                fprintf(stderr,"%s\n","EINVAL: Arg '--filter-full' needs a value.");
                err = -1; goto fail; }
            if( filterRaw ){
                fprintf(stderr,"%s\n","EINVAL: Cannot use '--filter-full' because a filter is already set.");
                err=-1; goto fail; }
            filterRaw = arg;
			resclone->flg |= FLG_isFilterFull;
        }else if( !strcmp(arg,"--filter-part") ){
            if(!( arg=argv[++i] )){
                fprintf(stderr,"%s\n","EINVAL: Arg '--filter-part' needs a value.");
                err = -1; goto fail; }
            if( filterRaw ){
                fprintf(stderr,"%s\n","EINVAL: Cannot use '--filter-part' because a filter is already set.");
                err = -1; goto fail; }
            filterRaw = arg;
			resclone->flg &= ~FLG_isFilterFull;
        }else if( !strcmp(arg,"--file") ){
            if(!( arg=argv[++i]) ){
                fprintf(stderr,"%s\n","EINVAL: Arg '--file' needs a value.");
                err = -1; goto fail;
            }
            resclone->file = arg;
        }else if( !strcmp(arg,"--print-path") ){
            resclone->flg |= FLG_printPath;
        }else{
            fprintf(stderr,"%s%s\n", "EINVAL: Unknown arg ",arg);
            err = -1; goto fail;
        }
    }

    if( resclone->mode != MODE_PUSH && resclone->mode != MODE_FETCH ){
        fprintf(stderr,"EINVAL: One of --push or --pull required.\n");
        err = -1; goto fail;
    }

    if( !urlArg ){
        fprintf(stderr,"EINVAL: Arg --url missing.\n");
        err = -1; goto fail;
    }

    int urlFromArgs_len = strlen(urlArg);
    if( ((urlArg)[urlFromArgs_len-1]) != '/' ){
        uint_t url_len = urlFromArgs_len + 1;
        *url = malloc(url_len+1); /* TODO Mallocator */
        memcpy(*url, urlArg, urlFromArgs_len);
        (*url)[url_len-1] = '/';
        (*url)[url_len] = '\0';
    }else{
        *url = strdup(urlArg); /* TODO Mallocator */
    }

    if( filterRaw ){
        uint_t buf_len = strlen(filterRaw);
        char *buf = malloc(1 + buf_len + 2);
        buf[0] = '_'; // <- Match whole segment.
        memcpy(buf+1, filterRaw, buf_len+1);
        char *beg, *end;
        size_t filter_cap = 0;
        end = buf+1; // <- Initialize at begin for 1st iteration.
        for( uint_t iSegm=0 ;; ++iSegm ){
            for( beg=end ; *beg=='/' ; ++beg ); // <- Search for begin and ..
            for( end=beg ; *end!='/' && *end!='\0' ; ++end ); // <- .. end of current segment.
            char origBeg = beg[-1];
            char origSep = *end;
            char origNext = end[1];
            beg[-1] = '^'; // <- Add 'match-start' so we MUST match whole segment.
            *end = '$';    // <- Add 'match-end' so we must match whole segment.
            end[1] = '\0'; // <- Temporary terminate to compile segment only.
            if( iSegm >= filter_cap ){
                filter_cap += 8;
                void *tmp = realloc(*filter, filter_cap*sizeof**filter);
                fprintf(stderr, "%s%u%s%p\n",
                    "[DEBUG] realloc(NULL, ", (unsigned)(filter_cap*sizeof**filter)," ) -> ", tmp);
                if( tmp == NULL ){
                    fprintf(stderr, "realloc(" PRIuz "): %s\n", filter_cap*sizeof**filter, strerror(errno));
                    err = -ENOMEM; goto fail; }
                *filter = tmp;
            }
            fprintf(stderr, "%s%d%s%s%s\n", "[DEBUG] filter[", iSegm, "] -> '", beg-1, "'");
            err = regcomp((*filter)+iSegm, beg-1, REG_EXTENDED);
            if( err ){
                fprintf(stderr, "regcomp(%s): " PRIuz "\n", beg, err);
                err = -1; goto fail; }
            /* Restore surrounding stuff. */
            beg[-1] = origBeg;
            *end = origSep; /* <- Restore tmp 'end-of-match' ($) */
            end[1] = origNext; /* <- Restore tmp termination. */
            if( *end == '\0' ){ /* EOF */
                *filter_cnt = iSegm +1;
                *filter = realloc(*filter, *filter_cnt *sizeof(**filter)); /* Trim result. */
                assert(*filter != NULL);
                free(buf); buf = NULL;
                break;
            }
        }
    }

    if( resclone->mode == MODE_PUSH && *filter ){
        fprintf(stderr, "%s\n", "EINVAL: Filtering not supported for push mode.");
        err = -1; goto fail;
    }

    return 0;
fail:
    free(*url); *url = NULL;
    for( uint_t i=0 ; i<*filter_cnt ; ++i ){
        regfree(&(filter[0][i]));
    }
    *filter_cnt = 0;
    free(*filter); *filter = NULL;
    return err;
}


/**
 * @param url - The input to parse
 * @param host_beg - will be set to where the host begins.
 * @param host_len - Will be set to how long host is.
 * @param port - Will be set to the port. Or will have the same value if
 *      no port present. */
static int
parseUrl(
	char const*url, int url_len,
	int*proto_beg, int*proto_len,
	int*host_beg, int*host_len,
	int*port,
	int*path_beg
){
	LOGT("[TRACE] %s()\n", __func__);
	int i = 0;
	*proto_beg = 0;
	for(;; ++i ){
		if( i >= url_len ){
			LOGD("EINVAL: %.*s\n\t@ %s:%d (%s)\n",  url_len, url, __FILE__, __LINE__, __func__);
			return -EINVAL;
		}
		if( i == 0 && url[i] == 'h' ) continue;
		if( i == 1 && url[i] == 't' ) continue;
		if( i == 2 && url[i] == 't' ) continue;
		if( i == 3 && url[i] == 'p' ) continue;
		if( i == 4 && url[i] == ':' ){ *proto_len = i - *proto_beg; continue; }
		if( i == 4 && url[i] == 's' ) continue;
		if( i == 5 && url[i] == ':' ){ *proto_len = i - *proto_beg; continue; }
		if( (          i == 5) && url[i] == ':' ) continue;
		if( (i == 5 || i == 6) && url[i] == '/' ) continue;
		if( (i == 6 || i == 7) && url[i] == '/' ) continue;
		*host_beg = i;
		break;
	}
	for(;; ++i ){
		if( i >= url_len ){
			LOGD("EINVAL: %.*s\n\t@ %s:%d (%s)\n",  url_len, url, __FILE__, __LINE__, __func__);
			return -EINVAL;
		}
		if( (url[i] >= '0' && url[i] <= '9')
		||  (url[i] >= 'A' && url[i] <= 'Z')
		||  (url[i] >= 'a' && url[i] <= 'z')
		||  url[i] == '-' ||  url[i] == '_' ||  url[i] == '.'
		) continue;
		*host_len = i - *host_beg;
		break;
	}
	for(;; ++i ){
		if( i >= url_len ){
			LOGD("EINVAL: %.*s\n\t@ %s:%d (%s)\n",  url_len, url, __FILE__, __LINE__, __func__);
			return -EINVAL;
		}
		if( url[i] == ':' ){ /* port */
			int port_beg = (i += 1);
			for(;; ++i ){
				if( i >= url_len ){
					LOGD("EINVAL: %.*s\n\t@ %s:%d (%s)\n",
						url_len, url, __FILE__, __LINE__, __func__);
					return -EINVAL;
				}
				if( url[i] < '0' || url[i] > '9' ) break;
			}
			char *end;
			int tmp = strtol(url+port_beg, &end, 10);
			if( !end ){
				LOGD("EINVAL: %.*s\n\t@ %s:%d (%s)\n",  url_len, url, __FILE__, __LINE__, __func__);
				return -EINVAL;
			}
			*port = tmp;
			i = end - url;
		}
		break;
	}
	*path_beg = i;
	return 0;
}


static inline void
DloadNode_dtor( struct DloadNode*this ){
	assert(this->mAGIC == DloadNode_mAGIC);
	this->mAGIC = 0;
	if( this->jsonParser )
		this->jsonParser_unref(this->jsonParser);
	if( this->path )
		FN_Mallocator_realloc(this->mallocator, this->path, this->path_len+1, 0);
	if( this->req )
		this->req_unref(this->req);
	if( this->rspBody )
		FN_Mallocator_realloc(this->mallocator, this->rspBody, this->rspBody_cap, 0);
// TODO unused?	if( this->json )
// TODO unused?		this->json_unref(this->json);
}


static void
DloadNode_unref( struct DloadNode*this ){
	struct Qntan_Mallocator **mallocator;
	DloadNode_dtor(this);
	FN_Mallocator_realloc(this->mallocator, this, sizeof*this, 0);
}


static void
onDloadFileResponseHeader( CLOSURE _, char*, int code, char*, struct HttpClientReq_Hdr*, int ){
	LOGT("[TRACE] @ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
	DEFINE_DloadNode(dload, _);
	WATCH_STACK_HEIGHT();
	assert(code >= 100 && code <= 999);
	dload->httpRspCode = code;
}


static void
onDloadFileResponseBody( CLOSURE _, char*buf, int len, int flgs ){
	LOGT("[TRACE] @ %s:%d %s(%s)\n", __FILE__, __LINE__, __func__, (flgs&4)?"EOF":"");
	int const FLG_EOF = 4;
	DEFINE_DloadNode(dload, _);
	WATCH_STACK_HEIGHT();
	if( len < 0 ){ /* ERROR */
		downloadFileNode(len, _, NULL, 0);
		return;
	}
	if( len > 0 ){ /* has body chunk */
		if( dload->rspBody_cap < dload->rspBody_len + len ){
			size_t const newSz = dload->rspBody_len + len + 1024;
			void *tmp = FN_Mallocator_realloc(dload->mallocator,
				dload->rspBody, dload->rspBody_cap, newSz);
			if( !tmp ){
				LOGD("%s: mallocator.realloc()\n\t@ %s:%d (%s)\n",
					strerrname(errno), __FILE__, __LINE__, __func__);
				exit(1); /*TODO*/
			}
			dload->rspBody = tmp;
			dload->rspBody_cap = newSz;
		}
		memcpy(dload->rspBody + dload->rspBody_len, buf, len);
		dload->rspBody_len += len;
	}
	if( flgs & FLG_EOF ){ /* is last buf */
		downloadFileNode(0, _, NULL, 0);
	}
}


static void
fSxrNEglvGxB11GJq( int e, CLOSURE _ ){ if( e < 0 ) downloadFileNode(e, _, NULL, 0); }
static void
fKsP6BezIDEMcaSG6( int e, CLOSURE _ ){ downloadFileNode(e, _, NULL, 0); }
/**/
static void
downloadFileNode( REGISTER int err, CLOSURE _, void(*onDone)(int,CLOSURE), CLOSURE onDoneArg ){
	DEFINE_DloadNode(dload, _);
	DEFINE_Resclone(resclone, dload->resclone);
	WATCH_STACK_HEIGHT();
	#define CORO_STATE dload->coroState
	enum { begin=0, onFileResponseComplete, onTarHeaderWritten, onTarEntryFullyWritten, };
	switch( CORO_STATE ){case begin:{
		assert(err == 0);
		assert(dload->path);
		assert(!dload->onDone);
		assert(onDone);
		dload->onDone = onDone;
		dload->onDoneArg = onDoneArg;
		if( resclone->flg & FLG_printPath ){
			LOGI("GET %s\n", dload->path);
		}
		struct HttpClientReq_Opts opts = {
			.deps = &resclone->deps,
			.mthd = "GET",
			.host = resclone->host,
			.port = resclone->port,
			.useTls = !!(resclone->flg & FLG_isTls),
			.url = dload->path,
			.cls = (CLOSURE)dload,
			.onRspHdr = onDloadFileResponseHeader,
			.onRspBodyChunk = onDloadFileResponseBody,
		};
		dload->req = newHttpClientReq(&opts);
		dload->req_unref = opts.unref;  assert(opts.unref);
		if( !dload->req ){ err = -errno;
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		CORO_STATE = onFileResponseComplete;
		FN_HttpClientReq_write(dload->req, NULL, 0, 4, fSxrNEglvGxB11GJq, _);
		return;
	}case onFileResponseComplete:{
		if( err < 0 ){
			LOGE("%s: httpReq.write() '%.*s'\n\t@ %s:%d (%s)\n",
				strerrname(-err), dload->path_len, dload->path, __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		assert(err == 0);
		if( dload->httpRspCode != 200 ){
			/* Ugh? Just one request earlier, server said there's a directory on
			 * that URL. Nevermind. Just skip it and at least download the other
			 * stuff. */
			LOGI("[INFO ] Skip HTTP %d -> '%.*s'\n", dload->httpRspCode, dload->path_len, dload->path);
			err = 0; goto resolveWithErr;
		}
	}{
		int const rootPath_len = strlen(resclone->path);
		assert(resclone->path[rootPath_len-1] == '/');
		struct Qntan_TarEnc_Hdr hdr = {
			.path = dload->path + rootPath_len,
			.path_len = dload->path_len - rootPath_len,
			.mode = 0644,
			.mTimeEpchSec = time(NULL),
			.nBodyOctets = dload->rspBody_len,
			.linkType = 0x30, /* aka 'file' */
		};
		CORO_STATE = onTarHeaderWritten;
		FN_TarEnc_nextEntry(resclone->tar, &hdr, fKsP6BezIDEMcaSG6, _);
		return;
	}case onTarHeaderWritten:{
		CORO_STATE = onTarEntryFullyWritten;
		FN_TarEnc_write(resclone->tar, dload->rspBody, dload->rspBody_len, 4,
			fKsP6BezIDEMcaSG6, _);
		return;
	}case onTarEntryFullyWritten:{
		if( err != dload->rspBody_len ){
			if( err >= 0 ){ err = -EIO; }
			LOGD("%s: %s\n\t@ %s:%d (%s)\n",
				strerrname(-err), FN_TarEnc_getLastErrorStr(resclone->tar),
				__FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		err = 0;
	}resolveWithErr:{
		assert(dload->mAGIC == DloadNode_mAGIC);
		void (*onDone)(int,CLOSURE) = dload->onDone;
		CLOSURE onDoneArg = dload->onDoneArg;
		DloadNode_unref(dload);
		onDone(err, onDoneArg);
		return;
	}}
	LOGD("assert(s != %d)\n\t@ %s:%d (%s)\n", CORO_STATE, __FILE__, __LINE__, __func__); assert(0);
	#undef CORO_STATE
}


static void
onDloadDirResponseHeader( CLOSURE _, char*, int code, char*, struct HttpClientReq_Hdr*, int ){
	LOGT("[TRACE] @ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
	DEFINE_DloadNode(dload, _);
	dload->httpRspCode = code;
}


static void
onDloadDirResponseBody( CLOSURE _, char*buf, int len, int flgs ){
	LOGT("[TRACE] @ %s:%d %s(%s)\n", __FILE__, __LINE__, __func__, (flgs&4)?"EOF":"");
	int const FLG_EOF = 4;
	DEFINE_DloadNode(dload, _);
	if( len < 0 ){ /* ERROR */
		downloadDirNode(len, _, NULL, 0);
		return;
	}
	if( len > 0 ){ /* has body chunk */
		if( dload->rspBody_cap < dload->rspBody_len + len ){
			size_t newSz = dload->rspBody_len + len + 1024;
			void *tmp = FN_Mallocator_realloc(dload->mallocator,
				dload->rspBody, dload->rspBody_cap, newSz);
			if( !tmp ){
				LOGD("%s: mallocator.realloc()\n\t@ %s:%d (%s)",
					strerrname(errno), __FILE__, __LINE__, __func__);
				exit(1); /*TODO*/
			}
			dload->rspBody = tmp;
			dload->rspBody_cap = newSz;
		}
		memcpy(dload->rspBody + dload->rspBody_len, buf, len);
		dload->rspBody_len += len;
	}
	if( flgs & FLG_EOF ){ /* is last buf */
		downloadDirNode(0, _, NULL, 0);
	}
}


static void
onDirectoryJsonResult( CLOSURE _, int err, void*json_, uint64_t errOff ){
	DEFINE_DloadNode(dload, _);
	if( err != 0 ){
		LOGE("Json parse fail at off %lu:\n%.*s\n\t@ %s:%d (%s)\n",
			errOff, MIN(dload->rspBody_len, 4096), dload->rspBody, __FILE__, __LINE__, __func__);
		exit(1); /*TODO*/
	}
	/* TODO clarify, if the object stays valid beyond return. */
	dload->json = (struct Qntan_JsonTreeDec_JsonNode*)json_;
}


static void
fI3aAUG5XeM97YSaG( int e, CLOSURE _ ){ if( e < 0 ) downloadDirNode(e, _, NULL, 0); }
static void
fgn8myHh133WklV03( int e, CLOSURE _ ){ downloadDirNode(e, _, NULL, 0); }
/**/
static void
downloadDirNode( REGISTER int err, CLOSURE _, void(*onDone)(int,CLOSURE), CLOSURE onDoneArg ){
	DEFINE_DloadNode(dload, _);
	DEFINE_Pull(pull, dload->pull);
	DEFINE_Resclone(resclone, dload->resclone);
	WATCH_STACK_HEIGHT();
	#define CORO_STATE dload->coroState
	enum { begin=0, onResponseComplete, sbLILGAv8I0XmQdfH, nextChild, };
	switch( CORO_STATE ){case begin:{
		assert(err == 0);
		assert(dload->path);
		assert(onDone);
		assert(!dload->onDone);
		dload->onDone = onDone;
		dload->onDoneArg = onDoneArg;
		struct HttpClientReq_Opts opts = {
			.deps = &resclone->deps,
			.mthd = "GET",
			.host = resclone->host,
			.port = resclone->port,
			.useTls = !!(resclone->flg & FLG_isTls),
			.url = dload->path,
			.cls = (CLOSURE)dload,
			.onRspHdr = onDloadDirResponseHeader,
			.onRspBodyChunk = onDloadDirResponseBody,
		};
		dload->req = newHttpClientReq(&opts);
		dload->req_unref = opts.unref;  assert(opts.unref);
		if( !dload->req ){ err = -errno;
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		CORO_STATE = onResponseComplete;
		FN_HttpClientReq_write(dload->req, NULL, 0, 4, fI3aAUG5XeM97YSaG, _);
		return;
	}case onResponseComplete:{
		if( err < 0 ){
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		assert(err == 0);
		if( dload->httpRspCode != 200 ){
			LOGW("[WARN ] Skip HTTP %d -> '%.*s'\n",
				dload->httpRspCode, dload->path_len, dload->path);
			err = 0; goto resolveWithErr;
		}
	}/*parse response JSON*/{
		DEFINE_Resclone(resclone, dload->resclone);
		assert(dload->rspBody_len > 0);
		dload->jsonParser = newJsonTreeParser(
			&resclone->deps, onDirectoryJsonResult, &dload->jsonParser_unref, _);
		CORO_STATE = sbLILGAv8I0XmQdfH;
		FN_JsonTreeDec_write(dload->jsonParser, dload->rspBody, dload->rspBody_len, 4,
			fgn8myHh133WklV03, _);
		return;
	}case sbLILGAv8I0XmQdfH:{
		if( err != dload->rspBody_len ){
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			err = (err < 0) * err + (err >= 0) * -EIO; goto resolveWithErr;
		}
		assert(dload->json);
	}/* verify json root */{
		#define JSON (dload->json)
		if( JSON->type != '{' ){
			LOGW("[WARN] Skip: Json root expected to be object. But found type '%c'\n", JSON->type);
			err = 0; goto resolveWithErr;
		}
		if( JSON->childs_len != 1 ){
			LOGW("[WARN] Skip: Json root expected to contain exactly ONE entry. But found %d\n",
				JSON->childs_len);
			err = 0; goto resolveWithErr;
		}
		#undef JSON
	}/* verify the lonely entry */{
		#define ARRAY (dload->json->childs[0])
		if( ARRAY->type != '[' ){
			LOGW("[WARN] Skip: rsp[\"%.*s\"] expected to be array. But found type '%c' in:\n%.*s\n",
				ARRAY->key_len, ARRAY->key, ARRAY->type,
				MIN(dload->rspBody_len, 4096), dload->rspBody);
			err = 0; goto resolveWithErr;
		}
	}/*forEach child*/{
		dload->iChild = -1;
		FALL;
	}case nextChild:nextChild:{
		if( err < 0 ){
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		dload->iChild += 1;
		if( dload->iChild >= ARRAY->childs_len )
			goto onAllChildsRecursed;
		#define CHILD (dload->json->childs[0]->childs[dload->iChild])
		if( CHILD->type != 's' ){
			LOGW("[WARN] Skip: rsp[\"%.*s\"][%d] expected to be string. But found type '%c' in:\n%.*s\n",
				ARRAY->key_len, ARRAY->key, dload->iChild, CHILD->type,
				MIN(dload->rspBody_len, 4096), dload->rspBody);
			dload->iChild += 1;
			err = 0; goto nextChild;
		}
		if( CHILD->valStr_len < 1 ){
			LOGW("[WARN] Skip: rsp[\"%.*s\"][%d] expected NOT empty. In:\n%.*s\n",
				ARRAY->key_len, ARRAY->key, dload->iChild,
				MIN(dload->rspBody_len, 4096), dload->rspBody);
			dload->iChild += 1;
			err = 0; goto nextChild;
		}
		#undef ARRAY
	}/*recurse into current child*/{
		DEFINE_Resclone(resclone, dload->resclone);
		DloadNode *child = FN_Mallocator_realloc(dload->mallocator, NULL, 0, sizeof*child);
		*child = (struct DloadNode){
			.mAGIC = DloadNode_mAGIC,
			.pull = pull,
			.resclone = resclone,
			.mallocator = dload->mallocator,
		};
		int const isSlashNeeded = (1
			&& (dload->path[dload->path_len-1] != '/')
			&& (CHILD->valStr[0] != '/')
		);
		child->path_len = dload->path_len + CHILD->valStr_len + !!isSlashNeeded;
		child->path = FN_Mallocator_realloc(child->mallocator, NULL, 0, child->path_len +1);
		if( !child->path ){ err = -errno;
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		err = snprintf(child->path, child->path_len+1, "%s%s%.*s",
			dload->path, isSlashNeeded?"/":"", CHILD->valStr_len, CHILD->valStr);
		assert(err == child->path_len);
		/* ready to go down the next level into the recursion (either to dir or
		 * file child) */
		CORO_STATE = nextChild;
		if( CHILD->valStr[CHILD->valStr_len-1] == '/' ){
			downloadDirNode(0, (CLOSURE)child, fgn8myHh133WklV03, _);
		}else{
			downloadFileNode(0, (CLOSURE)child, fgn8myHh133WklV03, _);
		}
		return;
		#undef CHILD
	}onAllChildsRecursed:{
		err = 0;
	}resolveWithErr:{
		assert(dload->mAGIC == DloadNode_mAGIC);
		void (*onDone)(int,CLOSURE) = dload->onDone;
		CLOSURE onDoneArg = dload->onDoneArg;
		if( dload == &pull->rootDloadNode )
			DloadNode_dtor(dload);
		else
			DloadNode_unref(dload);
		onDone(err, onDoneArg);
		return;
	}}
	LOGD("assert(s != %d)\n\t@ %s:%d (%s)\n", CORO_STATE, __FILE__, __LINE__, __func__); assert(0);
	#undef CORO_STATE
}


static void
onDstTarChunk(
	CLOSURE _, char const*buf, int len, int flgs, void(*onDone)(int,CLOSURE), CLOSURE onDoneArg
){
	REGISTER int err; 
	DEFINE_Resclone(resclone, _);
	/*TODO use NON-EvLoop thread*/
	assert(resclone->fileHdl);
	err = fwrite(buf, 1, len, resclone->fileHdl);
	onDone((err != len) * -errno, onDoneArg);
}


static inline void
pullFn( REGISTER int err, CLOSURE _ ){
	DEFINE_Pull(pull, _);
	DEFINE_Resclone(resclone, container_of(pull, Resclone, pull));
	#define CORO_STATE pull->coroState
	enum { begin=0, sGlsFMvmbXCRBhvvn, sORYFewyirNFWty9I, };
	switch( CORO_STATE ){case begin:{
		if( !resclone->file && isatty(1) ){
			fprintf(stderr, "EINVAL: Refuse to write binary data to tty.\n");
			resclone->exitCode = -1;
			return;
		}
		/*prepate destination file */{
			assert(!resclone->fileHdl);
			if( !resclone->file ){ /* just use stdout then */
				resclone->fileHdl = stdout;
			}else{
				resclone->fileHdl = fopen(resclone->file, "wb");
				if( !resclone->fileHdl ){ err = -errno;
					LOGE("%s: %s\n\t@ %s:%d (%s)", strerrname(-err), resclone->file,
						__FILE__, __LINE__, __func__);
					goto resolveWithErr;
				}
			}
		}/* prepate destination tar */{
			assert(!resclone->tar);
			resclone->tar = newTarEnc(&resclone->deps, onDstTarChunk, (CLOSURE)resclone);
			assert(resclone->tar);
		}
		/*initialize entrypoint for recursive downloads*/
		DloadNode *dload = &pull->rootDloadNode;
		assert(dload->mAGIC == 0);
		*dload = (struct DloadNode){
			.mAGIC = DloadNode_mAGIC,
			.resclone = resclone,
			.pull = pull,
			.mallocator = resclone->deps.mallocator,
		};
		dload->path_len = strlen(resclone->path);  assert(dload->path_len > 0);
		dload->path = FN_Mallocator_realloc(dload->mallocator, NULL, 0, dload->path_len +1);
		if( !dload->path ){ err = -errno;
			LOGD("%s: realloc()\n\t@ %s:%d (%s)\n", strerror(-err), __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		memcpy(dload->path, resclone->path, dload->path_len);
		dload->path[dload->path_len] = '\0';
		CORO_STATE = sGlsFMvmbXCRBhvvn;
		downloadDirNode(0, (CLOSURE)dload, pullFn, _);
		return;
	}case sGlsFMvmbXCRBhvvn:{
	}resolveWithErr:{
		if( err != 0 ){
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			exit(1); /*TODO*/
		}
		/* finalize tar archive */
		CORO_STATE = sORYFewyirNFWty9I;
		FN_TarEnc_write(resclone->tar, NULL, 0, 8, pullFn, _);
		return;
	}case sORYFewyirNFWty9I:{
		LOGD("[DEBUG] Done\n");
		return;
	}}
	LOGD("assert(s != %d)\n\t@ %s:%d (%s)\n", CORO_STATE, __FILE__, __LINE__, __func__); assert(0);
	#undef CORO_STATE
}


static void
onUploadFileResponseHeader( CLOSURE _, char*, int code, char*, struct HttpClientReq_Hdr*, int ){
	LOGT("[TRACE] @ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
	DEFINE_Put(put, _);
	assert(code >= 100 && code <= 999);
	put->httpRspCode = code;
	putFile_kontinue(0, _);
}


static void
onUploadFileResponseBody( CLOSURE _, char*buf, int len, int flgs ){
	LOGT("[TRACE] @ %s:%d %s(%s)\n", __FILE__, __LINE__, __func__, (flgs&4)?"EOF":"");
	int const FLG_EOF = 4;
	DEFINE_Put(put, _);
	DEFINE_Push(push, put->push);
	if( len < 0 ){ /* ERROR */
		pushTar(len, (CLOSURE)push);
		return;
	}
	if( len > 0 ){ /* has body chunk */
		/*Not interested in the response body*/
	}
	if( flgs & FLG_EOF ){ /* is last buf */
		pushTar(0, (CLOSURE)push);
	}
}


static void
fGMQ9mgr4jrivzEgn( void*, int err, int flg, Qntan_Cls _ ){
	assert(flg == 0 || flg == 4);
	DEFINE_Put(put, _);
	put->buf_len = err;
	put->flg |= ((!!(flg & 4)) * FLG_tarEntryIsEof);
	putFile_kontinue(err, _);
}
static void
fGrhy7aquM7pdyI5N( int e, CLOSURE c ){ putFile_kontinue(e, c); }
/**/
static void
putFile_kontinue( int err, Qntan_Cls _ ){
	DEFINE_Put(put, _);
	DEFINE_Push(push, put->push);
	WATCH_STACK_HEIGHT();
	#define CORO_STATE put->coroState
	enum { begin=0, sGJXMLgGGtI3vF6AK, se8WE9EdZpCpsb9v4, onResponseComplete, };
	switch( CORO_STATE ){case begin:{
		assert(err == 0);
	}readNextChunk:{
		CORO_STATE = sGJXMLgGGtI3vF6AK;
		FN_TarDec_readBody(push->tar, put->buf, put->buf_cap, fGMQ9mgr4jrivzEgn, (Qntan_Cls)put);
		return;
	}case sGJXMLgGGtI3vF6AK:{
		if( err < 0 ){
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		put->buf_len = err;
		#define IS_SRC_EOF (!!(put->flg & FLG_tarEntryIsEof))
		int const flgs = IS_SRC_EOF * 4;
		CORO_STATE = se8WE9EdZpCpsb9v4;
		FN_HttpClientReq_write(put->req, put->buf, put->buf_len, flgs, fGrhy7aquM7pdyI5N, _);
		return;
	}case se8WE9EdZpCpsb9v4:{
		if( err < 0 ){
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		assert(err == put->buf_len);
		if( IS_SRC_EOF ){
			CORO_STATE = onResponseComplete;
			/* nothing to do right now. Must wait for response. */
			return;
		}
		err = 0;  goto readNextChunk;
		#undef IS_SRC_EOF
	}case onResponseComplete:{
		if( put->httpRspCode != 200 ){
			assert(0);
		}
	}resolveWithErr:{
		assert(put->mAGIC == Put_mAGIC);
		put->mAGIC = 0;
		put->req_unref(put->req);  put->req = NULL;
		void (*onDone)(int,Qntan_Cls) = put->onDone;
		Qntan_Cls onDoneArg = put->onDoneArg;
		FN_Mallocator_realloc(put->mallocator, put, sizeof*put, 0);
		onDone(err, onDoneArg);
		return;
	}}
	LOGD("assert(s != %d)\n\t@ %s:%d (%s)\n", CORO_STATE, __FILE__, __LINE__, __func__); assert(0);
	#undef CORO_STATE
}


static void
putFile(
	Push*push,
	void(*onDone)(int,Qntan_Cls),
	Qntan_Cls onDoneArg
){
	assert(onDone);
	assert(onDoneArg);
	int err;
	DEFINE_Resclone(resclone, push->resclone);
	Put*const put = FN_Mallocator_realloc(push->mallocator, NULL, 0, sizeof*put);
	if( !put ){
		assert(errno > 0); onDone(-errno, onDoneArg); return; }
	*put = (struct Put){
		.mAGIC = Put_mAGIC,
		.push = push,
		.mallocator = push->mallocator,
		.onDone = onDone,
		.onDoneArg = onDoneArg,
		.buf_cap = sizeof put->buf,
	}; assert(put->buf_cap > 8);
	/* need zero-term :( */
	char path[4096];
	assert(push->tarHdr);
	int const basePath_len = strlen(resclone->path);
	int const isSlashNeeded = 1
		&& push->tarHdr->path[push->tarHdr->path_len-1] != '/'
		&& resclone->path[basePath_len-1] != '/' ;
	int const isSlashDoubld = 1
		&& push->tarHdr->path[push->tarHdr->path_len-1] == '/'
		&& resclone->path[basePath_len-1] == '/' ;
	err = snprintf(path, sizeof path, "%s%s%.*s",
		resclone->path,
		isSlashNeeded ? "/" : "",
		push->tarHdr->path_len+(isSlashDoubld?1:0),  push->tarHdr->path-(isSlashDoubld?1:0) );
	if( err >= (int)sizeof path )
		assert(!"TODO_wbOzO0i6GtlEdZo7 PathTooLong");
	if( resclone->flg & FLG_printPath )
		LOGI("PUT %.*s\n", err, path);
	struct HttpClientReq_Opts opts = {
		.deps = &resclone->deps,
		.mthd = "PUT",
		.host = resclone->host,
		.port = resclone->port,
		.useTls = !!(resclone->flg & FLG_isTls),
		.url = path,
		.cls = (CLOSURE)put,
		.onRspHdr = onUploadFileResponseHeader,
		.onRspBodyChunk = onUploadFileResponseBody,
	};
	put->req = newHttpClientReq(&opts);
	put->req_unref = opts.unref;  assert(opts.unref);
	putFile_kontinue(0, QNTAN_CLS(put));
}


static void
fQi5CDMXIbcsTxKLE( int err, struct Qntan_TarDec_Hdr*hdr, Qntan_Cls _ ){
	DEFINE_Push(push, _);
	push->tarHdr = hdr;
	pushTar(err, _);
}
/**/
static void
pushTar( REGISTER int err, CLOSURE _ ){
	DEFINE_Push(push, _);
	DEFINE_Resclone(resclone, push->resclone);
	WATCH_STACK_HEIGHT();
	#define CORO_STATE push->coroState
	enum { begin=0, sRZZjFzmqtjC5n1hU, sPpWeBYwDHNPrAWdE, szjcxqe7Gxw0KFqui, };
	switch( CORO_STATE ){case begin:{
		assert(err == 0);
		if( !resclone->file && isatty(0) ){
			fprintf(stderr, "EINVAL: Refuse to read binary data from tty.\n");
			resclone->exitCode = -1;
			return;
		}
	}/*prepare source*/{
		FILE *f;
		assert(!push->file);
		if( !resclone->file ){ /* just use stdio then */
			f = stdin;
		}else{
			f = fopen(resclone->file, "rb");
			if( !f ){ err = -errno;
				LOGE("%s: %s\n\t@ %s:%d (%s)\n", strerrname(-err), resclone->file,
					__FILE__, __LINE__, __func__);
				goto resolveWithErr;
			}
		}
		push->file = newFileStdlib(&resclone->deps, f, f != stdin, &push->file_unref);
		if( !push->file ){
			assert(errno > 0); err = -errno; goto resolveWithErr; }
		assert(!push->tar);
		push->tar = newTarDec(&resclone->deps, push->file);
		assert(push->tar);
	}nextTarEntry:{
		CORO_STATE = sRZZjFzmqtjC5n1hU;
		FN_TarDec_nextHdr(push->tar, fQi5CDMXIbcsTxKLE, _);
		return;
	}case sRZZjFzmqtjC5n1hU:{
		if( err <= 0 ){
			if( err == 0 ){ /* EOF */
				goto resolveWithErr; }
			LOGD("%s: tar.nextHdr()\n\t@ %s:%d (%s)\n",
				strerrname(-err), __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		assert(err == 1);  assert(push->tarHdr);
		CORO_STATE = szjcxqe7Gxw0KFqui;
		putFile(push, pushTar, _);
		push->tarHdr = NULL; /*just in case, as it isn't valid after return anyway*/
		return;
	}case szjcxqe7Gxw0KFqui:{
		if( err < 0 ){
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			goto resolveWithErr;
		}
		goto nextTarEntry;
	}case sPpWeBYwDHNPrAWdE:{
		assert(0);
	}resolveWithErr:{
		if( err != 0 ){
			LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
			exit(1); /*TODO*/
		}
		LOGD("[DEBUG] Done\n");
		return;
	}}
	LOGD("assert(s != %d)\n\t@ %s:%d (%s)\n", CORO_STATE, __FILE__, __LINE__, __func__); assert(0);
	#undef CORO_STATE
}


static void
run( CLOSURE _ ){
	DEFINE_Resclone(resclone, _);
	REGISTER int err;
	err = parseArgs(resclone, &resclone->url, &resclone->filter, &resclone->filter_len);
	if( err ){
		resclone->exitCode = err;
		return;
	}
	/* SnipSnap URL into individual parts. */
	int const urlLen = strlen(resclone->url);
	int proto_beg, proto_len, host_beg, host_len, path_beg;
	err = parseUrl(resclone->url, urlLen,
		&proto_beg, &proto_len, &host_beg, &host_len, &resclone->port, &path_beg);
	if( err < 0 ){
		LOGD("\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
		return;
	}
	assert(err == 0);
	int const urlCap = urlLen + 4;
	resclone->urlStorage_len = urlCap;
	resclone->urlStorage = FN_Mallocator_realloc(
		resclone->deps.mallocator, NULL, 0, resclone->urlStorage_len);
	if( !resclone->urlStorage ){
		err = -errno;
		LOGD("%s: realloc()\n\t@ %s:%d (%s)\n", strerrname(-err), __FILE__, __LINE__, __func__);
		return;
	}
	char *it = resclone->urlStorage;
	{
		memcpy(it, resclone->url + proto_beg, proto_len); it += proto_len;
		it++[0] = '\0';
	}{
		resclone->host = it;
		memcpy(it, resclone->url + host_beg, host_len); it += host_len;
		it++[0] = '\0';
	}{
		resclone->path = it;
		memcpy(it, resclone->url + path_beg, urlLen - path_beg); it += urlLen - path_beg;
		it++[0] = '\0';
	}{
		assert(it - resclone->urlStorage < urlLen + 4);
	}
	if( resclone->mode == MODE_FETCH ){
		Pull*const pull = &resclone->pull;
		assert(pull->mAGIC == 0);
		*pull = (struct Pull){
			.mAGIC = Pull_mAGIC,
		};
		pullFn(0, (CLOSURE)pull);
	}else if( resclone->mode == MODE_PUSH ){
		Push*const push = &resclone->push;
		assert(push->mAGIC == 0);
		*push = (struct Push){
			.mAGIC = Push_mAGIC,
			.resclone = resclone,
			.mallocator = resclone->deps.mallocator,
		};
		pushTar(0, (CLOSURE)push);
	}else{
		assert(!"unreachable");
	}
}


int
gateleenResclone_run( int argc, char**argv ){
	#if !NDEBUG
	int blubb;  stackframeofmain = (uintptr_t)&blubb;
	#endif
	REGISTER int err;
	Resclone *resclone = &(Resclone){
		.mAGIC = Resclone_mAGIC,
		.argc = argc,
		.argv = argv,
	};
	err = initEnv(&resclone->deps, resclone->envMem, sizeof resclone->envMem);
	if( err ){
		LOGD("\t@ %s:%d\n", __FILE__, __LINE__); goto resolveWithErr;
	}
	FN_EvLoop_enque(resclone->deps.env, run, (CLOSURE)resclone);
	FN_EvLoop_runUntilDone(resclone->deps.env);
	err = resclone->exitCode;
resolveWithErr:
	return err;
}


int
gateleenResclone_main( int argc, char**argv ){
	LOGT("[TRACE] %s()\n", __func__);
	int ret;
	ret = gateleenResclone_run(argc, argv);
	if( ret < 0 ) ret = -ret;
	return (ret > 127) ? 1 : ret;
}


/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

/* This Unit */
#include "gateleen_resclone.h"

/* System */
#include <assert.h>
#include <errno.h>
#include <libgen.h>
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
#   define FMT_SIZE_T "%llu"
#   define isatty(FD) 1  /* TODO FUCK stupid systems */
#else
#   define FMT_SIZE_T "%lu"
#endif
#define ERR_PARSE_DIR_LIST -2

#if !NDEBUG
#	define IF_DBG(expr) expr
#else
#	define IF_DBG(expr)
#endif


#define FLG_printPath (1<<0)
#define FLG_isTls (1<<1)
#define FLG_isFilterFull (1<<2)


typedef  struct Resclone  Resclone;
typedef  struct ClsDload  ClsDload;
typedef  struct Upload  Upload;
typedef  struct ResourceDir  ResourceDir;
typedef  struct ResourceFile  ResourceFile;
typedef  struct Put  Put;


struct Cls595AB944 /* state of collectResourceIntoMemory() */ {
	unsigned mAGIC;
	int coroState;
	int eno;
	ResourceFile *resourceFile;
	void(*onDone)(int err,void*arg);
	void*onDoneArg;
};


struct ClsB5D40F60/* httpPutEntry */{
	unsigned mAGIC;
	int coroState;
	char *name;  int name_len;
	char *path;  int path_len;
	uint_least64_t nBodyOctets;
	struct Garbage_HttpClientReq **req;
	int readFlgs;
	char *buf;  int buf_cap;
	void(*onDone)(int err,void*arg);
	void*onDoneArg;
};


struct ClsB806037F/* readArchive */{
	unsigned mAGIC;
	int coroState;
	struct Garbage_TarDecHdr *tarHdr;
	void (*onDone)(int,void*);  void *onDoneArg;
};


struct ClsFE7D8786 /* aka copyBufToArchive */ {
	unsigned mAGIC;
	int coroState;
	ResourceFile *resourceFile;
	void(*onDone)(int err,void*arg);
	void*onDoneArg;
};


/**
 * Closure for a download instructed by external caller.
 *
 * It represents the single action of a download action during the whole
 * program livetime. There is either a download OR an upload, but NOT both
 * during process lifetime. */
#define ClsDload_mAGIC 0x63036F77
struct ClsDload {
    unsigned mAGIC;
    struct Resclone *resclone;
    struct Garbage_HttpClientReq **req; /*TODO unref*/
	struct Garbage_TarEnc **tar;
	FILE *tarFile;
    struct archive *dstArchive;
    struct archive_entry *tmpEntry;
    char *archiveFile;
};


/**
 * Is a specific intermediate node in the tree to transfer. Its count is
 * dynamic and gets instantiated in a tree structure. the leaf notes
 * then are `ResourceFile` structure. */
#define ResourceDir_mAGIC 0xE06C2536
struct ResourceDir {
	unsigned mAGIC;
	int eno;
	int state_gateleenResclone_download;
    struct ClsDload *dload;
    struct ResourceDir *parentDir;
	char *path;  int path_len, path_cap;
	short httpRspCode;
    char *rspBody;
    size_t rspBody_len;
    size_t rspBody_cap;
    char *name;
	char **childNames;
	int childNames_len;
	int currChildName;
	/**/
	struct Garbage_JsonTreeParser **jsonParser;
	/**/
	void(*onDone)(int,void*);
	void*onDoneArg;
};


/** Closure for a file download. */
#define ResourceFile_mAGIC 0xEAF41178
struct ResourceFile {
	unsigned mAGIC;
	int eno;
	int state_iterateNextResourceFile;
	int iThisChild; /* indicates that this is the n-th child seen from its parent. */
    struct ResourceDir *resourceDir;
    size_t srcChunkIdx;
	char *path;  int path_len;
	int httpRspCode;
    char *buf;
    int buf_len;
    int buf_memSz;
	/**/
	struct Cls595AB944 cls595AB944;
	struct ClsFE7D8786 clsFE7D8786;
	/**/
	void(*onDone)(int,void*);
	void *onDoneArg;
};


/**
 * Closure for an upload instructed by external caller. */
#define Upload_mAGIC 0x9021B0FE
struct Upload {
	unsigned mAGIC;
	int coroState_push;
    struct Resclone *resclone;
    char *archiveFile;
    struct archive *srcArchive;
	Put *put; /* TODO unused? */
	/**/
	struct ClsB5D40F60 clsB5D40F60;
	struct ClsB806037F clsB806037F;
	/**/
	struct Garbage_TarDec **tar;
	/**/
};


/** Closure for a PUT of a single resource. */
struct Put {
    struct Upload *upload;
    /* Path (relative to rootUrl) of the resource to be uploaded. */
    char *name;
};


/** Main module handle representing an instance. */
#define Resclone_mAGIC 0xA5450000
struct Resclone {
    unsigned mAGIC;
	int flg;
	int argc; char**argv;
	int state_pull;
	int eno;
	int exitCode;
    enum OpMode mode;
    /** Array of regex patterns to use per segment. */
    regex_t *filter;
    size_t filter_len;
    /** Says if only start or whole path needs to match the pattern. */
    int isFilterFull;
    /* Path to archive file to use. Using stdin/stdout if NULL. */
    char *file;
    /*
	 * base URL given from cmdline, but split in parts. */
	char *host, *path;
	int host_len, path_len;
	uint_least16_t port;
    /**/
	union {
		struct ClsDload clsDload;
		struct Upload clsUpload;
	};
    /**/
	struct EnvAndDeps deps;
    /**/
    uintptr_t envMem[64];
    /**/
};


/* Operations ****************************************************************/


TPL_ARRAY(str, char*, 16);


static void collectResourceIntoMemory( struct Cls595AB944* );
static void gateleenResclone_download( ClsDload*, ResourceDir*, char*, void(*)(int,void*), void*);
static void gateleenResclone_download_kontinue( void* );
static void httpPutEntry_kontinue( int, void* );
static void pull( void* );
static void pull_kontinue( int, void* );
static void readArchive_kontinue( int, void* );


static inline struct Resclone*
assert_is_Resclone( void*p, char const*f, int l ){
#if !NDEBUG
	if( !p ){ LOGF("assert(resclone != NULL) @ %s:%d\n", f, l); abort(); }
	Resclone const*const q = p;
	if( q->mAGIC != Resclone_mAGIC ){
		LOGF("assert(mAGIC == Resclone_mAGIC) @ %s:%d\n", f, l); abort(); }
#endif
	return p;
}
#define assert_is_Resclone(p) assert_is_Resclone(p, __FILE__, __LINE__)


static inline struct ClsDload*
assert_is_ClsDload( void*p, char const*f, int l ){
#if !NDEBUG
	if( !p ){ LOGF("assert(clsDload != NULL) @ %s:%d\n", f, l); abort(); }
	ClsDload const*const q = p;
	if( q->mAGIC != ClsDload_mAGIC ){
		LOGF("assert(mAGIC == ClsDload_mAGIC) @ %s:%d\n", f, l); abort(); }
#endif
	return p;
}
#define assert_is_ClsDload(p) assert_is_ClsDload(p, __FILE__, __LINE__)


static inline struct ResourceDir*
assert_is_ResourceDir( void*p, char const*f, int l ){
#if !NDEBUG
	if( !p ){ LOGF("assert(clsDload != NULL) @ %s:%d\n", f, l); abort(); }
	ResourceDir const*const q = p;
	if( q->mAGIC != ResourceDir_mAGIC ){
		LOGF("assert(mAGIC == ResourceDir_mAGIC) @ %s:%d\n", f, l); abort(); }
#endif
	return p;
}
#define assert_is_ResourceDir(p) assert_is_ResourceDir(p, __FILE__, __LINE__)


static inline struct ResourceFile*
assert_is_ResourceFile( void*p, char const*f, int l ){
#if !NDEBUG
	if( !p ){ LOGF("assert(clsDload != NULL) @ %s:%d\n", f, l); abort(); }
	ResourceFile const*const q = p;
	if( q->mAGIC != ResourceFile_mAGIC ){
		LOGF("assert(mAGIC == ResourceFile_mAGIC) @ %s:%d\n", f, l); abort(); }
#endif
	return p;
}
#define assert_is_ResourceFile(p) assert_is_ResourceFile(p, __FILE__, __LINE__)


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
        "        Root node of remote tre\n"
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
                    fprintf(stderr, "realloc("FMT_SIZE_T"): %s\n", filter_cap*sizeof**filter, strerror(errno));
                    err = -ENOMEM; goto fail; }
                *filter = tmp;
            }
            fprintf(stderr, "%s%d%s%s%s\n", "[DEBUG] filter[", iSegm, "] -> '", beg-1, "'");
            err = regcomp((*filter)+iSegm, beg-1, REG_EXTENDED);
            if( err ){
                fprintf(stderr, "regcomp(%s): "FMT_SIZE_T"\n", beg, err);
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
	uint_least16_t*port,
	int*path_beg
){
	LOGT("[TRACE] %s()\n", __func__);
	int i = 0;
	*proto_beg = 0;
	for(;; ++i ){
		if( i >= url_len ){ assert(!"TODO_28UXyd6zEw3fnIib"); }
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
		if( i >= url_len ){ assert(!"TODO_28UXyd6zEw3fnIib"); }
		if( (url[i] >= '0' && url[i] <= '9')
		||  (url[i] >= 'A' && url[i] <= 'Z')
		||  (url[i] >= 'a' && url[i] <= 'z')
		||  url[i] == '-' ||  url[i] == '_' ||  url[i] == '.'
		) continue;
		*host_len = i - *host_beg;
		break;
	}
	for(;; ++i ){
		if( i >= url_len ){ assert(!"TODO_28UXyd6zEw3fnIib"); }
		if( url[i] == ':' ){ /* port */
			int port_beg = (i += 1);
			for(;; ++i ){
				if( i >= url_len ){ assert(!"TODO_28UXyd6zEw3fnIib"); }
				if( url[i] < '0' || url[i] > '9' ) break;
			}
			char *end;
			int tmp = strtol(url+port_beg, &end, 10);
			if( !end ){ assert(!"TODO_T8IZDlPqGlgt3imK"); }
			*port = tmp;
			i = end - url;
		}
		break;
	}
	*path_beg = i;
	return 0;
}


static void
onResourceFileHttpRspHdr(
	const char*proto, int proto_len,
	int rspCode,
	const char*phrase, int phrase_len,
	const struct Garbage_HttpMsg_Hdr*hdrs, int hdrs_cnt,
	struct Garbage_HttpClientReq**_, void*cls_
){
	(void)_;
	LOGT("[TRACE] %s()\n", __func__);
	struct Cls595AB944*const cls = cls_; assert(cls->mAGIC == 0x595AB944);
	ResourceFile*const resourceFile = assert_is_ResourceFile(cls->resourceFile);
	resourceFile->httpRspCode = rspCode;
	if( rspCode != 200 && rspCode != 404 ){
		LOGD("< %.*s %d %.*s\n", proto_len, proto, rspCode, phrase_len, phrase);
		for( int i = 0 ; i < hdrs_cnt ; ++i ){
			LOGD("< %.*s: %.*s\n", hdrs[i].key_len, hdrs[i].key, hdrs[i].val_len, hdrs[i].val);
		}
	}
}


/*
 * Flg 0x4 set means, that this is the last buffer. This is the last callback
 * called for this http message. */
static void
onResourceFileHttpRspBody(
	const char*buf, int buf_len, int flg, struct Garbage_HttpClientReq**_, void*cls_
){
	(void)_;
	LOGT("[TRACE] %s()\n", __func__);
	struct Cls595AB944*const cls = cls_; assert(cls->mAGIC == 0x595AB944);
	ResourceFile*const resourceFile = assert_is_ResourceFile(cls->resourceFile);
	if( buf_len > 0 ){ /* data */
		if( resourceFile->httpRspCode != 200 ) return;
		if( resourceFile->buf_len + buf_len >= resourceFile->buf_memSz ){
			Resclone*const resclone = resourceFile->resourceDir->dload->resclone;
			size_t oldSz = resourceFile->buf_memSz;
			resourceFile->buf_memSz = resourceFile->buf_len + buf_len + 512;
			/* TODO free --vvvvvvvvvvvvvv */
			void*tmp = Mallocator_realloc(resclone->deps.mallocator,
				resourceFile->buf, oldSz, resourceFile->buf_memSz*sizeof*resourceFile->buf);
			if( !tmp ){
				resourceFile->eno = -errno;
				LOGT("%s:\n\t@ %s:%d\n", strerrname(-errno), __FILE__, __LINE__);
				assert(!"TODO_onVtKYESyXQXvCg4");
			}
			resourceFile->buf = tmp;
		}
		memcpy(resourceFile->buf + resourceFile->buf_len, buf, buf_len);
		resourceFile->buf_len += buf_len;
	}
	if( flg & 4 ){ /* EOF */
		collectResourceIntoMemory(cls);
	}
}


static void
onResourceFileError( int retval, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	struct Cls595AB944*const cls = cls_; assert(cls->mAGIC == 0x595AB944);
	cls->eno = retval;
	if( cls->eno ){ LOGT("%s: %.*s\n\t@ %s:%d\n", strerrname(-cls->eno),
		cls->resourceFile->path_len, cls->resourceFile->path, __FILE__, __LINE__); }
	collectResourceIntoMemory(cls);
}


static void
collectResourceIntoMemory( struct Cls595AB944*cls ){
	LOGT("[TRACE] %s()\n", __func__);
	assert(cls->mAGIC == 0x595AB944);
	ResourceFile*const resourceFile = assert_is_ResourceFile(cls->resourceFile);
	ResourceDir*const resourceDir = assert_is_ResourceDir(resourceFile->resourceDir);
	Resclone*const resclone = resourceDir->dload->resclone;

	#define CORO_STATE (cls->coroState)
	enum { begin=0, sgDMgDx6HnAdNWWis, };
	switch( CORO_STATE ){case begin:{
		assert(cls->onDone);
		/**/
		static struct Garbage_HttpClientReq_Mentor reqMentor = {
			.onRspHdr = onResourceFileHttpRspHdr,
			.onRspBody = onResourceFileHttpRspBody,
			.onError = onResourceFileError,
		};
		int const isTls = (resclone->flg & FLG_isTls);
		//LOGD("[DEBUG] dload \"http%s://%s:%d%s\"\n", isTls?"s":"", resclone->host, resclone->port, resourceFile->path);
		struct Garbage_HttpClientReq **req;
		req = newHttpClientReq(&resclone->deps, "GET", resclone->host, resclone->port,
			isTls, resourceFile->path, NULL, 0, &reqMentor, cls);
		if( !req ){ assert(!"TODO_pMYskoj4hmYk1DET"); }
		CORO_STATE = sgDMgDx6HnAdNWWis;
		FN_HttpClientReq_closeSnk(req);
		return;
	}case sgDMgDx6HnAdNWWis:{
		if( cls->eno ){
			LOGT("\t@ %s:%d\n", __FILE__, __LINE__);
		}else if( resclone->flg & FLG_printPath && resourceFile->httpRspCode == 200 ){
			assert(resourceFile->path_len > resclone->path_len);
			LOGI("%.*s\n",
				resourceFile->path_len - resclone->path_len,
				resourceFile->path + resclone->path_len);
		}
	}/*endWithClsEno*/{
		void(*onDone)(int,void*) = cls->onDone;  cls->onDone = NULL;
		assert(onDone);
		onDone(cls->eno, cls->onDoneArg);
		return;
	}}
	LOGD("assert(s != %d) @ %s:%d\n", CORO_STATE, __FILE__, __LINE__); abort();
	#undef CORO_STATE
}


static void
onTarOutChunk(
	void*cls_, char const*buf, int buf_len, int flgs,
	void(*onDone)(int,void*), void*onDoneArg
){
	assert(flgs == 0 || flgs == 4);
	LOGT("[TRACE] %s()\n", __func__);
	int err;
	ResourceFile*const resourceFile = assert_is_ResourceFile(cls_);
	ClsDload*const dload = assert_is_ClsDload(resourceFile->resourceDir->dload);
	if( !dload->tarFile ){
		if( !dload->archiveFile || !strncmp(dload->archiveFile, "-", 2) ){
			dload->tarFile = stdout;
		}else{
			dload->tarFile = fopen(dload->archiveFile, "wb");
			if( !dload->tarFile ){
				err = -errno;
				LOGE("%s: \"%s\"\n\t@ %s:%d\n", strerrname(-err), dload->archiveFile,
					__FILE__, __LINE__);
				dload->resclone->exitCode = 1;
				return;
			}
		}
	}
	if( buf_len > 0 ){
		err = fwrite(buf, 1, buf_len, dload->tarFile);
		if( err != buf_len ){ err = -errno;
			LOGE("%s:\n\t@ %s:%d\n", strerrname(-err), __FILE__, __LINE__);
			dload->resclone->exitCode = 1;
			return;
		}
	}
	if( flgs & 4 ){
		fclose(dload->tarFile);  dload->tarFile = NULL;
	}
	onDone(buf_len, onDoneArg);
}


static void
copyBufToArchive_kontinue( int err, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	struct ClsFE7D8786*const cls = cls_;  assert(cls->mAGIC == 0xFE7D8786);
	ResourceFile*const resourceFile = cls->resourceFile;
	ClsDload*const dload = resourceFile->resourceDir->dload;

	#define CORO_STATE (cls->coroState)
	enum { begin=0, sE2iEFO6D7uNJ2A85, sX7Vo8fJSSxJAJtHp, };
	switch( CORO_STATE ){case begin:{
		Resclone*const resclone = resourceFile->resourceDir->dload->resclone;
		if( !dload->tar ){
			dload->tar = newTarEnc(&resclone->deps, onTarOutChunk, resourceFile);
			assert(dload->tar && "TODO_ZHqUG7mMYW5ySOdp");
		}
		char *fileName = resourceFile->path + resclone->path_len;
		int fileName_len = resourceFile->path_len - resclone->path_len;
		assert(fileName[0] == '/');
		fileName += 1;  fileName_len -= 1;
		assert(fileName[0] != '/');
		struct Garbage_TarEncHdr tarHdr = {
			.path = fileName,
			.path_len = fileName_len,
			.mode = 0644,
			.mTimeEpchSec = time(NULL),
			.nBodyOctets = resourceFile->buf_len,
		};
		CORO_STATE = sE2iEFO6D7uNJ2A85;
		(*dload->tar)->nextEntry(dload->tar, &tarHdr, copyBufToArchive_kontinue, cls);
		return;
	}case sE2iEFO6D7uNJ2A85:{
		if( err < 0 ){
			LOGW("%s: TODO_vnbvTTOLx5A3g9uf\n\t@ %s:%d\n",
				strerrname(-err), __FILE__, __LINE__);
		}
		if( resourceFile->buf_len > 0 ){
			CORO_STATE = sX7Vo8fJSSxJAJtHp;
			(*dload->tar)->write(dload->tar, resourceFile->buf, resourceFile->buf_len,
				copyBufToArchive_kontinue, cls);
			return;
		}else err = 0;
		FALL;
	}case sX7Vo8fJSSxJAJtHp:{
		if( err < 0 ){
			LOGD("%s: %s\n\t@ %s:%d\n", strerrname(-err),
				(*dload->tar)->getLastErrorStr(dload->tar), __FILE__, __LINE__);
			goto endWithErr;
		}
		if( err != resourceFile->buf_len ){
			LOGD("%s: \n\t@ %s:%d\n", strerrname(-err), __FILE__, __LINE__);
			err = -EIO; goto endWithErr;
		}
		resourceFile->buf_len = 0;

		err = 0;
	}endWithErr:{
		cls->mAGIC = 0;
		void(*onDone)(int,void*) = cls->onDone;  cls->onDone = NULL;
		onDone(err, cls->onDoneArg);
		return;
	}}
	LOGD("assert(s != %d) @ %s:%d\n", CORO_STATE, __FILE__, __LINE__); abort();
	#undef CORO_STATE
}


static inline void
copyBufToArchive(
	ResourceFile*resourceFile,
	void(*onDone)(int err,void*arg),
	void*onDoneArg
){
	LOGT("[TRACE] %s()\n", __func__);
	struct ClsFE7D8786*const cls = &resourceFile->clsFE7D8786;
	assert(cls->mAGIC == 0 && "clsFE7D8786 already in use.");
	*cls = (struct ClsFE7D8786){
		.mAGIC = 0xFE7D8786,
		.resourceFile = resourceFile,
		.onDone = onDone,
		.onDoneArg = onDoneArg,
	};
	copyBufToArchive_kontinue(0, cls);
}


/** @return 0:Reject, 1:Accept, <0:ERROR */
static int
pathFilterAcceptsEntry(
	ClsDload*dload, ResourceDir*resourceDir, char const*nameOrig, int name_len
){
	LOGT("[TRACE] %s()\n", __func__);
	int err;
	char *name = Mallocator_realloc(dload->resclone->deps.mallocator, NULL, 0, name_len+1);
	if( !name ){ assert(errno > 0); return -errno; }
	memcpy(name, nameOrig, name_len);
	// TODO needed?  name[name_len] = '\0';

    if( dload->resclone->filter ){
        // Count parents to find correct regex to apply.
        uint_t idx = 0;
        for( ResourceDir*it=resourceDir->parentDir ; it ; it=it->parentDir ){ ++idx; }
        // Check if we even have such a long filter at all.
        if( idx >= dload->resclone->filter_len ){
            if( dload->resclone->flg & FLG_isFilterFull ){
                //LOGD("[DEBUG] Path longer than --filter-full -> reject.\n");
                err = 0; goto endFn;
            }else{
                //LOGD("[DEBUG] Path longer than --filter-part -> accept.\n");
                err = 1; goto endFn;
            }
        }
        // We have a regex. Setup the check.
        int restoreEndSlash = 0;
        if( name[name_len-1] == '/' ){
            restoreEndSlash = !0;
            name[name_len-1] = '\0';
        }
        LOGD("[DEBUG] idx=%u name=\"%s\"\n", idx, name);
        regex_t *filterArr = dload->resclone->filter;
        regex_t *r = filterArr + idx;
        err = regexec(r, name, 0, 0, 0);
        if( !err ){
            //LOGD("[DEBUG] Segment accepted by filter.\n");
            err = 1; /* fall to restoreEndSlash */
        }else if( err == REG_NOMATCH ){
            //LOGD("[DEBUG] Segment rejected by filter.\n");
            err = 0; /* fall to restoreEndSlash */
        }else{
            LOGE("[ERROR] regexec(rgx, \"%.*s\") -> %d\n", name_len, name, err);
            err = -1; /* fall to restoreEndSlash */
        }
        if( restoreEndSlash ){
            name[name_len-1] = '/';
        }
        goto endFn;
    }

    err = 1; /* accept by default */
endFn:
	Mallocator_realloc(dload->resclone->deps.mallocator, name, name_len+1, 0);
	return err;
}


static void
onDloadPushIoTask( void(*task)(void*arg), void*arg, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	ClsDload*const dload = assert_is_ClsDload(cls_);
	FN_ThreadPool_enque(dload->resclone->deps.ioWorker, task, arg);
}


static void
onDloadError( int retval, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	LOGW("%s:\n\t@ %s:%d\n", strerrname(-retval), __FILE__, __LINE__);
	__asm__("int $3;nop;");/*TODO*/
	resourceDir->eno = retval;
	assert(*resourceDir->dload->req);
	assert((*resourceDir->dload->req)->pause);
	gateleenResclone_download_kontinue(resourceDir);
}


static void
onResourceDirHttpRspHdr(
	const char*proto, int proto_len,
	int rspCode,
	const char*phrase, int phrase_len,
	const struct Garbage_HttpMsg_Hdr*hdrs, int hdrs_cnt,
	struct Garbage_HttpClientReq**_,
	void*cls_
){
	(void)_;
	LOGT("[TRACE] %s()\n", __func__);
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	if( rspCode != 200 ){
		LOGD("< %.*s %d %.*s\n", proto_len, proto, rspCode, phrase_len, phrase);
		for( int i=0 ; i < hdrs_cnt ; ++i ){
			LOGD("< %.*s: %.*s\n", hdrs[i].key_len, hdrs[i].key, hdrs[i].val_len, hdrs[i].val);
		}
	}
	if( rspCode != 200 ){ resourceDir->dload->resclone->exitCode = -1; }
	resourceDir->httpRspCode = rspCode;
}


static void
iterateNextResourceFile( int err, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	ResourceFile*const resourceFile = assert_is_ResourceFile(cls_);
	assert(resourceFile->onDone);
	enum { begin=0, sIWV6nnx7FyogLUBz, spz2UiLEf04xmhO14, };
	#define CORO_STATE (resourceFile->state_iterateNextResourceFile)
	switch( CORO_STATE ){case begin:{
		ResourceDir *const resourceDir = resourceFile->resourceDir;
		ClsDload *const dload = resourceDir->dload;
		char const *name = resourceDir->childNames[resourceDir->currChildName];  assert(name);
		int const name_len = strlen(name);
		err = pathFilterAcceptsEntry(dload, resourceDir, name, name_len);
		if( err < 0 ){ /* ERROR */
			goto endWithErr;
		}else if( err == 0 ){ /* REJECT */
			//LOGI("[INFO ] Skip     '%s%.*s'  (filtered)\n", dload->url, name_len , name);
			err = -10000-__LINE__;
			LOGD("TODO: choose better errno(%d)\n\t@ %s:%d \n", err, __FILE__, __LINE__);
			goto endWithErr;
		}
		assert(name[name_len-1] != '/' && "Why the F** did you call this function then?!?");
		int requiredLen = resourceDir->path_len + name_len + (sizeof"\0"-1);
		assert(!resourceFile->path);
		void *tmp = Mallocator_realloc(dload->resclone->deps.mallocator,
			resourceFile->path, resourceFile->path_len+(sizeof"\0"-1), requiredLen +(sizeof"\0"-1));
		if( !tmp ){ err = -errno;
			LOGD("%s:\n\t@ %s:%d", strerrname(-err), __FILE__, __LINE__);
			goto endWithErr;
		}
		resourceFile->path = tmp;
		resourceFile->path_len = requiredLen;
		err = sprintf(resourceFile->path, "%.*s%.*s",
			resourceDir->path_len, resourceDir->path,
			name_len, name);
		assert(resourceFile->path[err] != '/');
		resourceFile->path_len = err;
		resourceFile->buf_len = 0; /* <- Reset before use. */
		struct Cls595AB944*clsSub = &resourceFile->cls595AB944;
		assert(clsSub->mAGIC == 0 && "Bad: Closure busy.");
		*clsSub = (struct Cls595AB944){
			.mAGIC = 0x595AB944,
			.resourceFile = resourceFile,
			.onDone = iterateNextResourceFile,
			.onDoneArg = resourceFile,
		};
		CORO_STATE = sIWV6nnx7FyogLUBz;
		collectResourceIntoMemory(clsSub);
		return;
	}case sIWV6nnx7FyogLUBz:{
		if( err ){ goto endWithErr; }
		if( resourceFile->httpRspCode == 200 ){
			CORO_STATE = spz2UiLEf04xmhO14;
			copyBufToArchive(resourceFile, iterateNextResourceFile, resourceFile);
			return;
		}
		FALL;
	}case spz2UiLEf04xmhO14:{
		/* assume err is already set? */
	}endWithErr:{
		void(*onDone)(int,void*) = resourceFile->onDone; resourceFile->onDone = NULL;
		assert(onDone);
		onDone(err, resourceFile->onDoneArg);
	}}
	#undef CORO_STATE
}


static void
fmkKBgMWbr6Scr748( int err, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	if( err < 0 ){ assert(!"TODO_NsR9vBN74QJzVZX5"); }
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	FN_HttpClientReq_resume(resourceDir->dload->req);
}


static void
onRspJsonParsed( void*cls_, int err, void*json_ ){
	LOGT("[TRACE] %s()\n", __func__);
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	if( err ){
		resourceDir->httpRspCode = ERR_PARSE_DIR_LIST;
		LOGW("%s: Failed to parse response JSON\n\t@ %s:%d\n",
			strerrname(-err), __FILE__, __LINE__);
		goto endFn;
	}
	struct Garbage_JsonTreeParser_JsonNode const*const rootNode = json_;
	struct Garbage_JsonTreeParser_JsonNode const*json = rootNode;
	assert(json->type == '{');
	if( json->childs_len != 1 ){
		LOGW("EINVAL: JSON root expected ONE child but got %d\n", json->childs_len);
		resourceDir->httpRspCode = ERR_PARSE_DIR_LIST;
		goto endFn;
	}
	if( json->childs[0]->type != '[' ){
		LOGE("EINVAL: json['%.*s'] expected to be an array. But is not.\n",
			json->childs[0]->key_len, json->childs[0]->key);
		resourceDir->httpRspCode = ERR_PARSE_DIR_LIST;
		goto endFn;
	}
	int const numChilds = json->childs[0]->childs_len;
	int totalBufLen = 0;
	for( int i = 0 ; i < numChilds ; ++i ){
		totalBufLen += json->childs[0]->childs[i]->valStr_len + 1;
	}
	totalBufLen += (numChilds +1) * sizeof(char*);
	resourceDir->childNames = Mallocator_realloc(
		resourceDir->dload->resclone->deps.mallocator, NULL, 0, totalBufLen);
	if( !resourceDir->childNames ){ assert(errno > 0); err = -errno;
		LOGD("%s:\n\t@ %s:%d", strerror(-err), __FILE__, __LINE__);
		resourceDir->eno = -errno; goto endFn;
	}
	char **nxtPtr = resourceDir->childNames;
	char *nxtName = ((char*)resourceDir->childNames) + (numChilds + 1) * sizeof*nxtPtr;
	for( int i = 0 ; i < numChilds ; ++i ){
		if( json->childs[0]->childs[i]->type != 's' ){
			LOGE("ERROR: %.*s['%u'] expected to be string. But is not.\n",
				json->childs[0]->childs[i]->key_len, json->childs[0]->childs[i]->key, i);
			resourceDir->httpRspCode = ERR_PARSE_DIR_LIST;
			goto endFn;
		}
		char const* childName = json->childs[0]->childs[i]->valStr;
		int const childName_len = json->childs[0]->childs[i]->valStr_len;
		//LOGD("Child: \"%.*s\"\n", childName_len, childName);
		memcpy(nxtName, childName, childName_len);
		nxtName[childName_len] = '\0';
		*nxtPtr++ = nxtName;
		nxtName += childName_len +1;
	}
	resourceDir->childNames_len = numChilds;
	resourceDir->eno = 0;
endFn:
	/* continue dload where we left off. eno gets passed within cls itself. */
	gateleenResclone_download_kontinue(resourceDir);
}


static void onJsonParseError( void*cls_, uintptr_t errOff ){
	LOGT("[TRACE] %s()\n", __func__);
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	int len = (resourceDir->rspBody_len > 200) ? 200 : resourceDir->rspBody_len;
	LOGE("Failed to parse JSON around offset %llu:\n%.*s%s\n\t@ %s:%d\n",
		errOff, len, resourceDir->rspBody,
		(len != (int)resourceDir->rspBody_len) ? "....." : "",
		__FILE__, __LINE__);
}


static void
onDloadRspBody(
	const char*buf, int buf_len, int flgs,
	struct Garbage_HttpClientReq**req,
	void*cls_
){
	LOGT("[TRACE] %s(l=%d, f=0x%X)\n", __func__, buf_len, flgs);
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	if( resourceDir->httpRspCode != 200 ){ return; }
	if( buf_len < 0 ){ /* error */
		LOGD("TODO: %d\n\t@ %s:%d\n", buf_len, __FILE__, __LINE__); abort();
		return;
	}
	if( !resourceDir->jsonParser ){
		resourceDir->jsonParser = newJsonTreeParser(
			&resourceDir->dload->resclone->deps,
			onRspJsonParsed, onJsonParseError, resourceDir);
		assert(resourceDir->jsonParser && "TODO_GtLfTLwu2EjSFiyW");
	}
	if( buf_len > 0 || flgs & 4 ){
		FN_HttpClientReq_pause(req);
		/* just-a-hack. Attach buf here, so we can (hopefully) print it on error.
		 * WARN: This likely will report wrong position, if buffer is chunked! */
		if( !resourceDir->rspBody ){
			resourceDir->rspBody = (void*)buf;
			resourceDir->rspBody_len = buf_len;
		}
		/**/
		FN_JsonTreeParser_write(resourceDir->jsonParser,
			(void*)buf, buf_len, flgs & 4, fmkKBgMWbr6Scr748, resourceDir);
		return;
	}
	assert(!"unreachable");
}


static void
faoAd1hYqUJczMbAZ( int i, void*v ){
	LOGT("[TRACE] %s()\n", __func__);
	ResourceDir*const resourceDir = assert_is_ResourceDir(v);
	resourceDir->eno = i;
	gateleenResclone_download_kontinue(resourceDir);
}
static void
gateleenResclone_download_kontinue( void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	int err;
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	/* TODO is dload maybe the wrong context for some cases in here? */
	ClsDload*const dload = assert_is_ClsDload(resourceDir->dload);
	#define CORO_STATE (resourceDir->state_gateleenResclone_download)
	#define CORO_GOTO(S) do{goto S;}while(0)
	enum { begin=0, sywgOcZGKrgTP3onF, onChildDone, };
	switch( CORO_STATE ){case begin:{
		Resclone*const resclone = assert_is_Resclone(dload->resclone);

		/* setup URL */
		int const name_len = (resourceDir->name) ? strlen(resourceDir->name) : 0;
		{
			/* need parent path, plus our own name. */
			int path_cap = 0
				+ ((resourceDir->parentDir) ? resourceDir->parentDir->path_len : resclone->path_len)
				+ name_len
				+ (sizeof"/\0"-1);
			char *tmp = Mallocator_realloc(dload->resclone->deps.mallocator, NULL, 0, path_cap);
			if( !tmp ){ assert(!"TODO_9f0C0WeHGN2J9enx"); }
			char *it = tmp;
			if( !resourceDir->parentDir ){
				/* looks like the root call */
				memcpy(it, resclone->path, resclone->path_len);  it += resclone->path_len;
			}else{
				ResourceDir*const parent = resourceDir->parentDir;
				memcpy(it, parent->path, parent->path_len);  it += parent->path_len;
			}
			if( !resourceDir->name ){
				if( it[-1] != '/' ) *it++ = '/';
			}else{
				memcpy(it, resourceDir->name, name_len);  it += name_len;
			}
			it[0] = '\0';
			assert(it - tmp < path_cap);
			resourceDir->path = tmp;
			resourceDir->path_len = it - tmp;
			resourceDir->path_cap = it - tmp;
		}
		if( resourceDir->parentDir ){
			err = pathFilterAcceptsEntry(dload, resourceDir->parentDir,
				resourceDir->name, name_len);
			if( err <= 0 ){ /* error or reject */
				resourceDir->eno = err;  goto endWithEno;
			}
		}
		static struct Garbage_HttpClientReq_Mentor requestMentor = {
			.pushIoTask = onDloadPushIoTask,
			.onError = onDloadError,
			.onRspHdr = onResourceDirHttpRspHdr,
			.onRspBody = onDloadRspBody,
		};
		int isTls = (resclone->flg & FLG_isTls);
		//LOGD("[DEBUG] dload \"http%s://%s:%d%s\"\n", isTls?"s":"",
		//	resclone->host, resclone->port, resourceDir->path);
		struct Garbage_HttpMsg_Hdr hdrs[] = {
			{ .key = "Accept", .key_len = 6,
			  .val = "application/json", .val_len = 16, },
		};
		dload->req = newHttpClientReq(&dload->resclone->deps,
			"GET", resclone->host, resclone->port, isTls, resourceDir->path,
			hdrs, sizeof hdrs/sizeof*hdrs, &requestMentor, resourceDir);
		if( !dload->req ){ assert(!"TODO_9A7x4x7VPEDHXEkX"); }
		FN_HttpClientReq_closeSnk(dload->req);
		CORO_STATE = sywgOcZGKrgTP3onF;
		return;
	}case sywgOcZGKrgTP3onF:{
		if( resourceDir->httpRspCode == ERR_PARSE_DIR_LIST ){
			LOGT("\t@ %s:%d\n", __FILE__, __LINE__);
			resourceDir->eno = 0; CORO_GOTO(endWithEno);
		}
		if( resourceDir->httpRspCode != 200 ){
			/* Ugh? Just one request earlier, server said there's a directory on
			 * that URL. Nevermind. Just skip it and at least download the other
			 * stuff. */
			LOGI("[INFO ] Skip HTTP %d -> '%s'\n", resourceDir->httpRspCode, resourceDir->path);
			resourceDir->eno = 0; CORO_GOTO(endWithEno);
		}
	}nextChild:{
		assert(resourceDir->childNames);
		if( resourceDir->currChildName >= resourceDir->childNames_len ){
			/* no more childs */
			resourceDir->eno = 0;  goto endWithEno;
		}
		char *childName = resourceDir->childNames[resourceDir->currChildName];
		assert(childName);
		int const childName_len = strlen(childName);
		/* Look if Gateleen reports a 'directory'. So we have to "go recursive"
		 * now. Have fun reading asynchronous code, operating recursively :D */
		if( childName[childName_len-1] == '/' ){
			//LOGD("[DEBUG] Scan     '%s%.*s'\n", dload->url, childName_len, childName);
			assert(childName[childName_len] == '\0');
			CORO_STATE = onChildDone;
			gateleenResclone_download(dload, resourceDir, childName,
				faoAd1hYqUJczMbAZ, resourceDir);
			return;
		}
		/* NOT a dir (aka collection), so we assume leaf (aka file/resource) */
		ResourceFile *resourceFile = NULL;
		resourceFile = Mallocator_realloc(dload->resclone->deps.mallocator,
			NULL, 0, sizeof*resourceFile);
		if( !resourceFile ){ assert(errno > 0); err = -errno;
			LOGD("%s:\n\t@ %s:%d\n", strerrname(-err), __FILE__, __LINE__);
			resourceDir->eno = err; goto endWithEno;
		}
		*resourceFile = (ResourceFile){
			.mAGIC = ResourceFile_mAGIC,
			.resourceDir = resourceDir,
			.onDone = faoAd1hYqUJczMbAZ,
			.onDoneArg = resourceDir,
		};
		CORO_STATE = onChildDone;
		iterateNextResourceFile(0, resourceFile);
		return;
	}case onChildDone:{
		if( resourceDir->eno ){
			LOGT("\t@ %s:%d\n", __FILE__, __LINE__);
			goto endWithEno;
		}
		resourceDir->currChildName += 1;
		goto nextChild;
	}endWithEno:{
		void(*onDone)(int,void*) = resourceDir->onDone;  resourceDir->onDone = NULL;
		onDone(resourceDir->eno, resourceDir->onDoneArg);
		return;
	}}
	LOGD("assert(s != %d)  %s:%d\n", CORO_STATE, __FILE__, __LINE__); abort();
	#undef CORO_GOTO
	#undef CORO_STATE
}


/** Gets called for every resource to scan/download.
 * HINT: Gets called recursively. */
static inline void
gateleenResclone_download(
	ClsDload*dload,
	ResourceDir*parentResourceDir,
	char*entryName,
	void(*onDone)(int,void*),
	void*onDoneArg
){
	LOGT("[TRACE] %s()\n", __func__);
	assert_is_ClsDload(dload);
	assert(onDone);

	/* closure */
	/* TODO free ------vvvvvvvvvvv */
	ResourceDir *const resourceDir = Mallocator_realloc(
		dload->resclone->deps.mallocator, NULL, 0, sizeof*resourceDir);
	if( !resourceDir ){ assert(!"TODO_Hahq0XcbwgpB2tSd"); }
	*resourceDir = (ResourceDir){
		.mAGIC = ResourceDir_mAGIC,
		.dload = dload,
		.parentDir = parentResourceDir,
		.name = (entryName) ? strdup(entryName) : NULL,
		/* TODO mallocator ---^^^^^^ */
		/* TODO free ---------^^^^^^ */
		.onDone = onDone,
		.onDoneArg = onDoneArg,
	};

	gateleenResclone_download_kontinue(resourceDir);
}


static void
f7WyuHF2bvoqlU4RT(
	const char*proto, int proto_len,
	int rspCode,
	const char*phrase, int phrase_len,
	const struct Garbage_HttpMsg_Hdr*hdrs, int hdrs_cnt,
	struct Garbage_HttpClientReq**_,
	Garbage_Closure _2
){
	(void)_; (void)_2;
	LOGT("[TRACE] %s()\n", __func__);
	if( rspCode != 200 && rspCode != 404 ){
		LOGD("< %.*s %d %.*s\n", proto_len, proto, rspCode, phrase_len, phrase);
		for( int i = 0 ; i < hdrs_cnt ; ++i ){
			LOGD("< %.*s: %.*s\n", hdrs[i].key_len, hdrs[i].key, hdrs[i].val_len, hdrs[i].val);
		}
	}
}


static void
fPHXEGzplo6ORfXqF(
	const char*_1, int _2, int flg,
	struct Garbage_HttpClientReq**_3,
	void*cls_
){
	(void)_1;(void)_2;(void)_3;
	LOGT("[TRACE] %s()\n", __func__);
	if( flg & 4 ){ /* EOF */
		struct ClsB5D40F60*const cls = cls_;  assert(cls->mAGIC == 0xB5D40F60);
		httpPutEntry_kontinue(0, cls_);
	}
}


static void
ffLJT5IsjD1Oow5PK( int retval, void*_ ){
	(void)_;
	LOGD("[DEBUG] http.onError(%s)\n", strerrname(-retval));
	assert(!"TODO_WIyp7RBHR6erQutf");
}


static void
fbF6TjHxYye01NFq1(
	void*buf,
	int buf_len,
	int flgs,
	void*cls_
){
	LOGT("[TRACE] %s()\n", __func__);
	struct ClsB5D40F60*const cls = cls_;  assert(cls->mAGIC == 0xB5D40F60);
	assert(buf == cls->buf);
	cls->readFlgs = flgs;
	httpPutEntry_kontinue(buf_len, cls);
}


static void
fmN0tlcnbkXpujQD5( int err, void*buf, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	struct ClsB5D40F60*const cls = cls_;  assert(cls->mAGIC == 0xB5D40F60);
	assert(buf == cls->buf);
	httpPutEntry_kontinue(err, cls);
}


static void
httpPutEntry_kontinue( int err, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	struct ClsB5D40F60*const cls = cls_;  assert(cls->mAGIC == 0xB5D40F60);
	Upload*const upload = container_of(cls, Upload, clsB5D40F60); assert(upload->mAGIC == Upload_mAGIC);
	#define CORO_STATE (cls->coroState)
	enum { begin=0, sXIO8Xcsyt2gAInQS, s5D9EhJ0HSWC5rcYp, sELzjNenIaxsHsqGG, };
	switch( CORO_STATE ){case begin:{
		Resclone*const resclone = upload->resclone;
		assert(!cls->path);
		cls->path_len = resclone->path_len +(sizeof"/"-1) + cls->name_len;
		cls->path = Mallocator_realloc(resclone->deps.mallocator,
			NULL, 0, cls->path_len + 1);
		if( !cls->path ){ assert(!"TODO_EZ6bgYM5YzxtSzbE"); }
		char *name = cls->name;
		int name_len = cls->name_len;
		while( name[0] == '/' || (name[0] == '.' && name[1] == '/') ){
			assert(name_len > 0);
			name += 1;
			name_len -= 1;
		}
		err = snprintf(cls->path, cls->path_len+1, "%.*s/%.*s",
			resclone->path_len, resclone->path,
			cls->name_len, cls->name);
		assert(err <= cls->path_len+1);
		char contentLenStr[16];
		err = snprintf(contentLenStr, sizeof contentLenStr, "%llu",
			(long long unsigned)cls->nBodyOctets);
		if( err > (int)sizeof contentLenStr ){ assert(!"TODO_mAaGCDaGtz3m46Z7"); abort(); }
		struct Garbage_HttpMsg_Hdr hdrs[] = {
			{ .key = "Content-Type", .key_len = 12,
			  .val = "application/json", .val_len = 16, },
			{ .key = "Content-Length", .key_len = 14,
			  .val = contentLenStr, .val_len = err, },
		};
		static struct Garbage_HttpClientReq_Mentor mentor = {
			.onRspHdr = f7WyuHF2bvoqlU4RT,
			.onRspBody = fPHXEGzplo6ORfXqF,
			.onError = ffLJT5IsjD1Oow5PK,
		};
		assert(!cls->req);
		cls->req = newHttpClientReq(&upload->resclone->deps,
			"PUT", resclone->host, resclone->port, (resclone->flg & FLG_isTls), cls->path,
			hdrs, sizeof hdrs/sizeof*hdrs, &mentor, cls);
		if( !cls->req ){ assert(!"TODO_HU3Q1feXqU38jY8e"); }
	}getNextBodyChunk:{
		if( !cls->buf ){
			cls->buf_cap = 128*1024*1024;
			cls->buf = Mallocator_realloc(upload->resclone->deps.mallocator,
				NULL, 0, cls->buf_cap);
			if( !cls->buf ){ assert(!"TODO_NLHQdKZhfQPhmMxz"); }
		}
		CORO_STATE = sXIO8Xcsyt2gAInQS;
		(*upload->tar)->readBody(upload->tar, cls->buf, cls->buf_cap, fbF6TjHxYye01NFq1, cls);
		return;
	}case sXIO8Xcsyt2gAInQS:{
		if( err < 0 ){ assert(!"TODO_7trvuvsCBrQwhpoC"); }
		assert(cls->readFlgs == 0 || cls->readFlgs == 4);
		if( err > 0 || cls->readFlgs & 4 ){
			assert(err <= cls->buf_cap);
			CORO_STATE = s5D9EhJ0HSWC5rcYp;
			(*cls->req)->write(cls->req, cls->buf, err, cls->readFlgs, fmN0tlcnbkXpujQD5, cls);
			return;
		}
		FALL;
	}case s5D9EhJ0HSWC5rcYp:{
		if(!( cls->readFlgs & 4 )){
			goto getNextBodyChunk;
		}else{
			//LOGD("request complete. wait until httpclient calls us back with response\n");
			CORO_STATE = sELzjNenIaxsHsqGG;
			return;
		}
	}case sELzjNenIaxsHsqGG:{
		/* this request is complete now */
		err = 0;
	}/*endWithErr*/{
		cls->mAGIC = 0;
		(*cls->req)->unref(cls->req);
		assert(cls->buf);
		Mallocator_realloc(upload->resclone->deps.mallocator, cls->buf, cls->buf_cap, 0);
		assert(cls->name);
		Mallocator_realloc(upload->resclone->deps.mallocator, cls->name, strlen(cls->name)+1, 0);
		assert(cls->path);
		Mallocator_realloc(upload->resclone->deps.mallocator, cls->path, cls->path_len+1, 0);
		cls->onDone(err, cls->onDoneArg);
		return;
	}}
	LOGD("assert(s != %d) %s:%d\n", CORO_STATE, __FILE__, __LINE__); abort();
	#undef CORO_STATE
}


static void
httpPutEntry( Upload*upload, char const*name, int name_len, uint_fast64_t nBodyOctets, void(*onDone)(int,void*), void*onDoneArg ){
	LOGT("[TRACE] %s()\n", __func__);
	assert(name); assert(name_len >= 0);
	struct ClsB5D40F60*const cls = &upload->clsB5D40F60;
	assert(cls->mAGIC == 0);
	*cls = (struct ClsB5D40F60){
		.mAGIC = 0xB5D40F60,
		.name_len = name_len,
		.nBodyOctets = nBodyOctets,
		.onDone = onDone,
		.onDoneArg = onDoneArg,
	};
	cls->name = Mallocator_realloc(upload->resclone->deps.mallocator,
		NULL, 0, name_len+1);
	memcpy(cls->name, name, name_len);
	cls->name[name_len] = '\0';
	httpPutEntry_kontinue(0, cls);
}


static void
fm2FnNMu9BBEL9LMg( int err, struct Garbage_TarDecHdr*hdr, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	struct ClsB806037F*const cls = cls_;  assert(cls->mAGIC == 0xB806037F);
	cls->tarHdr = hdr;
	readArchive_kontinue(err, cls);
}


static void
readArchive_kontinue( int err, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	struct ClsB806037F*const cls = cls_;  assert(cls->mAGIC == 0xB806037F);
	Upload*const upload = container_of(cls, Upload, clsB806037F); assert(upload->mAGIC == Upload_mAGIC);
	#define CORO_STATE (cls->coroState)
	enum { begin=0, sG3Oi8pzOqsutt2Fr, stF36OGCqWGZOpdf7, };
	switch( CORO_STATE ){case begin:{
		Resclone*const resclone = assert_is_Resclone(upload->resclone);
		assert(!upload->tar);
		upload->tar = newTarDec(&resclone->deps, resclone->file);
		if( !upload->tar ){ LOGT("\t@ %s:%d\n", __FILE__, __LINE__); err = -1; goto endWithErr; }
		if( err ){
			assert(!err); err = -1; goto endWithErr; }
	}nextArchiveEntry:{
		CORO_STATE = sG3Oi8pzOqsutt2Fr;
		(*upload->tar)->nextHdr(upload->tar, fm2FnNMu9BBEL9LMg, cls);
		return;
	}case sG3Oi8pzOqsutt2Fr:{
		if( err == 0 ){ /*EOF*/ err = 0; goto endWithErr; }
		if( err != 1 ){ assert(!"TODO_Nzaodq0pZpY3X8yo"); }
		//int const filetype = cls->tarHdr->filetype;
		int const isDir = (cls->tarHdr->filetype == 'd');
		int const isRegularFile = (cls->tarHdr->filetype == '0' || cls->tarHdr->filetype == '\0');
		/* Ignore dirs because gateleen doesn't know 'dirs' as such. */
		if( isDir ){ goto nextArchiveEntry; }
		if( !isRegularFile ){
			LOGW("WARN: Ignore non-regular file '%.*s'\n",
				cls->tarHdr->path_len, cls->tarHdr->path);
			goto nextArchiveEntry;
		}
		if( upload->resclone->flg & FLG_printPath ){
			LOGI("%.*s\n", cls->tarHdr->path_len, cls->tarHdr->path);
		}
		CORO_STATE = stF36OGCqWGZOpdf7;
		httpPutEntry(upload, cls->tarHdr->path, cls->tarHdr->path_len, cls->tarHdr->nBodyOctets,
			readArchive_kontinue, cls);
		return;
	}case stF36OGCqWGZOpdf7:{
		if( err ){ assert(!"TODO_SLbwHnsvLFGxfIiq"); }
		goto nextArchiveEntry;
	}endWithErr:{
		cls->mAGIC = 0;
		cls->onDone(err, cls->onDoneArg);
		return;
	}}
	LOGD("assert(s != %d)  %s:%d\n", CORO_STATE, __FILE__, __LINE__); abort();
	#undef CORO_STATE
}


static inline void
readArchive(
	Upload*upload,
	void(*onDone)(int,void*),
	void *onDoneArg
){
	LOGT("[TRACE] %s()\n", __func__);
	struct ClsB806037F*const cls = &upload->clsB806037F;
	assert(cls->mAGIC == 0);
	*cls = (struct ClsB806037F){
		.mAGIC = 0xB806037F,
		.onDone = onDone,
		.onDoneArg = onDoneArg,
	};
	readArchive_kontinue(0, cls);
}


static void
pull_kontinue( int err, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	ClsDload*const dload = assert_is_ClsDload(cls_);
	Resclone*const resclone = assert_is_Resclone(dload->resclone);
	#define CORO_STATE (resclone->state_pull)
	#define CORO_GOTO(S) goto S
	enum { begin=0, sRgcmUWoHED7pgReU, sArUiyl2d1oYejma4, };
	switch( CORO_STATE ){case begin:{
		if( resclone->file == NULL && isatty(1) ){
			LOGE("ERROR: Are you sure you wanna write binary content to tty?\n\t@ %s:%d\n",
				__FILE__, __LINE__);
			resclone->eno = -1; CORO_GOTO(endWithEno);
		}
		CORO_STATE = sArUiyl2d1oYejma4;
		gateleenResclone_download(dload, NULL, NULL, pull_kontinue, dload);
		return;
	}case sArUiyl2d1oYejma4:{
		if( err ){ LOGT("\t@ %s:%d\n", __FILE__, __LINE__); goto endWithEno; }
		if( dload->tar ){
			CORO_STATE = sRgcmUWoHED7pgReU;
			(*dload->tar)->closeSnk(dload->tar, pull_kontinue, dload);
			dload->tar = NULL;
			return;
		}
	}case sRgcmUWoHED7pgReU:{
	}endWithEno:{
		//LOGI("[INFO ] Pull Done with %d\n", resclone->eno);
	}}
	#undef CORO_STATE
	#undef CORO_GOTO
}


static void
pull( void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	Resclone*const resclone = assert_is_Resclone(cls_);
	ClsDload*const dload = &resclone->clsDload;
	assert(dload->mAGIC == 0);
	*dload = (ClsDload){
		.mAGIC = ClsDload_mAGIC,
		.resclone = resclone,
		.archiveFile = resclone->file,
	}; assert_is_ClsDload(dload);
	pull_kontinue(0, dload);
}


static void
push_kontinue( int err, void*Upload_ ){
	LOGT("[TRACE] %s()\n", __func__);
	Upload*const upload = Upload_;  assert(upload->mAGIC == Upload_mAGIC);
	#define CORO_STATE (upload->coroState_push)
	enum { begin=0, shfjzaxi4RzTcUx1O, };
	switch( CORO_STATE ){case begin:{
		CORO_STATE = shfjzaxi4RzTcUx1O;
		readArchive(upload, push_kontinue, upload);
		return;
	}case shfjzaxi4RzTcUx1O:{
		if( upload->srcArchive ){
			assert(!"TODO_B2AAAIBdAADcJAAA");
			//archive_read_free(upload->srcArchive);
		}
		if( err ){ upload->resclone->exitCode = err; }
		return;
	}}
	LOGD("assert(s != %d)  %s:%d\n", CORO_STATE, __FILE__, __LINE__); abort();
	#undef CORO_STATE
}


static void
push( void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	Resclone*const resclone = assert_is_Resclone(cls_);
	Upload*const upload = &resclone->clsUpload;
	assert(upload->mAGIC == 0);
	*upload = (struct Upload){
		.mAGIC = Upload_mAGIC,
		.resclone = resclone, /* TODO remove, bcause container_of is enough */
		.archiveFile = resclone->file,
	};
	push_kontinue(0, upload);
}


static void
fvr4Ls8sH4112Kypd( void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	int err;
	char *url;
	Resclone*const resclone = assert_is_Resclone(cls_);

	err = parseArgs(resclone, &url, &resclone->filter, &resclone->filter_len);
	if( err ){ resclone->exitCode = err; return; }

	/* extract URL parts */
	int proto_beg, proto_len, host_beg, host_len, path_beg;
	uint_least16_t port;
	int const url_len = strlen(url);
	/* TODO do NOT leak 'tmp' */
	char *tmp = Mallocator_realloc(resclone->deps.mallocator,
		NULL, 0, url_len +(sizeof"\0\0\0"-1));
	if( !tmp ){ assert(!"TODO_5hZGZzbhoAepUIUL"); }
	char *it = tmp;
	parseUrl(url, strlen(url), &proto_beg, &proto_len, &host_beg, &host_len, &port, &path_beg);
	int path_len = url_len - path_beg;
	resclone->flg |= FLG_isTls
		* !!(proto_len == 5 && !strncmp(url + proto_beg, "https", proto_len));
	memcpy(it, url + host_beg, host_len);
	(resclone->host = it)[host_len] = '\0';  it += host_len +1;
	memcpy(it, url + path_beg, path_len);
	(resclone->path = it)[path_len] = '\0';  it += path_len +1;
	resclone->host_len = host_len;
	resclone->path_len = path_len;
	resclone->port = port;
	while( resclone->path[resclone->path_len-1] == '/' ){
		assert(resclone->path_len > 0);
		resclone->path_len -= 1;
	}

	assert(resclone->path[0] == '/');
	assert(resclone->path[resclone->path_len-1] != '/');

	resclone->argc = 0;
	resclone->argv = NULL;

	if( resclone->mode == MODE_FETCH ){
		FN_Env_enque(resclone->deps.env, pull, resclone);
	}else if( resclone->mode == MODE_PUSH ){
		FN_Env_enque(resclone->deps.env, push, resclone);
	}else{
		resclone->exitCode = -1; return;
	}
}


int
gateleenResclone_run( int argc, char**argv ){
	LOGT("[TRACE] %s()\n", __func__);
    int err;
    Resclone *resclone = &(Resclone){
        .mAGIC = Resclone_mAGIC,
		.argc = argc,
		.argv = argv,
    };

	if( initEnv(&resclone->deps, resclone->envMem, sizeof resclone->envMem) ){
		LOGD("\t@ %s:%d\n", __FILE__, __LINE__); goto endFn;
	}

	FN_Env_enque(resclone->deps.env, fvr4Ls8sH4112Kypd, resclone);
	FN_Env_runUntilDone(resclone->deps.env);

	err = resclone->exitCode;
    goto endFn;

    assert(!"Unreachable");
endFn:
	/* TODO fix mem leaks */
	//parseArgs(-1, argv, resclone, &resclone->mode, &resclone->url, &resclone->filter,
	//	&resclone->filter_len);
	resclone->mode = MODE_NULL; resclone->file = NULL;
	return err;
}


int
gateleenResclone_main( int argc, char**argv ){
	LOGT("[TRACE] %s()\n", __func__);
    int ret;
    ret = gateleenResclone_run(argc, argv);
    if( ret < 0 ){ ret = 0 - ret; }
    return (ret > 127) ? 1 : ret;
}


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
#else
#   define FMT_SIZE_T "%lu"
#endif
#define ERR_PARSE_DIR_LIST -2

#if !NDEBUG
#	define IF_DBG(expr) expr
#else
#	define IF_DBG(expr)
#endif


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
    char *rootUrl;
	char *url;  int url_len;
	int resUrl_len; /* TODO maybe in wrong closure? */
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
    char *url;  int url_len;  int url_cap;
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
struct Upload {
    struct Resclone *resclone;
    char *rootUrl;
    char *archiveFile;
    struct archive *srcArchive;
    // OBSOLETE CURL *curl;
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
	int state_pull;
	int eno;
	int exitCode;
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
static void pull( void* );


static inline struct Resclone* assert_is_Resclone( void*p, char const*f, int l ){
#if !NDEBUG
	if( !p ){ LOGF("assert(resclone != NULL) @ %s:%d\n", f, l); abort(); }
	Resclone const*const q = p;
	if( q->mAGIC != Resclone_mAGIC ){
		LOGF("assert(mAGIC == Resclone_mAGIC) @ %s:%d\n", f, l); abort(); }
#endif
	return p;
}
#define assert_is_Resclone(p) assert_is_Resclone(p, __FILE__, __LINE__)


static inline struct ClsDload* assert_is_ClsDload( void*p, char const*f, int l ){
#if !NDEBUG
	if( !p ){ LOGF("assert(clsDload != NULL) @ %s:%d\n", f, l); abort(); }
	ClsDload const*const q = p;
	if( q->mAGIC != ClsDload_mAGIC ){
		LOGF("assert(mAGIC == ClsDload_mAGIC) @ %s:%d\n", f, l); abort(); }
#endif
	return p;
}
#define assert_is_ClsDload(p) assert_is_ClsDload(p, __FILE__, __LINE__)


static inline struct ResourceDir* assert_is_ResourceDir( void*p, char const*f, int l ){
#if !NDEBUG
	if( !p ){ LOGF("assert(clsDload != NULL) @ %s:%d\n", f, l); abort(); }
	ResourceDir const*const q = p;
	if( q->mAGIC != ResourceDir_mAGIC ){
		LOGF("assert(mAGIC == ResourceDir_mAGIC) @ %s:%d\n", f, l); abort(); }
#endif
	return p;
}
#define assert_is_ResourceDir(p) assert_is_ResourceDir(p, __FILE__, __LINE__)


static inline struct ResourceFile* assert_is_ResourceFile( void*p, char const*f, int l ){
#if !NDEBUG
	if( !p ){ LOGF("assert(clsDload != NULL) @ %s:%d\n", f, l); abort(); }
	ResourceFile const*const q = p;
	if( q->mAGIC != ResourceFile_mAGIC ){
		LOGF("assert(mAGIC == ResourceFile_mAGIC) @ %s:%d\n", f, l); abort(); }
#endif
	return p;
}
#define assert_is_ResourceFile(p) assert_is_ResourceFile(p, __FILE__, __LINE__)


static void printHelp( void ){
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
        "  \n"
    );
}


static int parseArgs( int argc, char**argv, OpMode*mode, char**url, regex_t**filter, size_t*filter_cnt, int*isFilterFull, char**file ){
    ssize_t err;
    char *filterRaw = NULL;
    if( argc == -1 ){ // -1 indicates the call to free our resources. So simply jump
        err = 0; goto fail;    // to 'fail' because that has the same effect.
    }
    *mode = 0;
    *url = NULL;
    *filter = NULL;
    *filter_cnt = 0;
    *isFilterFull = 0;
    *file = NULL;

    for( int i=1 ; i<argc ; ++i ){
        char *arg = argv[i];
        if( !strcmp(arg,"--help") ){
            printHelp();
            err = -1; goto fail;
        }else if( !strcmp(arg,"--pull") ){
            if( *mode ){
                fprintf(stderr,"%s\n","EINVAL: Mode already specified. Won't set '--pull'.");
                err = -1; goto fail;
            }
            *mode = MODE_FETCH;
        }else if( !strcmp(arg,"--push") ){
            if( *mode ){
                fprintf(stderr,"%s\n","EINVAL: Mode already specified. Won't set '--push'.");
                err = -1; goto fail;
            }
            *mode = MODE_PUSH;
        }else if( !strcmp(arg,"--url") ){
            if(!( arg=argv[++i]) ){
                fprintf(stderr,"%s\n","EINVAL: Arg '--url' needs a value.");
                err = -1; goto fail;
            }
            *url = arg;
        }else if( !strcmp(arg,"--filter-full") ){
            if(!( arg=argv[++i] )){
                fprintf(stderr,"%s\n","EINVAL: Arg '--filter-full' needs a value.");
                err = -1; goto fail; }
            if( filterRaw ){
                fprintf(stderr,"%s\n","EINVAL: Cannot use '--filter-full' because a filter is already set.");
                err=-1; goto fail; }
            filterRaw = arg;
            *isFilterFull = !0;
        }else if( !strcmp(arg,"--filter-part") ){
            if(!( arg=argv[++i] )){
                fprintf(stderr,"%s\n","EINVAL: Arg '--filter-part' needs a value.");
                err = -1; goto fail; }
            if( filterRaw ){
                fprintf(stderr,"%s\n","EINVAL: Cannot use '--filter-part' because a filter is already set.");
                err = -1; goto fail; }
            filterRaw = arg;
            *isFilterFull = 0;
        }else if( !strcmp(arg,"--file") ){
            if(!( arg=argv[++i]) ){
                fprintf(stderr,"%s\n","EINVAL: Arg '--file' needs a value.");
                err = -1; goto fail;
            }
            *file = arg;
        }else{
            fprintf(stderr,"%s%s\n", "EINVAL: Unknown arg ",arg);
            err = -1; goto fail;
        }
    }

    if( *mode == 0 ){
        fprintf(stderr,"EINVAL: One of --push or --pull required.\n");
        err = -1; goto fail;
    }

    if( *url==NULL ){
        fprintf(stderr,"EINVAL: Arg --url missing.\n");
        err = -1; goto fail;
    }
    uint_t urlFromArgs_len = strlen(*url);
    if( ((*url)[urlFromArgs_len-1]) != '/' ){
        char *urlFromArgs = *url;
        uint_t url_len = urlFromArgs_len + 1;
        *url = malloc(url_len+1); /* TODO: Should we free this? */
        memcpy(*url, urlFromArgs, urlFromArgs_len);
        (*url)[url_len-1] = '/';
        (*url)[url_len] = '\0';
    }else{
		/* TODO Mallocator */
		/* TODO free */
        *url = strdup(*url);
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

    if( *mode == MODE_PUSH && *filter ){
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
static int parseUrl(
	char const*url, int url_len,
	int*host_beg, int*host_len,
	uint_least16_t*port,
	int*path_beg
){
	int i = 0;
	for(;; ++i ){
		if( i >= url_len ){ assert(!"TODO_28UXyd6zEw3fnIib"); }
		if( i == 0 && url[i] == 'h' ) continue;
		if( i == 1 && url[i] == 't' ) continue;
		if( i == 2 && url[i] == 't' ) continue;
		if( i == 3 && url[i] == 'p' ) continue;
		if( i == 4 && url[i] == ':' ) continue;
		if( i == 5 && url[i] == '/' ) continue;
		if( i == 6 && url[i] == '/' ) continue;
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


static size_t onCurlDirRsp( char*buf, size_t size, size_t nmemb, void*ResourceDir_ ){
    int err;
    fprintf(stderr, "%s%s%s%p%s"FMT_SIZE_T"%s"FMT_SIZE_T"%s%p%s\n", "[TRACE] ", __func__, "( buf=", buf,
        ", size=", size, ", nmemb=", nmemb, ", cls=", ResourceDir_, " )");
    ResourceDir *resourceDir = ResourceDir_;
    //ClsDload *dload = resourceDir->dload;
    // OBSOLETE CURL *curl = dload->curl;
    const size_t buf_len = size * nmemb;

	assert(!"TODO_v98AK3klUbNxYqSj");
    //long rspCode;
    // OBSOLETE curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rspCode);
    //resourceDir->rspCode = rspCode;
    //if( rspCode != 200 ){
    //    return size * nmemb; }

    // Collect whole response body into one buf (as cJSON seems unable to parse
    // partially)
    if( resourceDir->rspBody_cap < resourceDir->rspBody_len + buf_len +1 ){
        /* Enlarge buf */
        resourceDir->rspBody_cap = resourceDir->rspBody_len + buf_len + 1024;
        void *tmp = realloc(resourceDir->rspBody, resourceDir->rspBody_cap);
        if( tmp == NULL ){
            err = size * nmemb/*TODO could we return anything better here?*/; goto endFn; }
        resourceDir->rspBody = tmp;
    }
    memcpy(resourceDir->rspBody+resourceDir->rspBody_len, buf, buf_len);
    resourceDir->rspBody_len += buf_len;
    resourceDir->rspBody[resourceDir->rspBody_len] = '\0';

    // Parsing occurs in the caller, as soon we processed whole response.

    err = size * nmemb;
endFn:
    return err;
}


static void onResourceFileHttpRspHdr(
	const char*proto, int proto_len,
	int rspCode,
	const char*phrase, int phrase_len,
	const struct Garbage_HttpMsg_Hdr*hdrs, int hdrs_cnt,
	struct Garbage_HttpClientReq**req, void*cls_
){
	//ResourceFile*const resourceFile = assert_is_ResourceFile(cls_);
	// TODO if( rspCode != 200 && rspCode != 404 )
	{
		LOGD("< %.*s %d %.*s\n", proto_len, proto, rspCode, phrase_len, phrase);
		for( int i = 0 ; i < hdrs_cnt ; ++i ){
			LOGD("< %.*s: %.*s\n", hdrs[i].key_len, hdrs[i].key, hdrs[i].val_len, hdrs[i].val);
		}
	}
}


/*
 * Flg 0x4 set means, that this is the last buffer. This is the last callback
 * called for this http message. */
static void onResourceFileHttpRspBody(
	const char*buf, int buf_len, int flg, struct Garbage_HttpClientReq**req, void*cls_
){
	struct Cls595AB944*const cls = cls_; assert(cls->mAGIC == 0x595AB944);
	ResourceFile*const resourceFile = assert_is_ResourceFile(cls->resourceFile);
	if( buf_len > 0 ){ /* data */
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


static void onResourceFileError( int retval, void*cls_ ){
	struct Cls595AB944*const cls = cls_; assert(cls->mAGIC == 0x595AB944);
	cls->eno = retval;
	if( cls->eno ){ LOGT("%s: %.*s\n\t@ %s:%d\n", strerrname(-cls->eno),
		cls->resourceFile->url_len, cls->resourceFile->url, __FILE__, __LINE__); }
	collectResourceIntoMemory(cls);
}


static void collectResourceIntoMemory( struct Cls595AB944*cls ){
	assert(cls->mAGIC == 0x595AB944);
	int err;

	#define CORO_STATE (cls->coroState)
	enum { begin=0, sgDMgDx6HnAdNWWis, };
	switch( CORO_STATE ){case begin:{
		assert(cls->onDone);
		ResourceFile*const resourceFile = assert_is_ResourceFile(cls->resourceFile);
		ResourceDir*const resourceDir = assert_is_ResourceDir(resourceFile->resourceDir);
		ClsDload*const dload = assert_is_ClsDload(resourceDir->dload);
		Resclone*const resclone = dload->resclone;
		/**/
		static struct Garbage_HttpClientReq_Mentor reqMentor = {
			.onRspHdr = onResourceFileHttpRspHdr,
			.onRspBody = onResourceFileHttpRspBody,
			.onError = onResourceFileError,
		};
		int host_beg, host_len, path_beg;
		uint_least16_t port;
		err = parseUrl(resourceFile->url, resourceFile->url_len,
			&host_beg, &host_len, &port, &path_beg);
		if( err ){ assert(!"TODO_dNkENg9EFSLccljy"); }
		struct Garbage_HttpClientReq **req;
		char host[32]; err = snprintf(host, sizeof host,
			"%.*s", host_len, resourceFile->url+host_beg);
		assert(err < (int)sizeof host);
		char *url = resourceFile->url + path_beg;
		req = newHttpsClientReq(&resclone->deps, "GET", host, port, url, NULL, 0, &reqMentor, cls);
		if( !req ){ assert(!"TODO_pMYskoj4hmYk1DET"); }
		FN_HttpClientReq_closeSnk(req);
		CORO_STATE = sgDMgDx6HnAdNWWis;
		FN_HttpClientReq_resume(req);
		return;
	}case sgDMgDx6HnAdNWWis:{
		if( cls->eno ){ LOGT("\t@ %s:%d\n", __FILE__, __LINE__); }
	}/*endWithClsEno*/{
		void(*onDone)(int,void*) = cls->onDone;  cls->onDone = NULL;
		assert(onDone);
		onDone(cls->eno, cls->onDoneArg);
		return;
	}}
	LOGD("assert(s != %d) @ %s:%d\n", CORO_STATE, __FILE__, __LINE__); abort();
	#undef CORO_STATE
}


static void onTarOutChunk(
	void*cls_, const char*buf, int buf_len, int flgs,
	void(*onDone)(int,void*), void*onDoneArg
){
	int err;
	ResourceFile*const resourceFile = assert_is_ResourceFile(cls_);
	ClsDload*const dload = assert_is_ClsDload(resourceFile->resourceDir->dload);
	if( !dload->tarFile ){
		if( !dload->archiveFile || !strncmp(dload->archiveFile, "-", 2) ){
			dload->tarFile = stdout;
		}else{
			dload->tarFile = fopen(dload->archiveFile, "w");
			if( !dload->tarFile ){
				err = -errno;
				LOGE("%s: \"%s\"\n\t@ %s:%d\n", strerrname(-err), dload->archiveFile,
					__FILE__, __LINE__);
				dload->resclone->exitCode = 1;
				return;
			}
		}
	}
	err = fwrite(buf, 1, buf_len, dload->tarFile);
	if( err != buf_len ){ err = -errno;
		LOGE("%s\n\t@ %s:%d\n", strerrname(-err), __FILE__, __LINE__);
		dload->resclone->exitCode = 1;
		return;
	}
	onDone(buf_len, onDoneArg);
}


static void copyBufToArchive_kontinue( int err, void*cls_ ){
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
		char *fileName = resourceFile->url + strlen(dload->rootUrl);
		int const fileName_len = strlen(fileName);
		struct Garbage_TarEncHdr tarHdr = {
			.path = fileName,
			.path_len = fileName_len,
			.mode = 0644,
			.nBodyOctets = resourceFile->buf_len,
		};
		CORO_STATE = sE2iEFO6D7uNJ2A85;
		(*dload->tar)->nextEntry(dload->tar, &tarHdr, copyBufToArchive_kontinue, cls);
		return;
	}case sE2iEFO6D7uNJ2A85:{
		if( err < 0 ) LOGW("%s: TODO_cp6WZH15OGqJtPwt\n\t@ %s:%d\n",
			strerrname(-err), __FILE__, __LINE__);
		CORO_STATE = sX7Vo8fJSSxJAJtHp;
		(*dload->tar)->write(dload->tar, resourceFile->buf, resourceFile->buf_len,
			copyBufToArchive_kontinue, cls);
		return;
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

		//ssize_t written = archive_write_data(dload->dstArchive, resourceFile->buf, resourceFile->buf_len);
		//if( written < 0 ){
		//    fprintf(stderr, "%s%s\n", "[ERROR] Failed to archive_write_data: ",
		//        archive_error_string(dload->dstArchive));
		//    err = -1; goto endFn;
		//}else if( written != resourceFile->buf_len ){
		//    fprintf(stderr, "%s%u%s"FMT_SIZE_T"\n", "[ERROR] archive_write_data failed to write all ",
		//        resourceFile->buf_len, " bytes. Instead it wrote ", written);
		//    err = -1; goto endFn;
		//}

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


static void copyBufToArchive(
	ResourceFile*resourceFile,
	void(*onDone)(int err,void*arg),
	void*onDoneArg
){
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
static int pathFilterAcceptsEntry(
	ClsDload*dload, ResourceDir*resourceDir, char const*nameOrig, int name_len
){
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
            if( dload->resclone->isFilterFull ){
                LOGD("[DEBUG] Path longer than --filter-full -> reject.\n");
                err = 0; goto endFn;
            }else{
                LOGD("[DEBUG] Path longer than --filter-part -> accept.\n");
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
            LOGD("[DEBUG] Segment accepted by filter.\n");
            err = 1; /* fall to restoreEndSlash */
        }else if( err == REG_NOMATCH ){
            LOGD("[DEBUG] Segment rejected by filter.\n");
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


static void onDloadPushIoTask( void(*task)(void*arg), void*arg, void*cls_ ){
	LOGT("[TRACE] %s()\n", __func__);
	ClsDload*const dload = assert_is_ClsDload(cls_);
	FN_ThreadPool_enque(dload->resclone->deps.ioWorker, task, arg);
}


static void onDloadError( int retval, void*cls_ ){
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	LOGW("%s:\n\t@ %s:%d\n", strerrname(-retval), __FILE__, __LINE__);
	resourceDir->eno = retval;
	assert(*resourceDir->dload->req);
	assert((*resourceDir->dload->req)->pause);
	gateleenResclone_download_kontinue(resourceDir);
}


static void onResourceDirHttpRspHdr(
	const char*proto, int proto_len,
	int rspCode,
	const char*phrase, int phrase_len,
	const struct Garbage_HttpMsg_Hdr*hdrs, int hdrs_cnt,
	struct Garbage_HttpClientReq**req,
	void*cls_
){
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


static void iterateNextResourceFile( int err, void*cls_ ){
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
			resourceFile->eno = err; goto endWithEno;
		}else if( err == 0 ){ /* REJECT */
			LOGI("[INFO ] Skip     '%s%.*s'  (filtered)\n", dload->url, name_len , name);
			resourceFile->eno = -10000-__LINE__;
			LOGD("TODO: choose better errno(%d)\n\t@ %s:%d \n",
				resourceFile->eno, __FILE__, __LINE__);
			goto endWithEno;
		}
		assert(name[name_len-1] != '/' && "Why the F** did you call this function then?!?");
		int requiredLen = dload->url_len + 1/*slash*/ + name_len;
		if( requiredLen >= resourceFile->url_cap ){
			void *tmp = Mallocator_realloc(dload->resclone->deps.mallocator,
				resourceFile->url, resourceFile->url_cap, requiredLen +1);
			if( !tmp ){ resourceDir->eno = -ENOMEM; goto endWithEno; }
			resourceFile->url = tmp;
			resourceFile->url_cap = requiredLen;
		}
		err = sprintf(resourceFile->url, "%s%.*s", dload->url, name_len, name);
		LOGI("[INFO ] Download '%s'\n", resourceFile->url);
		resourceFile->url_len = err;  assert(err <= resourceFile->url_cap);
		resourceFile->buf_len = 0; // <- Reset before use.
		struct Cls595AB944*clsSub = &resourceFile->cls595AB944;
		assert(clsSub->mAGIC == 0 && "Bad luck. Closure already busy.");
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
		if( err ){ resourceFile->eno = err; goto endWithEno; }
		CORO_STATE = spz2UiLEf04xmhO14;
		copyBufToArchive(resourceFile, iterateNextResourceFile, resourceFile);
		return;
	}case spz2UiLEf04xmhO14:{
		resourceFile->eno = err;
	}endWithEno:{
		void(*onDone)(int,void*) = resourceFile->onDone; resourceFile->onDone = NULL;
		assert(onDone);
		onDone(resourceFile->eno, resourceFile->onDoneArg);
	}}
	#undef CORO_STATE
}


static void fmkKBgMWbr6Scr748( int err, void*cls_ ){
	if( err < 0 ){ assert(!"TODO_NsR9vBN74QJzVZX5"); }
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	FN_HttpClientReq_resume(resourceDir->dload->req);
}


static void onRspJsonParsed( void*cls_, int err, void*json_ ){
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	if( err ){
		resourceDir->httpRspCode = ERR_PARSE_DIR_LIST;
		LOGW("%s: Failed to parse response JSON\n", strerrname(-err));
		goto endFn;
	}
	struct Garbage_JsonTreeParser_JsonNode const*const rootNode = json_;
	struct Garbage_JsonTreeParser_JsonNode const*json = rootNode;
	assert(json->type == '{');
	if( json->childs_len != 1 ){
		LOGW("[ERROR] JSON root expected ONE child but got %d\n", json->childs_len);
		resourceDir->httpRspCode = ERR_PARSE_DIR_LIST;
		goto endFn;
	}
	if( json->childs[0]->type != '[' ){
		LOGE("[ERROR] json['%.*s'] expected to be an array. But is not.\n",
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
		LOGD("%s @ %s:%d", strerror(-err), __FILE__, __LINE__);
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
		LOGD("Child: \"%.*s\"\n", childName_len, childName);
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


static void onDloadRspBody(
	const char*buf, int buf_len, int flgs,
	struct Garbage_HttpClientReq**req,
	void*cls_
){
	//LOGT("[TRACE] %s()\n", __func__);
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	if( resourceDir->httpRspCode != 200 ){ return; }
	if( buf_len < 0 ){ /* error */
		LOGD("TODO: %d  %s:%d\n", buf_len, __FILE__, __LINE__); abort();
		return;
	}
	if( !resourceDir->jsonParser ){
		resourceDir->jsonParser = newJsonTreeParser(
			&resourceDir->dload->resclone->deps, onRspJsonParsed, resourceDir);
		assert(resourceDir->jsonParser && "TODO_GtLfTLwu2EjSFiyW");
	}
	if( buf_len > 0 || flgs & 4 ){
		FN_HttpClientReq_pause(resourceDir->dload->req);
		FN_JsonTreeParser_write(resourceDir->jsonParser,
			(void*)buf, buf_len, flgs & 4, fmkKBgMWbr6Scr748, resourceDir);
		return;
	}
	assert(!"unreachable");
}


static void gateleenResclone_download_kontinueIV( int i, void*v ){
	ResourceDir*const resourceDir = assert_is_ResourceDir(v);
	resourceDir->eno = i;
	gateleenResclone_download_kontinue(resourceDir);
}
static void gateleenResclone_download_kontinue( void*cls_ ){
	int err;
	ResourceDir*const resourceDir = assert_is_ResourceDir(cls_);
	/* TODO is dload maybe the wrong context for some cases in here? */
	ClsDload*const dload = assert_is_ClsDload(resourceDir->dload);
	#define CORO_STATE (resourceDir->state_gateleenResclone_download)
	#define CORO_GOTO(S) do{goto S;}while(0)
	enum { begin=0, sywgOcZGKrgTP3onF, onChildDone, };
	switch( CORO_STATE ){case begin:{

		if( !resourceDir->name ){
			/* Is the case when its the root call and not a recursive one */
			resourceDir->name = dload->rootUrl;
		}

		/* setup URL */
		{
			dload->url_len = 0;
			for( ResourceDir*d=resourceDir ; d ; d=d->parentDir ){
				assert(d->name);
				int len = strlen(d->name) - strspn(d->name, "/");
				dload->url_len += len;
			}
			dload->url = Mallocator_realloc(dload->resclone->deps.mallocator, NULL, 0,
				dload->url_len +1 /*MayPreventReallocLaterForName*/+24);
			if( !dload->url ){ assert(!"TODO_5pt10Xwq0C4wUdr0"); }
			char *u = dload->url + dload->url_len;
			for( ResourceDir*d=resourceDir ; d ; d=d->parentDir ){
				char *name = d->name + strspn(d->name, "/");
				int name_len = strlen(name);
				memcpy(u-name_len, name, name_len); u -= name_len;
			}
			dload->url[dload->url_len] = '\0';
			LOGD("[DEBUG] URL '%s'\n", dload->url);
		}

		static struct Garbage_HttpClientReq_Mentor requestMentor = {
			.pushIoTask = onDloadPushIoTask,
			.onError = onDloadError,
			.onRspHdr = onResourceDirHttpRspHdr,
			.onRspBody = onDloadRspBody,
		};
		int host_beg, host_len, path_beg;
		uint_least16_t port;
		err = parseUrl(dload->url, strlen/*TODO*/(dload->url),
			&host_beg, &host_len, &port, &path_beg);
		char host[64]; err = snprintf(host, sizeof host, "%.*s", host_len, dload->url + host_beg);
		assert(err < (int)sizeof host && "ENOTSUP: hostname too long");
		char const *path = dload->url + path_beg;
		dload->req = newHttpsClientReq(&dload->resclone->deps,
			"GET", host, port, path, NULL, 0, &requestMentor, resourceDir);
		if( !dload->req ){ assert(!"TODO_9A7x4x7VPEDHXEkX"); }
		FN_HttpClientReq_closeSnk(dload->req);
		CORO_STATE = sywgOcZGKrgTP3onF;
		FN_HttpClientReq_resume(dload->req);
		return;
	}case sywgOcZGKrgTP3onF:{
		if( resourceDir->httpRspCode == ERR_PARSE_DIR_LIST ){
			LOGT("\t@ %s:%d\n", __FILE__, __LINE__);
			resourceDir->eno = 0; CORO_GOTO(endWithEno);
		}

		if( resourceDir->httpRspCode != 200 ){
			// Ugh? Just one request earlier, server said there's a directory on
			// that URL. Nevermind. Just skip it and at least download the other
			// stuff.
			LOGI("[INFO ] Skip HTTP %d -> '%s'\n", resourceDir->httpRspCode, dload->url);
			resourceDir->eno = 0; CORO_GOTO(endWithEno);
		}

	}nextChild:{
		assert(resourceDir->childNames);
		if( resourceDir->currChildName >= resourceDir->childNames_len ){
			/* no more childs */
			resourceDir->eno = 0;  goto endWithEno;
		}
		char *name = resourceDir->childNames[resourceDir->currChildName];
		assert(name);
		int const name_len = strlen(name);
		/* Look if Gateleen reports a 'directory'. So we have to "go recursive"
		 * now. Have fun reading asynchronous code, operating recursively :D */
		if( name[name_len-1] == '/' ){
			LOGD("[DEBUG] Scan     '%s%.*s'\n", dload->url, name_len, name);
			assert(name[name_len] == '\0');
			CORO_STATE = onChildDone;
			gateleenResclone_download(dload, resourceDir, name,
				gateleenResclone_download_kontinueIV, resourceDir);
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
			.onDone = gateleenResclone_download_kontinueIV,
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
		void(*onDone)(int,void*) = resourceDir->onDone ; resourceDir->onDone = NULL;
		onDone(resourceDir->eno, resourceDir->onDoneArg);
		return;
	}}
	LOGD("assert(s != %d)  %s:%d\n", CORO_STATE, __FILE__, __LINE__); abort();
	#undef CORO_GOTO
	#undef CORO_STATE
}


/** Gets called for every resource to scan/download.
 * HINT: Gets called recursively. */
static void gateleenResclone_download(
	ClsDload*dload,
	ResourceDir*parentResourceDir,
	char*entryName,
	void(*onDone)(int,void*),
	void*onDoneArg
){
	assert_is_ClsDload(dload);
	assert(onDone);
	//LOGT("[TRACE] %s()\n", __func__);

	/* closure */
	/* TODO free ------vvvvvvvvvvv */
	ResourceDir *const resourceDir = Mallocator_realloc(
		dload->resclone->deps.mallocator, NULL, 0, sizeof*resourceDir);
	if( !resourceDir ){ assert(!"TODO_Hahq0XcbwgpB2tSd"); }
	*resourceDir = (ResourceDir){
		.mAGIC = ResourceDir_mAGIC,
		.dload = dload,
		.parentDir = parentResourceDir,
		/* TODO Mallocator */
		/* TODO free */
		.name = (entryName) ? strdup(entryName) : NULL,
		.onDone = onDone,
		.onDoneArg = onDoneArg,
	};

	gateleenResclone_download_kontinue(resourceDir);
}


static size_t onUploadChunkRequested( char*buf, size_t size, size_t count, void*Put_ ){
    int err;
    //Put *put = Put_;
    //Upload *upload = put->upload;
    //const size_t buf_len = size * count;

    assert(!"TODO_KCcAACViAABlNQAA");
    ssize_t readLen;
    //readLen = archive_read_data(upload->srcArchive, buf, buf_len);
    //fprintf(stderr, "%s%lu%s\n", "[DEBUG] Cpy ", readLen, " bytes.");
    if( readLen < 0 ){
        //fprintf(stderr, "%s"FMT_SIZE_T"%s%s\n", "[ERROR] Failed to read from archive (code ",
        //    readLen, "): ", archive_error_string(upload->srcArchive));
        err = -1; goto endFn;
    }else if( readLen > 0 ){
        // Regular read. Data already written to 'buf'. Only need to adjust
        // return val.
        err = readLen; goto endFn;
    }else{ // EOF
        assert(readLen == 0);
        err = 0; goto endFn;
    }

    assert(!"Unreachable code");
endFn:
    //fprintf(stderr, "%s%s%s%ld\n", "[DEBUG] ", __func__, "() -> ", err);
    return err >= 0 ? err : /*CURL_READFUNC_ABORT TODO*/-42;
}


static ssize_t addContentTypeHeader( Put*put/*TODO, struct curl_slist *reqHdrs */ ){
    ssize_t err;
    char *contentTypeHdr = NULL;
    //Upload *upload = put->upload;
    const char *name = put->name;

    uint_t name_len = strlen(put->name);
    // Find file extension.
    const char *ext = name + name_len;
    for(; ext>name && *ext!='.' && *ext!='/' ; --ext );
    // Convert it to mime type.
    const char *mimeType;
    if( *ext == '.' ){
        mimeType = fileExtToMime(ext +1); // <- +1, to skip the (useless) dot.
        if( mimeType ){
            fprintf(stderr, "%s%s%s%s%s\n", "[DEBUG] Resolved file ext '", ext+1,"' to mime '", mimeType?mimeType:"<null>", "'.");
        }
    }else if( *ext=='/' || ext==name || *ext=='\0' ){ // TODO Explain why 0x00.
        mimeType = "application/json";
        fprintf(stderr, "%s\n", "[DEBUG] No file extension. Fallback to json (gateleen default)");
    }else{
        mimeType = NULL;
    }
    if( mimeType == NULL ){
        fprintf(stderr, "%s%s%s\n", "[DEBUG] Unknown file extension '", ext+1, "'. Will NOT add Content-Type header.");
        mimeType = ""; // <- Need to 'remove' header. To do this, pass an empty value to curl.
    }
    uint_t mimeType_len = strlen(mimeType);
    static const char contentTypePrefix[] = "Content-Type: ";
    static const uint_t contentTypePrefix_len = sizeof(contentTypePrefix)-1;
    contentTypeHdr = malloc( contentTypePrefix_len + mimeType_len +1 );
    memcpy(contentTypeHdr , contentTypePrefix , contentTypePrefix_len);
    memcpy(contentTypeHdr+contentTypePrefix_len , mimeType , mimeType_len+1);
    assert(!"TODO_6UcAAGEwAACHKAAA");
    //reqHdrs = curl_slist_append(reqHdrs, contentTypeHdr);
    //err = curl_easy_setopt(upload->curl, CURLOPT_HTTPHEADER, reqHdrs);
    if( err ){
        fprintf(stderr, "%s"FMT_SIZE_T"\n", "[ERROR] curl_easy_setopt(_, HTTPHEADER, _): ", err);
        assert(!err); err = -1; goto endFn; }

    err = 0;
endFn:
    free(contentTypeHdr);
    return err;
}


static ssize_t httpPutEntry( Put*put ){
    ssize_t err;
    Upload *upload = put->upload;
    char *url = NULL;
    // TODO struct curl_slist *reqHdrs = NULL;

    int rootUrl_len = strlen(upload->rootUrl);
    if( upload->rootUrl[rootUrl_len-1]=='/' ){
        rootUrl_len -= 1;
    }
    int url_len = strlen(upload->rootUrl) + strlen(put->name);
    url = malloc(url_len +2);
    if( url == NULL ){
        err = -ENOMEM; goto endFn; }
    sprintf(url, "%.*s/%s", rootUrl_len,upload->rootUrl, put->name);
    assert(!"TODO_chIAAHobAABdMwAA");
    //err =  CURLE_OK != curl_easy_setopt(upload->curl, CURLOPT_URL, url)
    //    || addContentTypeHeader(put, reqHdrs)
    //    ;
    if( err ){
        assert(!err); err = -1; goto endFn; }

    fprintf(stderr, "%s%s%s\n", "[INFO ] Upload '", url, "'");
    assert(!"TODO_ZWQAABNAACIZAAAR");
    //err = curl_easy_perform(upload->curl);
    //if( err != CURLE_OK ){
    //    fprintf(stderr, "%s%s%s"FMT_SIZE_T"%s%s\n",
    //        "[ERROR] PUT '", url, "' (code ", err, "): ", curl_easy_strerror(err));
    //    err = -1; goto endFn;
    //}
    long rspCode;
    //curl_easy_getinfo(upload->curl, CURLINFO_RESPONSE_CODE, &rspCode);
    if( rspCode <= 199 || rspCode >= 300 ){
        fprintf(stderr, "%s%ld%s%s%s\n",
            "[WARN ] Got RspCode ", rspCode, " for 'PUT ", url, "'");
    }else{
        //fprintf(stderr, "%s%ld%s%s%s\n", "[DEBUG] Got RspCode ", rspCode, " for 'PUT ", url, "'");
    }

    err = 0;
endFn:
    // TODO curl_slist_free_all(reqHdrs);
    free(url);
    return err;
}


static ssize_t readArchive( Upload*upload ){
    ssize_t err;
    //Put *put = NULL;

    //upload->srcArchive = archive_read_new();
    if( ! upload->srcArchive ){
        assert(upload->srcArchive); err = -1; goto endFn; }

    //const int blockSize = (1<<14);
    assert(!"TODO_1G0AAIdeAADxFQAA");
    //err = archive_read_support_format_all(upload->srcArchive)
    //   || archive_read_open_filename(upload->srcArchive, upload->archiveFile, blockSize)
    //   ;
    if( err ){
        //fprintf(stderr, "%s"FMT_SIZE_T"%s%s\n", "[ERROR] Failed to open src archive (code ", err, "): ",
        //    curl_easy_strerror(err));
        err = -1; goto endFn;
    }

    //err = curl_easy_setopt(upload->curl, CURLOPT_UPLOAD, 1L)
    //   || curl_easy_setopt(upload->curl, CURLOPT_READFUNCTION, onUploadChunkRequested)
    //    ;
    if( err ){
        assert(!err); err = -1; goto endFn; }
    assert(!"TODO_DlkAAL4rAACHIAAA");
    //for( struct archive_entry*entry ; archive_read_next_header(upload->srcArchive,&entry) == ARCHIVE_OK ;){
    //    const char *name = archive_entry_pathname(entry);
    //    int ftype = archive_entry_filetype(entry);
    //    if( ftype == AE_IFDIR ){
    //        continue; // Ignore dirs because gateleen doesn't know 'dirs' as such.
    //    }
    //    if( ftype != AE_IFREG ){
    //        fprintf(stderr, "%s%s%s\n", "[WARN ] Ignore non-regular file '", name, "'");
    //        continue;
    //    }
    //    //fprintf(stderr, "%s%s%s\n", "[DEBUG] Reading '",name,"'");
    //    Put _1 = {
    //        .upload = upload,
    //        .name = (char*)name
    //    }; put = &_1;
    //    err = curl_easy_setopt(upload->curl, CURLOPT_READDATA, put)
    //        || httpPutEntry(put);
    //    //curl = upload->curl; // Sync back. TODO: Still needed?
    //    if( err ){
    //        assert(!err); err = -1; goto endFn; }
    //}

    err = 0;
endFn:
    return err;
}


static void pull_onDownloadDone( int eno, void*cls_ ){
	ClsDload*const dload = assert_is_ClsDload(cls_);
	if( eno ){
		LOGT("\t@ %s:%d\n", __FILE__, __LINE__);
		return;
	}
	pull(dload->resclone);
}


static void pull( void*cls_ ){
	Resclone*const resclone = assert_is_Resclone(cls_);
	int err;
	ClsDload *dload = NULL;
	#define CORO_STATE (resclone->state_pull)
	#define CORO_GOTO(S) goto S
	enum { begin=0, sArUiyl2d1oYejma4, };
	switch( CORO_STATE ){case begin:{
		if( resclone->file == NULL && isatty(1) ){
			fprintf(stderr, "%s\n",
				"[ERROR] Are you sure you wanna write binary content to tty?");
			resclone->eno = -1; CORO_GOTO(endWithEno);
		}

		dload = Mallocator_realloc(resclone->deps.mallocator, NULL, 0, sizeof*dload);
		if( !dload ){
			err = -errno;
			LOGD("%s: Mallocator_realloc()\n\t@ %s:%d\n", strerrname(-err), __FILE__, __LINE__);
			resclone->eno = err; CORO_GOTO(endWithEno);
		}
		*dload = (ClsDload){
			.mAGIC = ClsDload_mAGIC,
			.resclone = resclone,
			.rootUrl = resclone->url,
			.archiveFile = resclone->file,
		}; assert_is_ClsDload(dload);
		CORO_STATE = sArUiyl2d1oYejma4;
		gateleenResclone_download(dload, NULL, NULL, pull_onDownloadDone, dload);
		return;
	}case sArUiyl2d1oYejma4:{
		LOGW("[TODO ] close tar archive\n");

		// assert(!"TODO_snwAAHEGAAC1VgAA");
		// if( dload->dstArchive && archive_write_close(dload->dstArchive) ){
		//     fprintf(stderr, "%s"FMT_SIZE_T"%s%s\n", "[ERROR] archive_write_close failed (code ",
		//         err, "): ", archive_error_string(dload->dstArchive));
		//     err = -1; goto endFn;
		// }
		//
		// err = 0;

	}endWithEno:{
		if( dload ){
			LOGW("[WARN ] TODO_Kh0AAJJLAACsXwAA fix resource-leak here\n");
		//archive_entry_free(dload->tmpEntry); dload->tmpEntry = NULL;
		//archive_write_free(dload->dstArchive); dload->dstArchive = NULL;
		}
		LOGI("[INFO ] Pull Done with %d\n", resclone->eno);
		/* TODO cleanup */
	}}
	#undef CORO_STATE
	#undef CORO_GOTO
}


static void push( void*cls_ ){
    Resclone*const resclone = assert_is_Resclone(cls_);
    int err;
    Upload *upload = NULL;

    Upload _1={0}; upload =&_1;
    upload->resclone = resclone;
    upload->archiveFile = resclone->file;
    upload->rootUrl = resclone->url;
    assert(!"TODO_6mwAAO5BAACYTAAA");
    //upload->curl = curl_easy_init();
    //if( ! upload->curl ){
    //    fprintf(stderr, "%s\n", "[ERROR] curl_easy_init() -> NULL");
    //    err = -1; goto endFn;
    //}

    err = readArchive(upload);
    if( err ){
        err = -1; goto endFn; }

    err = 0;
endFn:
    if( upload ){
        assert(!"TODO_B2AAAIBdAADcJAAA");
        //curl_easy_cleanup(upload->curl);
        //archive_read_free(upload->srcArchive);
    }
    if( err ){ LOGW("[WARN ] retval ignored: %d @%s:%d\n", err, __FILE__, __LINE__); }
}


int gateleenResclone_run( int argc, char**argv ){
    int err;
    Resclone *resclone = &(Resclone){
        .mAGIC = Resclone_mAGIC,
    };

    err = parseArgs(argc, argv, &resclone->mode, &resclone->url, &resclone->filter,
        &resclone->filter_len, &resclone->isFilterFull, &resclone->file);
    if( err ){
        err = -1; goto endFn; }

    if( initEnv(&resclone->deps, resclone->envMem, sizeof resclone->envMem) ){
		LOGD("\t@ %s:%d\n", __FILE__, __LINE__); goto endFn;
	}

    if( resclone->mode == MODE_FETCH ){
        FN_Env_enque(resclone->deps.env, pull, resclone);
    }else if( resclone->mode == MODE_PUSH ){
        FN_Env_enque(resclone->deps.env, push, resclone);
    }else{
        err = -1; goto endFn;
    }

    FN_Env_runUntilDone(resclone->deps.env);
	err = resclone->exitCode;
    goto endFn;

    assert(!"Unreachable");
endFn:
    parseArgs(-1, argv, &resclone->mode, &resclone->url, &resclone->filter, &resclone->filter_len,
        &resclone->isFilterFull, &resclone->file);
    resclone->mode = MODE_NULL; resclone->url = NULL; resclone->file = NULL;
    return err;
}


int gateleenResclone_main( int argc, char**argv ){
    int ret;
    ret = gateleenResclone_run(argc, argv);
    if( ret < 0 ){ ret = 0 - ret; }
    return (ret > 127) ? 1 : ret;
}


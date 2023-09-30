/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

/* This Unit */
#include "gateleen_resclone.h"

/* System */
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <regex.h>
#include <string.h>

/* Libs */
#include "archive.h"
#include "archive_entry.h"
#include <curl/curl.h>
#include <cJSON.h>

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



/** Operation mode. */
typedef enum OpMode {
    MODE_NULL =0,
    MODE_FETCH=1,
    MODE_PUSH =2
} OpMode;


/** Main module handle representing an instance. */
typedef struct Resclone {
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
} Resclone;


/** Closure for a download instructed by external caller. */
typedef struct ClsDload {
    struct Resclone *resclone;
    char *rootUrl;
    struct archive *dstArchive;
    struct archive_entry *tmpEntry;
    char *archiveFile;
    CURL *curl;
} ClsDload;


/** Closure for a collection (directory) download. */
typedef struct ResourceDir {
    struct ClsDload *dload;
    //yajl_handle yajl; <- TODO Obsolete
    struct ResourceDir *parentDir;
    char *rspBody;
    size_t rspBody_len;
    size_t rspBody_cap;
    short rspCode;
    char *name;
} ResourceDir;


/** Closure for a file download. */
typedef struct ResourceFile {
    struct ClsDload *dload;
    size_t srcChunkIdx;
    char *url;
    char *buf;
    int buf_len;
    int buf_memSz;
} ResourceFile;


/** Closure for an upload instructed by external caller. */
typedef struct Upload {
    struct Resclone *resclone;
    char *rootUrl;
    char *archiveFile;
    struct archive *srcArchive;
    CURL *curl;
} Upload;


/** Closure for a PUT of a single resource. */
typedef struct Put {
    struct Upload *upload;
    /* Path (relative to rootUrl) of the resource to be uploaded. */
    char *name;
} Put;



/* Operations ****************************************************************/


TPL_ARRAY(str, char*, 16);


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
        "        stdin/stdout if ommitted.\n"
        "  \n"
        "  \n"
    );
}


static int parseArgs( int argc, char**argv, OpMode*mode, char**url, regex_t**filter, size_t*filter_cnt, int*isFilterFull, char**file ){
    ssize_t err;
    char *filterRaw = NULL;
    if( argc == -1 ){ // -1 indicates the call to free our resources. So simply jump
        goto fail;    // to 'fail' because that has the same effect.
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
                printf("%s\n","ERROR: Mode already specified. Won't set '--pull'.");
                err = -1; goto fail;
            }
            *mode = MODE_FETCH;
        }else if( !strcmp(arg,"--push") ){
            if( *mode ){
                printf("%s\n","ERROR: Mode already specified. Won't set '--push'.");
                err = -1; goto fail;
            }
            *mode = MODE_PUSH;
        }else if( !strcmp(arg,"--url") ){
            if(!( arg=argv[++i]) ){
                printf("%s\n","ERROR: Arg '--url' needs a value.");
                err = -1; goto fail;
            }
            *url = arg;
        }else if( !strcmp(arg,"--filter-full") ){
            if(!( arg=argv[++i] )){
                printf("%s\n","ERROR: Arg '--filter-full' needs a value.");
                err = -1; goto fail; }
            if( filterRaw ){
                printf("%s\n","ERROR: Cannot use '--filter-full' because a filter is already set.");
                err=-1; goto fail; }
            filterRaw = arg;
            *isFilterFull = !0;
        }else if( !strcmp(arg,"--filter-part") ){
            if(!( arg=argv[++i] )){
                printf("%s\n","ERROR: Arg '--filter-part' needs a value.");
                err = -1; goto fail; }
            if( filterRaw ){
                printf("%s\n","ERROR: Cannot use '--filter-part' because a filter is already set.");
                err = -1; goto fail; }
            filterRaw = arg;
            *isFilterFull = 0;
        }else if( !strcmp(arg,"--file") ){
            if(!( arg=argv[++i]) ){
                printf("%s\n","ERROR: Arg '--file' needs a value.");
                err = -1; goto fail;
            }
            *file = arg;
        }else{
            printf("%s%s\n", "ERROR: Unknown arg ",arg);
            err = -1; goto fail;
        }
    }

    if( *mode == 0 ){
        printf("ERROR: One of --push or --pull required.\n");
        err = -1; goto fail;
    }

    if( *url==NULL ){
        printf("ERROR: Arg --url missing.\n");
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
                //fprintf(stderr, "%s%u%s%p\n",
                //    "[DEBUG] realloc(NULL, ", filter_cap*sizeof**filter," ) -> ", tmp);
                if( tmp == NULL ){
                    fprintf(stderr, "%s"FMT_SIZE_T"%s\n", "[ERROR] realloc(", filter_cap*sizeof**filter, ")");
                    err = -ENOMEM; goto fail; }
                *filter = tmp;
            }
            //fprintf(stderr, "%s%d%s%s%s\n", "[DEBUG] filter[", iSegm, "] -> '", beg-1, "'");
            err = regcomp((*filter)+iSegm, beg-1, REG_EXTENDED);
            if( err ){
                fprintf(stderr, "%s%s%s"FMT_SIZE_T"\n", "[ERROR] regcomp(", beg, ") -> ", err);
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
        fprintf(stderr, "%s\n", "[ERROR] Filtering not supported for push mode.");
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


static size_t onCurlDirRsp( char*buf, size_t size, size_t nmemb, void*ResourceDir_ ){
    int err;
    fprintf(stderr, "%s%s%s%p%s"FMT_SIZE_T"%s"FMT_SIZE_T"%s%p%s\n", "[TRACE] ", __func__, "( buf=", buf,
        ", size=", size, ", nmemb=", nmemb, ", cls=", ResourceDir_, " )");
    ResourceDir *resourceDir = ResourceDir_;
    ClsDload *dload = resourceDir->dload;
    CURL *curl = dload->curl;
    const size_t buf_len = size * nmemb;

    long rspCode;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rspCode);
    resourceDir->rspCode = rspCode;
    if( rspCode != 200 ){
        return size * nmemb; }

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


static size_t onResourceChunk( char*buf, size_t size, size_t nmemb, void*ResourceFile_ ){
    const int buf_len = size * nmemb;
    ResourceFile *resourceFile = ResourceFile_;

    const int avail = resourceFile->buf_memSz - resourceFile->buf_len;
    if( avail <= buf_len ){
        resourceFile->buf_memSz += buf_len - avail +1;
        void *tmp = realloc(resourceFile->buf , resourceFile->buf_memSz);
        assert(tmp != NULL); /* TODO error handling */
        resourceFile->buf = tmp;
    }
    char *it = resourceFile->buf + resourceFile->buf_len;
    memcpy(it, buf, buf_len); it += buf_len;
    *it = '\0';
    resourceFile->buf_len = it - resourceFile->buf;

    return buf_len;
}


static ssize_t collectResourceIntoMemory( ResourceFile*resourceFile, char*url ){
    ssize_t err;
    ClsDload *dload = resourceFile->dload;
    CURL *curl = dload->curl;

    err =  CURLE_OK!= curl_easy_setopt(curl, CURLOPT_URL, url)
        || CURLE_OK!= curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L )
        || CURLE_OK!= curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onResourceChunk)
        || CURLE_OK!= curl_easy_setopt(curl, CURLOPT_WRITEDATA, resourceFile)
        ;
    if( err ){ assert(!err); err = -1; goto endFn; }

    err = curl_easy_perform(curl);
    if( err != CURLE_OK ){
        fprintf(stderr, "%s%s%s%s%s"FMT_SIZE_T"%s%s\n", "[ERROR] ", __func__, "(): '",
            url, "' (code ", err, "): ", curl_easy_strerror(err));
        err = -1; goto endFn;
    }

    err = 0;
endFn:
    return err;
}


static ssize_t copyBufToArchive( ResourceFile*resourceFile ){
    ssize_t err;
    ClsDload *dload = resourceFile->dload;
    char *fileName = resourceFile->url + strlen(resourceFile->dload->rootUrl);

    if( ! dload->dstArchive ){
        /* Setup archive if not setup yet. */
        dload->dstArchive = archive_write_new();
        err =  archive_write_set_format_pax_restricted(dload->dstArchive)
            || archive_write_open_filename(dload->dstArchive, dload->archiveFile)
            ;
        if( err ){
            fprintf(stderr, "%s%s\n", "[ERROR] Failed to setup tar output: ",
                archive_error_string(dload->dstArchive));
            err = -1; goto endFn;
        }
    }

    if( dload->tmpEntry == NULL ){
        dload->tmpEntry = archive_entry_new();
    }else{
        dload->tmpEntry = archive_entry_clear(dload->tmpEntry);
    }
    archive_entry_set_pathname(dload->tmpEntry, fileName);
    archive_entry_set_filetype(dload->tmpEntry, AE_IFREG);
    archive_entry_set_size(dload->tmpEntry, resourceFile->buf_len);
    archive_entry_set_perm(dload->tmpEntry, 0644);
    err = archive_write_header(dload->dstArchive, dload->tmpEntry);
    if( err ){ err = -1; goto endFn; }

    ssize_t written = archive_write_data(dload->dstArchive, resourceFile->buf, resourceFile->buf_len);
    if( written < 0 ){
        fprintf(stderr, "%s%s\n", "[ERROR] Failed to archive_write_data: ",
            archive_error_string(dload->dstArchive));
        err = -1; goto endFn;
    }else if( written != resourceFile->buf_len ){
        fprintf(stderr, "%s%u%s"FMT_SIZE_T"\n", "[ERROR] archive_write_data failed to write all ",
            resourceFile->buf_len, " bytes. Instead it wrote ", written);
        err = -1; goto endFn;
    }
    resourceFile->buf_len = 0;

    err = 0;
endFn:
    return err;
}


/** @return 0:Reject, 1:Accept, <0:ERROR */
static ssize_t pathFilterAcceptsEntry( ClsDload*dload, ResourceDir*resourceDir, const char*nameOrig ){
    ssize_t err;
    char *name = strdup(nameOrig);
    uint_t name_len = strlen(name);

    if( dload->resclone->filter ){
        // Count parents to find correct regex to apply.
        uint_t idx = 0;
        for( ResourceDir*it=resourceDir->parentDir ; it ; it=it->parentDir ){ ++idx; }
        // Check if we even have such a long filter at all.
        if( idx >= dload->resclone->filter_len ){
            if( dload->resclone->isFilterFull ){
                //fprintf(stderr, "%s\n", "[DEBUG] Path longer than --filter-full -> reject.");
                err = 0; goto endFn;
            }else{
                //fprintf(stderr, "%s\n", "[DEBUG] Path longer than --filter-part -> accept.");
                err = 1; goto endFn;
            }
        }
        // We have a regex. Setup the check.
        int restoreEndSlash = 0;
        if( name[name_len-1] == '/' ){
            restoreEndSlash = !0;
            name[name_len-1] = '\0';
        }
        //fprintf(stderr, "%s%u%s%s%s\n", "[DEBUG] idx=", idx," name='", name, "'");
        regex_t *filterArr = dload->resclone->filter;
        regex_t *r = filterArr + idx;
        err = regexec(r, name, 0, 0, 0);
        if( !err ){
            //fprintf(stderr, "%s\n", "[DEBUG] Segment accepted by filter.");
            err = 1; /* fall to restoreEndSlash */
        }else if( err == REG_NOMATCH ){
            //fprintf(stderr, "%s\n", "[DEBUG] Segment rejected by filter.");
            err = 0; /* fall to restoreEndSlash */
        }else{
            fprintf(stderr, "%s%.*s%s"FMT_SIZE_T"\n", "[ERROR] regexec(rgx, '", (int)name_len, name, "') -> ", err);
            err = -1; /* fall to restoreEndSlash */
        }
        if( restoreEndSlash ){
            name[name_len-1] = '/';
        }
        goto endFn;
    }

    err = 1; /* accept by default */
endFn:
    free(name);
    return err;
}


/** Gets called for every resource to scan/download.
 * HINT: Gets called recursively. */
static ssize_t gateleenResclone_download( ClsDload*dload , ResourceDir*parentResourceDir , char*entryName ){
    ssize_t err;
    char *url = NULL;
    int url_len = 0;
    cJSON *jsonRoot = NULL;
    int resUrl_len = 0;
    ResourceDir *resourceDir = NULL;
    ResourceFile *resourceFile = NULL;

    if( entryName == NULL ){
        /* Is the case when its the root call and not a recursive one */
        entryName = dload->rootUrl;
    }

    // Stack-Alloc resourceDir-closure.
    ResourceDir _1 = {0}; resourceDir = &_1;
    resourceDir->dload = dload;
    resourceDir->parentDir = parentResourceDir;
    resourceDir->name = strdup(entryName);

    // Configure client
    {
        url_len = 0;
        for( ResourceDir*d=resourceDir ; d ; d=d->parentDir ){
            int len = strlen(d->name) - strspn(d->name, "/");
            url_len += len;
        }
        url = malloc(url_len +1 /*MayPreventReallocLaterForName*/+24);
        char *u = url + url_len;
        for( ResourceDir*d=resourceDir ; d ; d=d->parentDir ){
            char *name = d->name + strspn(d->name, "/");
            int name_len = strlen(name);
            memcpy(u-name_len, name, name_len); u -= name_len;
        }
        url[url_len] = '\0';
        //fprintf(stderr, "%s%s%s\n", "[DEBUG] URL '", url, "'");
        err =  CURLE_OK != curl_easy_setopt(dload->curl, CURLOPT_URL, url)
            || CURLE_OK != curl_easy_setopt(dload->curl, CURLOPT_FOLLOWLOCATION, 0L)
            || CURLE_OK != curl_easy_setopt(dload->curl, CURLOPT_WRITEFUNCTION, onCurlDirRsp)
            || CURLE_OK != curl_easy_setopt(dload->curl, CURLOPT_WRITEDATA, resourceDir)
            ;
        if( err ){
            assert(!err); err = -1; goto endFn; }
    }

    err = curl_easy_perform(dload->curl);
    if( err != CURLE_OK ){
        fprintf(stderr, "%s%s%s"FMT_SIZE_T"%s%s\n",
            "[ERROR] '", url, "' (code ", err, "): ", curl_easy_strerror(err));
        err = -1; goto endFn;
    }

    if( resourceDir->rspCode == ERR_PARSE_DIR_LIST ){
        err = 0; goto endFn; /* Already logged by sub-ctxt. Simply skip to next entry. */
    }
    if( resourceDir->rspCode != 200 ){
        // Ugh? Just one request earlier, server said there's a directory on
        // that URL. Nevermind. Just skip it and at least download the other
        // stuff.
        fprintf(stderr, "%s%d%s%s%s\n", "[INFO ] Skip HTTP ", resourceDir->rspCode, " -> '", url, "'");
        err = 0; goto endFn;
    }

    // Parse the collected response body.
    jsonRoot = cJSON_Parse(resourceDir->rspBody);
    if( ! cJSON_IsObject(jsonRoot) ){ // TODO: Handle case
        fprintf(stderr, "%s\n", "[ERROR] JSON root expected to be object but is not.");
        err = -1; goto endFn;
    }

    /* Do some validations to get to the payload we're interested in. */
    if( cJSON_GetArraySize(jsonRoot) != 1 ){
        fprintf(stderr, "%s%d\n", "[ERROR] JSON root expected ONE child but got ",
            cJSON_GetArraySize(jsonRoot));
        err = -1; goto endFn;
    }
    cJSON *data = jsonRoot->child;
    //fprintf(stderr, "%s%s%s\n", "[DEBUG] Processing json['", data->string, "']");
    if( ! cJSON_IsArray(data) ){
        fprintf(stderr, "%s%s%s\n", "[ERROR] json['", data->string,
            "'] expected to be an array. But is not.");
        err = -1; goto endFn;
    }

    // Iterate all the entries we have to process.
    ResourceFile _2 = {0}; resourceFile =&_2;
    resourceFile->dload = dload;
    uint_t iDirEntry = 0;
    for( cJSON *arrEntry=data->child ; arrEntry!=NULL ; arrEntry=arrEntry->next ){
        if( ! cJSON_IsString(arrEntry) ){
            fprintf(stderr, "%s%s%s%u%s\n", "[ERROR] ", data->string, "['", iDirEntry,
                "'] expected to be a string. But is not." );
            err = -1; goto endFn;
        }
        //fprintf(stderr, "%s%s%s%u%s%s\n", "[DEBUG] ", data->string, "[", iDirEntry, "] -> ", arrEntry->valuestring);
        char *name = arrEntry->valuestring;
        int name_len = strlen(name);

        err = pathFilterAcceptsEntry(dload, resourceDir, name);
        if( err < 0 ){ /* ERROR */
            goto endFn;
        }else if( err == 0 ){ /* REJECT */
            fprintf(stderr, "%s%s%s%s\n", "[INFO ] Skip     '", url, name, "'  (filtered)");
            continue;
        }else{ /* ACCEPT */
            /* Go ahead */
        }

        if( name[name_len-1] == '/' ){ /* Gateleen reports a 'directory' */
            //fprintf(stderr, "%s%s%s%s\n", "[DEBUG] Scan     '", url, name,"'");
            err = gateleenResclone_download(dload, resourceDir, name);
            if( err ){
                goto endFn; }
        }else{ /* Not a 'dir'? Then assume 'file' */
            int requiredLen = url_len + 1/*slash*/ + name_len;
            if( resUrl_len < requiredLen ){
                void *tmp = realloc(resourceFile->url, requiredLen +1);
                if( tmp == NULL ){
                    err = -ENOMEM; goto endFn; }
                resourceFile->url = tmp;
                resUrl_len = requiredLen;
            }
            sprintf(resourceFile->url, "%s%s", url, name);
            fprintf(stderr, "%s%s%s\n", "[INFO ] Download '", resourceFile->url, "'");
            resourceFile->buf_len = 0; // <- Reset before use.
            collectResourceIntoMemory(resourceFile, resourceFile->url);
            copyBufToArchive(resourceFile);
        }

        iDirEntry += 1;
    }

    err = 0; /* OK */
endFn:
    if( jsonRoot != NULL ){ cJSON_Delete(jsonRoot); }
    if( resourceFile ){
        free(resourceFile->buf); resourceFile->buf = NULL;
        free(resourceFile->url); resourceFile->url = NULL;
    }
    if( resourceDir ){
        free(resourceDir->name); resourceDir->name = NULL;
        free(resourceDir->rspBody); resourceDir->rspBody = NULL;
    }
    free(url);
    return err;
}


static void Resclone_free( Resclone*resclone ){
    if( resclone == NULL ) return;
    // TODO need free? -> char *url;
    // TODO need free? -> regex_t *filter;
    // TODO need free? -> char *file;
    free(resclone);
}


static Resclone* Resclone_alloc(){
    ssize_t err;
    Resclone *resclone = NULL;

    err = curl_global_init(CURL_GLOBAL_ALL);
    if( err ){
        assert(!err); goto fail; }

    resclone = calloc(1, sizeof*resclone);

    return resclone;
    fail:
    Resclone_free(resclone);
    return NULL;
}


static size_t onUploadChunkRequested( char*buf, size_t size, size_t count, void*Put_ ){
    int err;
    Put *put = Put_;
    Upload *upload = put->upload;
    const size_t buf_len = size * count;

    ssize_t readLen = archive_read_data(upload->srcArchive, buf, buf_len);
    //fprintf(stderr, "%s%lu%s\n", "[DEBUG] Cpy ", readLen, " bytes.");
    if( readLen < 0 ){
        fprintf(stderr, "%s"FMT_SIZE_T"%s%s\n", "[ERROR] Failed to read from archive (code ",
            readLen, "): ", archive_error_string(upload->srcArchive));
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
    return err >= 0 ? err : CURL_READFUNC_ABORT;
}


static ssize_t addContentTypeHeader( Put*put, struct curl_slist *reqHdrs ){
    ssize_t err;
    char *contentTypeHdr = NULL;
    Upload *upload = put->upload;
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
            //fprintf(stderr, "%s%s%s%s%s\n", "[DEBUG] Resolved file ext '", ext+1,"' to mime '", mimeType?mimeType:"<null>", "'.");
        }
    }else if( *ext=='/' || ext==name || *ext=='\0' ){ // TODO Explain why 0x00.
        mimeType = "application/json";
        //fprintf(stderr, "%s\n", "[DEBUG] No file extension. Fallback to json (gateleen default)");
    }else{
        mimeType = NULL;
    }
    if( mimeType == NULL ){
        //fprintf(stderr, "%s%s%s\n", "[DEBUG] Unknown file extension '", ext+1, "'. Will NOT add Content-Type header.");
        mimeType = ""; // <- Need to 'remove' header. To do this, pass an empty value to curl.
    }
    uint_t mimeType_len = strlen(mimeType);
    static const char contentTypePrefix[] = "Content-Type: ";
    static const uint_t contentTypePrefix_len = sizeof(contentTypePrefix)-1;
    contentTypeHdr = malloc( contentTypePrefix_len + mimeType_len +1 );
    memcpy(contentTypeHdr , contentTypePrefix , contentTypePrefix_len);
    memcpy(contentTypeHdr+contentTypePrefix_len , mimeType , mimeType_len+1);
    reqHdrs = curl_slist_append(reqHdrs, contentTypeHdr);
    err = curl_easy_setopt(upload->curl, CURLOPT_HTTPHEADER, reqHdrs);
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
    struct curl_slist *reqHdrs = NULL;

    int rootUrl_len = strlen(upload->rootUrl);
    if( upload->rootUrl[rootUrl_len-1]=='/' ){
        rootUrl_len -= 1;
    }
    int url_len = strlen(upload->rootUrl) + strlen(put->name);
    url = malloc(url_len +2);
    if( url == NULL ){
        err = -ENOMEM; goto endFn; }
    sprintf(url, "%.*s/%s", rootUrl_len,upload->rootUrl, put->name);
    err =  CURLE_OK != curl_easy_setopt(upload->curl, CURLOPT_URL, url)
        || addContentTypeHeader(put, reqHdrs)
        ;
    if( err ){
        assert(!err); err = -1; goto endFn; }

    fprintf(stderr, "%s%s%s\n", "[INFO ] Upload '", url, "'");
    err = curl_easy_perform(upload->curl);
    if( err != CURLE_OK ){
        fprintf(stderr, "%s%s%s"FMT_SIZE_T"%s%s\n",
            "[ERROR] PUT '", url, "' (code ", err, "): ", curl_easy_strerror(err));
        err = -1; goto endFn;
    }
    long rspCode;
    curl_easy_getinfo(upload->curl, CURLINFO_RESPONSE_CODE, &rspCode);
    if( rspCode <= 199 || rspCode >= 300 ){
        fprintf(stderr, "%s%ld%s%s%s\n",
            "[WARN ] Got RspCode ", rspCode, " for 'PUT ", url, "'");
    }else{
        //fprintf(stderr, "%s%ld%s%s%s\n", "[DEBUG] Got RspCode ", rspCode, " for 'PUT ", url, "'");
    }

    err = 0;
endFn:
    curl_slist_free_all(reqHdrs);
    free(url);
    return err;
}


static ssize_t readArchive( Upload*upload ){
    ssize_t err;
    Put *put = NULL;

    upload->srcArchive = archive_read_new();
    if( ! upload->srcArchive ){
        assert(upload->srcArchive); err = -1; goto endFn; }

    const int blockSize = (1<<14);
    err = archive_read_support_format_all(upload->srcArchive)
       || archive_read_open_filename(upload->srcArchive, upload->archiveFile, blockSize)
       ;
    if( err ){
        fprintf(stderr, "%s"FMT_SIZE_T"%s%s\n", "[ERROR] Failed to open src archive (code ", err, "): ",
            curl_easy_strerror(err));
        err = -1; goto endFn;
    }

    err = curl_easy_setopt(upload->curl, CURLOPT_UPLOAD, 1L)
       || curl_easy_setopt(upload->curl, CURLOPT_READFUNCTION, onUploadChunkRequested)
        ;
    if( err ){
        assert(!err); err = -1; goto endFn; }
    for( struct archive_entry*entry ; archive_read_next_header(upload->srcArchive,&entry) == ARCHIVE_OK ;){
        const char *name = archive_entry_pathname(entry);
        int ftype = archive_entry_filetype(entry);
        if( ftype == AE_IFDIR ){
            continue; // Ignore dirs because gateleen doesn't know 'dirs' as such.
        }
        if( ftype != AE_IFREG ){
            fprintf(stderr, "%s%s%s\n", "[WARN ] Ignore non-regular file '", name, "'");
            continue;
        }
        //fprintf(stderr, "%s%s%s\n", "[DEBUG] Reading '",name,"'");
        Put _1 = {
            .upload = upload,
            .name = (char*)name
        }; put = &_1;
        err = curl_easy_setopt(upload->curl, CURLOPT_READDATA, put)
            || httpPutEntry(put);
        //curl = upload->curl; // Sync back. TODO: Still needed?
        if( err ){
            assert(!err); err = -1; goto endFn; }
    }

    err = 0;
endFn:
    return err;
}


static ssize_t pull( Resclone*resclone ){
    ssize_t err;
    ClsDload *dload = NULL;

    if( resclone->file == NULL && isatty(1) ){
        fprintf(stderr, "%s\n",
            "[ERROR] Are you sure you wanna write binary content to tty?");
        err = -1; goto endFn;
    }

    ClsDload _1 = {0}; dload =&_1;
    dload->resclone = resclone;
    dload->rootUrl = resclone->url;
    dload->archiveFile = resclone->file;
    dload->curl = curl_easy_init();
    if( dload->curl == NULL ){
        fprintf(stderr, "%s\n", "[ERROR] curl_easy_init() -> NULL");
        err = -1; goto endFn;
    }

    err = gateleenResclone_download(dload, NULL, NULL);
    if( err ){
        err = -1; goto endFn; }

    if( dload->dstArchive && archive_write_close(dload->dstArchive) ){
        fprintf(stderr, "%s"FMT_SIZE_T"%s%s\n", "[ERROR] archive_write_close failed (code ",
            err, "): ", archive_error_string(dload->dstArchive));
        err = -1; goto endFn;
    }

    err = 0;
endFn:
    if( dload ){
        curl_easy_cleanup(dload->curl);
        archive_entry_free(dload->tmpEntry); dload->tmpEntry = NULL;
        archive_write_free(dload->dstArchive); dload->dstArchive = NULL;
    }
    return err;
}


static ssize_t push( Resclone*resclone ){
    ssize_t err;
    Upload *upload = NULL;

    Upload _1={0}; upload =&_1;
    upload->resclone = resclone;
    upload->archiveFile = resclone->file;
    upload->rootUrl = resclone->url;
    upload->curl = curl_easy_init();
    if( ! upload->curl ){
        fprintf(stderr, "%s\n", "[ERROR] curl_easy_init() -> NULL");
        err = -1; goto endFn;
    }

    err = readArchive(upload);
    if( err ){
        err = -1; goto endFn; }

    err = 0;
endFn:
    if( upload ){
        curl_easy_cleanup(upload->curl);
        archive_read_free(upload->srcArchive);
    }
    return err;
}


ssize_t gateleenResclone_run( int argc, char**argv ){
    ssize_t err;
    Resclone *resclone = NULL;

    resclone = Resclone_alloc();
    if( resclone == NULL ){
        err = -1; goto endFn; }

    err = parseArgs(argc, argv, &resclone->mode, &resclone->url, &resclone->filter,
        &resclone->filter_len, &resclone->isFilterFull, &resclone->file);
    if( err ){
        err = -1; goto endFn; }

    if( resclone->mode == MODE_FETCH ){
        err = pull(resclone); goto endFn;
    }else if( resclone->mode == MODE_PUSH ){
        err = push(resclone); goto endFn;
    }else{
        err = -1; goto endFn;
    }

    assert(!"Unreachable");
endFn:
    parseArgs(-1, argv, &resclone->mode, &resclone->url, &resclone->filter, &resclone->filter_len,
        &resclone->isFilterFull, &resclone->file);
    resclone->mode = MODE_NULL; resclone->url = NULL; resclone->file = NULL;
    Resclone_free(resclone);
    return err;
}


int gateleenResclone_main( int argc, char**argv ){
    int ret;
    ret = gateleenResclone_run(argc, argv);
    if( ret < 0 ){ ret = 0 - ret; }
    return (ret > 127) ? 1 : ret;
}


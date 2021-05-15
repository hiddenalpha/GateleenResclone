/* By using this work you agree to the terms and conditions in 'LICENCE.txt' */

/* This Unit */
#include "gateleen_resclone.h"

/* System */
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <regex.h>
#include <stdbool.h>
#include <string.h>

/* Libs */
#include "archive.h"
#include "archive_entry.h"
#include <curl/curl.h>
#include <cJSON.h>

/* Project */
#include "array.h"
#include "log.h"
#include "mime.h"
#include "util_string.h"


/* Types *********************************************************************/

#define ERR_PARSE_DIR_LIST -2



/** Operation mode. */
typedef enum opMode {
    MODE_NULL =0,
    MODE_FETCH=1,
    MODE_PUSH =2
} opMode_t;


/** Main module handle representing an instance. */
typedef struct this {
    enum opMode mode;
    /** Base URL where to upload to / download from. */
    char *url;
    /** Array of regex patterns to use per segment. */
    regex_t *filter;
    size_t filter_len;
    /** Says if only start or whole path needs to match the pattern. */
    bool isFilterFull;
    /* Path to archive file to use. Using stdin/stdout if NULL. */
    char *file;
} this_t;


/** Closure for a download instructed by external caller. */
typedef struct cls_dload {
    struct this *this;
    char *rootUrl;
    struct archive *dstArchive;
    struct archive_entry *tmpEntry;
    char *archiveFile;
    CURL *curl;
} cls_dload_t;


/** Closure for a collection (directory) download. */
typedef struct cls_resourceDir {
    struct cls_dload *dload;
    //yajl_handle yajl; <- TODO Obsolete
    struct cls_resourceDir *parentDir;
    char *rspBody;
    size_t rspBody_len;
    size_t rspBody_cap;
    short rspCode;
    char *name;
} cls_resourceDir_t;


/** Closure for a file download. */
typedef struct cls_resourceFile {
    struct cls_dload *dload;
    size_t srcChunkIdx;
    char *url;
    char *buf;
    int buf_len;
    int buf_memSz;
} cls_resourceFile_t;


/** Closure for an upload instructed by external caller. */
typedef struct cls_upload {
    struct this *this;
    char *rootUrl;
    char *archiveFile;
    struct archive *srcArchive;
    CURL *curl;
} cls_upload_t;


/** Closure for a PUT of a single resource. */
typedef struct cls_put {
    struct cls_upload *upload;
    /* Path (relative to rootUrl) of the resource to be uploaded. */
    char *name;
} cls_put_t;



/* Operations ****************************************************************/


TPL_ARRAY( str, char*, 16 );


static void
printHelp( void )
{
    char filename[sizeof(__FILE__)];
    strcpy( filename, __FILE__ );
    printf("%s%s%s",
        "\n"
        "  ", basename(filename), " - " STR_QUOT(PROJECT_VERSION) "\n"
        "\n"
        "Options:\n"
        "\n"
        "    --pull|--push\n"
        "        Choose to download or upload.\n"
        "\n"
        "    --url <url>\n"
        "        Root node of remote tree.\n"
        "\n"
        "    --filter-part <path-filter>\n"
        "        Regex pattern applied as predicate to the path starting after the path\n"
        "        specified in '--url'. Each path segment will be handled as its\n"
        "        individual pattern. If there are longer paths to process, they will be\n"
        "        accepted, as long they at least start-with specified filter.\n"
        "        Example:  /foo/[0-9]+/bar\n"
        "\n"
        "    --filter-full <path-filter>\n"
        "        Nearly same as '--filter-part'. But paths with more segments than the\n"
        "        pattern, will be rejected.\n"
        "\n"
        "    --file <path.tar>\n"
        "        (optional) Path to the archive file to read/write. Defaults to\n"
        "        stdin/stdout if ommitted.\n"
        "\n"
    );
}


static int
parseArgs( int argc, char**argv, opMode_t*mode, char**url, regex_t**filter, size_t*filter_cnt,
    bool*isFilterFull, char**file )
{
    ssize_t err, ret=0;
    char *filterRaw = NULL;
    if( argc == -1 ){ // -1 indicates the call to free our resources. So simply jump
        goto fail;    // to 'fail' because that has the same effect.
    }
    *mode = 0;
    *url = NULL;
    *filter = NULL;
    *filter_cnt = 0;
    *isFilterFull = false;
    *file = NULL;

    for( int i=1 ; i<argc ; ++i ){
        char *arg = argv[i];
        if( !strcmp(arg,"--help") ){
            printHelp();
            ret = -1; goto fail;
        }else if( !strcmp(arg,"--pull") ){
            if( *mode ){
                printf("%s\n","ERROR: Mode already specified. Won't set '--pull'.");
                ret = -1; goto fail;
            }
            *mode = MODE_FETCH;
        }else if( !strcmp(arg,"--push") ){
            if( *mode ){
                printf("%s\n","ERROR: Mode already specified. Won't set '--push'.");
                ret = -1; goto fail;
            }
            *mode = MODE_PUSH;
        }else if( !strcmp(arg,"--url") ){
            if(!( arg=argv[++i]) ){
                printf("%s\n","ERROR: Arg '--url' needs a value.");
                ret = -1; goto fail;
            }
            *url = arg;
        }else if( !strcmp(arg,"--filter-full") ){
            if(!( arg=argv[++i] )){
                printf("%s\n","ERROR: Arg '--filter-full' needs a value.");
                ret = -1; goto fail; }
            if( filterRaw ){
                printf("%s\n","ERROR: Cannot use '--filter-full' because a filter is already set.");
                ret=-1; goto fail; }
            filterRaw = arg;
            *isFilterFull = true;
        }else if( !strcmp(arg,"--filter-part") ){
            if(!( arg=argv[++i] )){
                printf("%s\n","ERROR: Arg '--filter-part' needs a value.");
                ret = -1; goto fail; }
            if( filterRaw ){
                printf("%s\n","ERROR: Cannot use '--filter-part' because a filter is already set.");
                ret=-1; goto fail; }
            filterRaw = arg;
            *isFilterFull = false;
        }else if( !strcmp(arg,"--file") ){
            if(!( arg=argv[++i]) ){
                printf("%s\n","ERROR: Arg '--file' needs a value.");
                ret = -1; goto fail;
            }
            *file = arg;
        }else{
            printf("%s%s\n", "ERROR: Unknown arg ",arg);
            ret = -1; goto fail;
        }
    }

    if( *mode==0 ){
        printf("%s\n", "ERROR: One of --push or --pull required.");
        ret = -1; goto fail;
    }

    if( *url==NULL ){
        printf("%s\n", "ERROR: Arg --url missing.");
        ret = -1; goto fail;
    }
    uint_t urlFromArgs_len = strlen( *url );
    if( ((*url)[urlFromArgs_len-1]) != '/' ){
        char *urlFromArgs = *url;
        uint_t url_len = urlFromArgs_len + 1;
        *url = malloc(url_len+1); /* TODO: Should we free this? */
        memcpy( *url, urlFromArgs, urlFromArgs_len );
        (*url)[url_len-1] = '/';
        (*url)[url_len] = '\0';
    }else{
        *url = strdup( *url ); // Make our own string (so we've no fear to call
                               // free on it later)
    }

    if( filterRaw ){
        uint_t buf_len = strlen( filterRaw );
        char *buf = malloc( 1+ buf_len +2 );
        buf[0] = '_'; // <- Match whole segment.
        memcpy( buf+1, filterRaw, buf_len+1 );
        char *beg, *end;
        size_t filter_cap = 0;
        end = buf+1; // <- Initialize at begin for 1st iteration.
        for( uint_t iSegm=0 ;; ++iSegm ){
            for( beg=end ; *beg=='/' ; ++beg ); // <- Search for begin and
            for( end=beg ; *end!='/' && *end!='\0' ; ++end ); // <- end of current segment.
            char origBeg = beg[-1];
            char origSep = *end;
            char origNext = end[1];
            beg[-1] = '^'; // <- Add 'match-start' so we MUST match whole segment.
            *end = '$';    // <- Add 'match-end' so we must match whole segment.
            end[1] = '\0'; // <- Temporary terminate to compile segment only.
            if( iSegm >= filter_cap ){
                filter_cap += 8;
                *filter = realloc( *filter, filter_cap*sizeof(**filter) );
                //LOG_DEBUG("%s%u%s%p\n", "realloc( NULL, ", filter_cap*sizeof(**filter)," ) -> ", *filter );
                if( !*filter ){ LOG_ERROR("%s%d%s\n","realloc(",filter_cap*sizeof(**filter),")"); ret=-ENOMEM; goto fail; }
            }
            //LOG_DEBUG("%s%d%s%s%s\n", "filter[", iSegm, "] -> '", beg-1, "'");
            err = regcomp( (*filter)+iSegm, beg-1, REG_EXTENDED);
            if( err ){ LOG_ERROR("%s%s%s%d\n","regcomp(",beg,") -> ", err); return -1; }
            // Restore surrounding stuff.
            beg[-1] = origBeg;
            *end = origSep; // <- Restore tmp 'end-of-match' ($).
            end[1] = origNext; // <- Restore tmp termination.
            if( *end=='\0' ){ // EOF
                *filter_cnt = iSegm +1;
                *filter = realloc( *filter, *filter_cnt *sizeof(**filter) ); // Trim result.
                free( buf ); buf=NULL;
                break;
            }
        }
    }

    if( *mode==MODE_PUSH && *filter ){
        printf("%s\n", "ERROR: Filtering not supported for push mode.");
        ret = -1; goto fail;
    }

    return 0;
    fail:
    free( *url ); *url=NULL;
    for( uint_t i=0 ; i<*filter_cnt ; ++i ){
        regfree( &(filter[0][i]) );
    }
    *filter_cnt = 0;
    free( *filter ); *filter=NULL;
    return ret;
}


static size_t
onCurlDirRsp( char*buf , size_t size , size_t nmemb , void*cls_resourceDir_ )
{
    //LOG_DEBUG( "%s%s%p%s%ld%s%ld%s%p%s\n" , __func__, "( buf=", buf, ", size=", size, ", nmemb=", nmemb, ", cls=", cls_resourceDir_, " )" );
    ssize_t err, ret = size*nmemb;
    cls_resourceDir_t *resourceDir = cls_resourceDir_;
    cls_dload_t *dload = resourceDir->dload;
    CURL *curl = dload->curl;
    const size_t buf_len = size * nmemb;

    long rspCode;
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &rspCode );
    resourceDir->rspCode = rspCode;
    if( rspCode != 200 ){
        goto endFn;
    }

    // Collect whole response body into one buf (as cJSON seems unable to parse
    // partially)
    if( resourceDir->rspBody_cap < resourceDir->rspBody_len + buf_len +1 ){
        // Enlarge buf
        resourceDir->rspBody_cap = resourceDir->rspBody_len + buf_len + 1024;
        resourceDir->rspBody = realloc( resourceDir->rspBody, resourceDir->rspBody_cap );
    }
    memcpy( resourceDir->rspBody+resourceDir->rspBody_len, buf, buf_len );
    resourceDir->rspBody_len += buf_len;
    resourceDir->rspBody[resourceDir->rspBody_len] = '\0';

    // Parsing occurs in the caller, as soon we processed whole response.

    endFn:
    return ret;
}


static size_t
onResourceChunk( char*buf , size_t size , size_t nmemb , void*cls_resourceFile_ )
{
    int buf_len = size * nmemb;
    int ret = buf_len;
    cls_resourceFile_t *resourceFile = cls_resourceFile_;

    int avail = resourceFile->buf_memSz - resourceFile->buf_len;
    if( avail <= buf_len ){
        resourceFile->buf_memSz += buf_len - avail +1;
        resourceFile->buf = realloc( resourceFile->buf , resourceFile->buf_memSz );
    }
    char *it = resourceFile->buf + resourceFile->buf_len;
    memcpy( it , buf , buf_len ); it+=buf_len;
    *it = '\0';
    resourceFile->buf_len = it - resourceFile->buf;

    return ret;
}


static ssize_t
collectResourceIntoMemory( cls_resourceFile_t*resourceFile , char*url )
{
    ssize_t err, ret=0;
    cls_dload_t *dload = resourceFile->dload;
    CURL *curl = dload->curl;

    err =  CURLE_OK!= curl_easy_setopt(curl, CURLOPT_URL, url)
        || CURLE_OK!= curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L )
        || CURLE_OK!= curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onResourceChunk)
        || CURLE_OK!= curl_easy_setopt(curl, CURLOPT_WRITEDATA, resourceFile)
        ;
    if( err ){ assert(!err); ret=-1; goto finally; }

    err = curl_easy_perform( curl );
    if( err!=CURLE_OK ){
        LOG_ERROR("%s%s%s%s%ld%s%s\n", __func__, "(): '",url,"' (code ", err, "): ", curl_easy_strerror(err));
        ret=-1; goto finally;
    }

    finally:
    return ret;
}


static ssize_t
copyBufToArchive( cls_resourceFile_t*resourceFile )
{
    ssize_t err, ret=0;
    cls_dload_t *dload = resourceFile->dload;
    char *fileName = resourceFile->url + strlen(resourceFile->dload->rootUrl);

    if( ! dload->dstArchive ){
        // Setup archive if not setup yet.
        dload->dstArchive = archive_write_new();
        err =  archive_write_set_format_pax_restricted( dload->dstArchive )
            || archive_write_open_filename( dload->dstArchive , dload->archiveFile )
            ;
        if( err ){
            LOG_ERROR("%s%s%s\n", __func__, "(): Failed to setup tar output: ", archive_error_string(dload->dstArchive));
            ret = -1; goto endFn;
        }
    }

    if( ! dload->tmpEntry ){
        dload->tmpEntry = archive_entry_new();
    }else{
        dload->tmpEntry = archive_entry_clear( dload->tmpEntry );
    }
    archive_entry_set_pathname(dload->tmpEntry, fileName);
    archive_entry_set_filetype(dload->tmpEntry, AE_IFREG);
    archive_entry_set_size(dload->tmpEntry, resourceFile->buf_len);
    archive_entry_set_perm(dload->tmpEntry, 0644);
    err = archive_write_header(dload->dstArchive, dload->tmpEntry);
    if( err ){ ret=-1; goto endFn; }

    ssize_t written = archive_write_data( dload->dstArchive , resourceFile->buf , resourceFile->buf_len );
    if( written < 0 ){
        LOG_ERROR("%s%s%s\n", __func__, "Failed to archive_write_data: ", archive_error_string(dload->dstArchive));
        ret = -1; goto endFn;
    }else if( written != resourceFile->buf_len ){
        LOG_ERROR("%s%s%u%s%lu\n", __func__, "(): archive_write_data failed to write all ", resourceFile->buf_len, " bytes. Instead it wrote ", written);
        ret = -1; goto endFn;
    }
    resourceFile->buf_len = 0;

    endFn:
    return ret;
}


/** @return 0:Reject, 1:Accept, <0:ERROR */
static ssize_t pathFilterAcceptsEntry( cls_dload_t*dload, cls_resourceDir_t*resourceDir, const char*nameOrig )
{
    ssize_t err, ret=1; // <- Accept per default.
    char *name = strdup( nameOrig );
    uint_t name_len = strlen( name );

    if( dload->this->filter ){
        // Count parents to find correct regex to apply.
        uint_t idx = 0;
        for( cls_resourceDir_t*it=resourceDir->parentDir ; it ; it=it->parentDir ){ ++idx; }
        // Check if we even have such a long filter at all.
        if( idx >= dload->this->filter_len ){
            if( dload->this->isFilterFull ){
                //LOG_DEBUG("%s\n", "Path longer than --filter-full -> reject.");
                ret = 0; goto finally;
            }else{
                //LOG_DEBUG("%s\n", "Path longer than --filter-part -> accept.");
                ret = 1; goto finally;
            }
        }
        // We have a regex. Setup the check.
        bool restoreEndSlash = false;
        if( name[name_len-1]=='/' ){
            restoreEndSlash = true;
            name[name_len-1] = '\0';
        }
        //LOG_DEBUG("%s%u%s%s%s\n", "idx=",idx," name='", name, "'");
        regex_t *filterArr = dload->this->filter;
        regex_t *r = filterArr + (idx);
        err = regexec( r, name, 0, 0, 0 );
        if( ! err ){
            //LOG_DEBUG("%s\n", "Segment accepted by filter.");
            ret = 1;
        }else if( err==REG_NOMATCH ){
            //LOG_DEBUG("%s\n", "Segment rejected by filter.");
            ret = 0;
        }else{
            LOG_ERROR("%s%.*s%s%d\n", "regexec(rgx, '",(int)name_len,name,"') -> ", err );
            ret = -1;
        }
        if( restoreEndSlash ){
            name[name_len-1] = '/';
        }
        goto finally;
    }

    finally:
    free( name );
    return ret;
}


/** Gets called for every resource to scan/download.
 * HINT: Gets called recursively. */
static ssize_t
gateleenResclone_download( cls_dload_t*dload , cls_resourceDir_t*parentResourceDir , char*entryName )
{
    ssize_t err, ret=0;
    char *url = NULL;
    int url_len = 0;
    cJSON *jsonRoot = NULL;
    int resUrl_len = 0;
    cls_resourceDir_t *resourceDir = NULL;
    cls_resourceFile_t *resourceFile = NULL;

    if( !entryName ){
        // Is the case when its the root call and not a recursive one.
        entryName = dload->rootUrl;
    }

    // Stack-Alloc resourceDir-closure.
    cls_resourceDir_t _1={0}; resourceDir =&_1;
    resourceDir->dload = dload;
    resourceDir->parentDir = parentResourceDir;
    resourceDir->name = strdup( entryName );

    // Configure client
    {
        url_len = 0;
        for( cls_resourceDir_t*d=resourceDir ; d ; d=d->parentDir ){
            int len = strlen(d->name) - strspn(d->name, "/");
            url_len += len;
        }
        url = malloc( url_len +1 /*MightPreventReallocLaterForName*/+24 );
        char *u = url + url_len;
        for( cls_resourceDir_t*d=resourceDir ; d ; d=d->parentDir ){
            char *name = d->name + strspn( d->name , "/" );
            int name_len = strlen(name);
            memcpy( u-name_len , name , name_len ); u-=name_len;
        }
        url[url_len] = '\0';
        //LOG_DEBUG("%s%s%s%s\n", __func__, "(): URL '",url,"'");
        err =  CURLE_OK != curl_easy_setopt(dload->curl, CURLOPT_URL, url)
            || CURLE_OK != curl_easy_setopt(dload->curl, CURLOPT_FOLLOWLOCATION, 0L )
            || CURLE_OK != curl_easy_setopt(dload->curl, CURLOPT_WRITEFUNCTION, onCurlDirRsp)
            || CURLE_OK != curl_easy_setopt(dload->curl, CURLOPT_WRITEDATA, resourceDir)
            ;
        if( err ){ assert(!err); ret=-1; goto finally; }
    }

    err = curl_easy_perform( dload->curl );
    if( err!=CURLE_OK ){
        LOG_ERROR("%s%s%s%s%ld%s%s\n", __func__, "(): '",url,"' (code ", err, "): ", curl_easy_strerror(err));
        ret=-1; goto finally;
    }

    if( resourceDir->rspCode == ERR_PARSE_DIR_LIST ){
        goto finally; // Already logged by sub-ctxt. Simply skip to next entry.
    }
    if( resourceDir->rspCode != 200 ){
        // Ugh? Just one request earlier, server said there's a directory on
        // that URL. Never mind. Just skip it and at least download the other
        // stuff.
        LOG_WARN("%s%d%s%s%s\n", "Skip HTTP ", resourceDir->rspCode ," -> '",url,"'");
        goto finally;
    }

    // Parse the collected response body.
    // I would like to NOT rely on zero-terminated strings. But did not find any
    // better API for this yet.
    jsonRoot = cJSON_Parse( resourceDir->rspBody );
    if( ! cJSON_IsObject(jsonRoot) ){ // TODO: Handle case
        LOG_ERROR("%s\n", "JSON root expected to be object but is not.");
        err = -1; goto finally;
    }

    // Do some validations to get to the payload we're interested in.
    if( cJSON_GetArraySize(jsonRoot) != 1 ){
        LOG_ERROR("%s%d\n", "JSON root expected ONE child but got ", cJSON_GetArraySize(jsonRoot) );
        err = -1; goto finally;
    }
    cJSON *data = jsonRoot->child;
    //LOG_DEBUG( "%s%s%s\n", "Processing json['", data->string, "']" );
    if( ! cJSON_IsArray(data) ){
        LOG_ERROR("%s%s%s\n", "json['", data->string,"'] expected to be an array. But is not." );
        err = -1; goto finally;
    }

    // Iterate all the entries we have to process.
    cls_resourceFile_t _2={0}; resourceFile =&_2;
    resourceFile->dload = dload;
    uint_t iDirEntry = 0;
    for( cJSON *arrEntry=data->child ; arrEntry!=NULL ; arrEntry=arrEntry->next ){
        if( ! cJSON_IsString(arrEntry) ){
            LOG_ERROR("%s%s%u%s\n", data->string,"['", iDirEntry,"'] expected to be a string. But is not." );
            err = -1; goto finally;
        }
        //LOG_DEBUG("%s%s%u%s%s%s\n", data->string, "[", iDirEntry, "] -> ", arrEntry->valuestring, "");
        char *name = arrEntry->valuestring;
        int name_len = strlen( name );

        err = pathFilterAcceptsEntry( dload, resourceDir, name );
        if( err<0 ){ // ERROR
            ret = err; goto finally;
        }else if( err==0 ){ // REJECT
            LOG_INFO("%s%s%s%s\n", "Skip     '", url, name, "'  (filtered)");
            continue;
        }else{ // ACCEPT
            // Go ahead.
        }

        if( name[name_len-1]=='/' ){ // Gateleen reports a 'directory'
            //LOG_DEBUG("%s%s%s%s\n", "Scan     '", url, name,"'");
            err = gateleenResclone_download( dload , resourceDir , name );
            if( err ){ ret=err; goto finally; }
        }
        else{ // Not a 'dir'? Then assume 'file'
            int requiredLen = url_len + 1/*slash*/ + name_len;
            if( resUrl_len < requiredLen ){
                resourceFile->url = realloc( resourceFile->url , requiredLen +1 );
                resUrl_len = requiredLen;
            }
            sprintf(resourceFile->url, "%s%s", url , name );
            LOG_INFO("%s%s%s\n", "Download '", resourceFile->url, "'");
            resourceFile->buf_len = 0; // <- Reset before use.
            collectResourceIntoMemory( resourceFile , resourceFile->url );
            copyBufToArchive( resourceFile );
        }

        iDirEntry += 1;
    }

    finally:
    if( jsonRoot != NULL ){ cJSON_Delete(jsonRoot); }
    if( resourceFile ){
        free(resourceFile->buf); resourceFile->buf = NULL;
        free( resourceFile->url ); resourceFile->url = NULL;
    }
    if( resourceDir ){
        free( resourceDir->name ); resourceDir->name = NULL;
        free( resourceDir->rspBody ); resourceDir->rspBody = NULL;
    }
    free( url );
    return ret;
}


static void
this_free( this_t*this )
{
    if( this==NULL ) return;

    // TODO need free? -> char *url;
    // TODO need free? -> regex_t *filter;
    // TODO need free? -> char *file;

    free( this );
}

static this_t*
this_alloc()
{
    ssize_t err;
    this_t *this = NULL;

    err = curl_global_init( CURL_GLOBAL_ALL );
    if( err ){ assert(!err); goto fail; }

    this = calloc(1, sizeof(*this) );

    return this;
    fail:
    this_free( this );
    return NULL;
}


static size_t
onUploadChunkRequested( char*buf , size_t size , size_t count , void*cls_put_ )
{
    cls_put_t *put = cls_put_;
    cls_upload_t *upload = put->upload;
    const size_t buf_len = size * count;
    ssize_t ret = buf_len;

    ssize_t readLen = archive_read_data(upload->srcArchive, buf, buf_len);
    //LOG_DEBUG("%s%lu%s\n", "Cpy ",readLen," bytes.");
    if( readLen<0 ){
        LOG_ERROR("%s%ld%s%s\n", "Failed to read from archive (code ",readLen,"): ", archive_error_string(upload->srcArchive));
        assert(0); ret=-1; goto endFn;
    }
    else if( readLen>0 ){
        // Regular read. Data already written to 'buf'. Only need to adjust
        // return val.
        ret = readLen;
    }
    else{ // readLen==0 -> EOF
        ret = 0; // Nothing more to read.
    }


    endFn:
    //LOG_DEBUG("%s%s%ld\n", __func__, "() -> ", ret);
    return ret>=0 ? ret : CURL_READFUNC_ABORT;
}


static ssize_t
addContentTypeHeader( cls_put_t*put , struct curl_slist *reqHdrs )
{
    ssize_t err, ret=0;
    char *contentTypeHdr = NULL;
    cls_upload_t *upload = put->upload;
    const char *name = put->name;

    uint_t name_len = strlen( put->name );
    // Find file extension.
    const char *ext = name + name_len;
    for(; ext>name && *ext!='.' && *ext!='/' ; --ext );
    // Convert it to mime type.
    const char *mimeType;
    if( *ext=='.' ){
        mimeType = fileExtToMime( ext+1 ); // <- +1, to skip the (useless) dot.
        if( mimeType ){
            //LOG_DEBUG("%s%s%s%s%s\n", "Resolved file ext '", ext+1,"' to mime '", mimeType?mimeType:"<null>", "'.");
        }
    }
    else if( *ext=='/' || ext==name || *ext=='\0' ){ // TODO Explain why 0x00.
        mimeType = "application/json";
        //LOG_DEBUG("%s\n", "No file extension. Fallback to json (gateleen default)");
    }
    else{
        mimeType = NULL;
    }
    if( mimeType==NULL ){
        //LOG_DEBUG("%s%s%s\n", "Unknown file extension '", ext+1, "'. Will NOT add Content-Type header.");
        mimeType = ""; // <- Need to 'remove' header. To do this, pass an empty value to curl.
    }
    uint_t mimeType_len = strlen( mimeType );
    static const char contentTypePrefix[] = "Content-Type: ";
    static const uint_t contentTypePrefix_len = sizeof(contentTypePrefix)-1;
    contentTypeHdr = malloc( contentTypePrefix_len + mimeType_len +1 );
    memcpy( contentTypeHdr , contentTypePrefix , contentTypePrefix_len );
    memcpy( contentTypeHdr+contentTypePrefix_len , mimeType , mimeType_len+1 );
    reqHdrs = curl_slist_append( reqHdrs, contentTypeHdr );
    err = curl_easy_setopt( upload->curl, CURLOPT_HTTPHEADER, reqHdrs );
    if( err ){ assert(!err); ret=-1; goto endFn; }

    endFn:
    free( contentTypeHdr );
    return ret;
}


static ssize_t
httpPutEntry( cls_put_t*put )
{
    ssize_t err, ret = 0;
    cls_upload_t *upload = put->upload;
    char *url = NULL;
    struct curl_slist *reqHdrs = NULL;

    int rootUrl_len = strlen(upload->rootUrl);
    if( upload->rootUrl[rootUrl_len-1]=='/' ){
        rootUrl_len -= 1;
    }
    int url_len = strlen(upload->rootUrl) + strlen(put->name);
    url = malloc( url_len +2 );
    sprintf(url, "%.*s/%s", rootUrl_len,upload->rootUrl, put->name);
    err =  CURLE_OK != curl_easy_setopt(upload->curl, CURLOPT_URL, url )
        || addContentTypeHeader( put , reqHdrs )
        ;
    if( err ){ assert(!err); ret=-1; goto endFn; }

    LOG_INFO("%s%s%s\n", "Upload '",url,"'");
    err = curl_easy_perform( upload->curl );
    if( err!=CURLE_OK ){
        LOG_ERROR("%s%s%s%ld%s%s\n", "PUT '",url,"' (code ", err, "): ", curl_easy_strerror(err));
        ret=-1; goto endFn;
    }
    long rspCode;
    curl_easy_getinfo(upload->curl, CURLINFO_RESPONSE_CODE, &rspCode);
    if( rspCode<=199 || rspCode>=300 ){
        LOG_WARN("%s%ld%s%s%s\n", "Got RspCode ", rspCode, " for 'PUT ", url, "'");
    }else{
        //LOG_DEBUG("%s%ld%s%s%s\n", "Got RspCode ", rspCode, " for 'PUT ", url, "'");
    }

    endFn:
    curl_slist_free_all( reqHdrs );
    free( url );
    return ret;
}


static ssize_t
readArchive( cls_upload_t*upload )
{
    ssize_t err, ret=0;
    cls_put_t *put = NULL;

    upload->srcArchive = archive_read_new();
    if( ! upload->srcArchive ){ assert(upload->srcArchive); ret=-1; goto endFn; }

    const int blockSize = 16384; // <- Because curl examples use this value too.
    err = archive_read_support_format_all( upload->srcArchive )
       || archive_read_open_filename( upload->srcArchive , upload->archiveFile , blockSize )
       ;
    if( err ){
        LOG_ERROR("%s%ld%s%s\n", "Failed to open src archive (code ",err,"): ", curl_easy_strerror(err) );
        ret=-1; goto endFn;
    }

    err = curl_easy_setopt( upload->curl, CURLOPT_UPLOAD, 1L)
       || curl_easy_setopt( upload->curl, CURLOPT_READFUNCTION, onUploadChunkRequested )
        ;
    if( err ){ assert(!err); ret=-1; goto endFn; }
    for( struct archive_entry*entry ; archive_read_next_header(upload->srcArchive,&entry)==ARCHIVE_OK ;){
        const char *name = archive_entry_pathname( entry );
        int ftype = archive_entry_filetype( entry );
        if( ftype == AE_IFDIR ){
            continue; // Ignore dirs because gateleen doesn't know 'dirs' as such.
        }
        if( ftype != AE_IFREG ){
            LOG_WARN("%s%s%s\n", "Ignore non-regular file '", name, "'");
            continue;
        }
        //LOG_DEBUG("%s%s%s\n", "Reading '",name,"'");
        cls_put_t _1={
            .upload = upload,
            .name = (char*)name
        }; put =&_1;
        err = curl_easy_setopt( upload->curl, CURLOPT_READDATA, put )
            || httpPutEntry( put );
        //curl = upload->curl; // Sync back. TODO: Still needed?
        if( err ){ ret=-1; goto endFn; }
    }

    endFn:
    return ret;
}


static ssize_t
pull( this_t*this )
{
    ssize_t err, ret = 0;
    cls_dload_t *dload = NULL;

    if( this->file==NULL && isatty(1) ){
        LOG_ERROR("%s\n", "Are you sure you wanna write binary content to tty?");
        ret = -1; goto finally;
    }

    cls_dload_t _1={0}; dload =&_1;
    dload->this = this;
    dload->rootUrl = this->url;
    dload->archiveFile = this->file;
    dload->curl = curl_easy_init();
    if( ! dload->curl ){
        LOG_ERROR("%s\n", "curl_easy_init() -> NULL");
        ret = -1; goto finally;
    }

    err = gateleenResclone_download( dload , NULL , NULL );
    if( err ){ ret=-1; goto finally; }

    if( dload->dstArchive && archive_write_close(dload->dstArchive) ){
        LOG_ERROR("%s%s%ld%s%s\n", __func__, "(): archive_write_close failed (code ", err, "): ", archive_error_string(dload->dstArchive));
        ret = -1; goto finally;
    }

    finally:
    if( dload ){
        curl_easy_cleanup( dload->curl );
        archive_entry_free( dload->tmpEntry ); dload->tmpEntry = NULL;
        archive_write_free( dload->dstArchive ); dload->dstArchive = NULL;
    }
    return ret;
}


static ssize_t
push( this_t*this )
{
    ssize_t err, ret = 0;
    cls_upload_t *upload = NULL;

    cls_upload_t _1={0}; upload =&_1;
    upload->this = this;
    upload->archiveFile = this->file;
    upload->rootUrl = this->url;
    upload->curl = curl_easy_init();
    if( ! upload->curl ){
        LOG_ERROR("%s\n", "curl_easy_init() -> NULL" );
        ret = -1; goto finally;
    }

    err = readArchive( upload );
    if( err ){ ret=-1; goto finally; }

    finally:
    if( upload ){
        curl_easy_cleanup( upload->curl );
        archive_read_free( upload->srcArchive );
    }
    return ret;
}


ssize_t
gateleenResclone_run( int argc , char**argv )
{
    ssize_t err, ret=0;
    this_t *this = NULL;

    this = this_alloc();
    if( this==NULL ){ ret=-1; goto finally; }

    err = parseArgs( argc, argv, &this->mode, &this->url, &this->filter, &this->filter_len,
        &this->isFilterFull, &this->file );
    if( err ){ ret=-1; goto finally; }

    if( this->mode == MODE_FETCH ){
        ret = pull(this); goto finally;
    }else if( this->mode == MODE_PUSH ){
        ret = push(this); goto finally;
    }else{
        ret = -1; goto finally;
    }

    finally:
    parseArgs( -1, argv, &this->mode, &this->url, &this->filter, &this->filter_len,
        &this->isFilterFull, &this->file );
    this->mode=MODE_NULL; this->url=NULL; this->file=NULL;
    this_free( this );
    return ret;
}


int
gateleenResclone_main( int argc , char**argv )
{
    int ret;
    ret = gateleenResclone_run( argc , argv );
    return ret<0||ret>127 ? 1 : ret;
}


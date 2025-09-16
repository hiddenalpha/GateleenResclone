
#include "gateleen_resclone.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

#define FLG_isDstClosed (1<<0)


#define HttpClientReqImpl_mAGIC 0x727D3347
#define DEFINE_HttpClientReqImpl(D, S) struct HttpClientReqImpl*const D=(void*)S;do{ \
	assert(D); assert(D->mAGIC == HttpClientReqImpl_mAGIC); }while(0)
typedef struct HttpClientReqImpl {
	unsigned mAGIC;
	int flg;
	struct HttpClientReq *vt;
	struct Qntan_Mallocator **mallocator;
	CURL *curl;
	struct curl_slist *reqHdrs;
	char hdrsBuf[1024];
	/**/
	char *reqBody; size_t reqBody_cap,  reqBody_beg, reqBody_end;
	/**/
	void (*awaitResponseCompleteFn)(int,CLOSURE);  CLOSURE awaitResponseCompleteArg;
	/**/
	void (*onRspHdr)(CLOSURE,char*,int,char*,struct HttpClientReq_Hdr*,int);
	void (*onRspBodyChunk)( CLOSURE, char*buf, int len, int flgs );
	CLOSURE cbCls;
	/**/
} HttpClientReqImpl;


static size_t
onCurlDataComesIn( char*buf, size_t sz, size_t cnt, void*this_ ){
	register int err;
	DEFINE_HttpClientReqImpl(this, this_);
	long rspCode;
	err = curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &rspCode);
	if( this->onRspHdr ){
		this->onRspHdr(this->cbCls,
			"TODO_IoryqQnVgddtZvte", rspCode, "TODO_KmRkKGPmBNdq2YiA", NULL, 0);
	}
	if( this->onRspBodyChunk && sz > 0 && cnt > 0 ){
		assert(1.00001f*sz*cnt < INT_MAX);
		this->onRspBodyChunk(this->cbCls, buf, sz * cnt, 0);
	}
	return sz * cnt;
}


static void
Resclone_HttpClientReq_write(
	struct HttpClientReq**_,
	char const*buf,  int len,  int flgs,
	void (*onDone)(int ret,CLOSURE),  CLOSURE onDoneArg
){
	LOGT("[TRACE] @ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
	assert(len >= 0);  assert(flgs == 0 || flgs == 4);
	register int err;
	DEFINE_HttpClientReqImpl(this, container_of(_, HttpClientReqImpl, vt));
	if( (len > 0 || flgs & 4) && !this->curl ){
		assert(!this->curl);
	}
	if( len > 0 ){
		assert(buf);
		if( this->reqBody_cap < this->reqBody_end - this->reqBody_beg + len ){
			size_t const oldLen = this->reqBody_end - this->reqBody_beg;
			size_t newSz = oldLen + len + 3072;
			void *tmp = FN_Mallocator_realloc(
				this->mallocator, this->reqBody, this->reqBody_cap, newSz);
			if( !tmp ){
				assert(errno > 0); onDone(-errno, onDoneArg); return; }
			this->reqBody = tmp;
			this->reqBody_cap = newSz;
		}
		memcpy(this->reqBody + this->reqBody_end, buf, len);
		this->reqBody_end += len;
	}
	if( flgs & 4 ){
		assert(!(this->flg & FLG_isDstClosed));
		this->flg |= FLG_isDstClosed;
		err = curl_easy_perform(this->curl);
		if( err != CURLE_OK ){
			LOGE("curl_easy_perform(): %s\n\t@ %s:%d (%s)\n",
				curl_easy_strerror(err), __FILE__, __LINE__, __func__);
			onDone(-EIO, onDoneArg);
			return;
		}
		if( this->onRspBodyChunk )
			this->onRspBodyChunk(this->cbCls, NULL, 0, 4);
	}
	onDone(len, onDoneArg);
	if( this->awaitResponseCompleteFn ){
		void (*fn)(int,CLOSURE) = this->awaitResponseCompleteFn;
		this->awaitResponseCompleteFn = NULL;
		fn(0, this->awaitResponseCompleteArg);
	}
}


static size_t
onUploadChunkRequested( char*buf, size_t sz, REGISTER size_t cnt, void*_ ){
	DEFINE_HttpClientReqImpl(this, _);
	assert(1.000001f * sz * cnt < SIZE_MAX);
	cnt = MIN(sz * cnt, this->reqBody_end - this->reqBody_beg);
	memcpy(buf, this->reqBody, cnt);
	this->reqBody_beg += cnt;
	return cnt;
}


static void
Resclone_HttpClientReq_awaitResponseComplete(
	struct HttpClientReq**_, void(*onDone)(int,CLOSURE), CLOSURE onDoneArg
){
	DEFINE_HttpClientReqImpl(this, container_of(_, HttpClientReqImpl, vt));
	assert(!this->awaitResponseCompleteFn);
	if( this->flg & FLG_isDstClosed ){
		onDone(0, onDoneArg);
	}else{
		this->awaitResponseCompleteFn = onDone;
		this->awaitResponseCompleteArg = onDoneArg;
	}
}


static void
Resclone_HttpClientReq_pause( struct HttpClientReq** ){
	assert(!"TODO_ynY4jn8Jco7jElIe ENOTSUP");
}


static void
Resclone_HttpClientReq_resume( struct HttpClientReq** ){
	assert(!"TODO_o4Di88GquzdA29ig ENOTSUP");
}


static void
Resclone_HttpClientReq_unref( struct HttpClientReq**_ ){
	LOGT("[TRACE] @ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
	DEFINE_HttpClientReqImpl(this, container_of(_, HttpClientReqImpl, vt));
	this->mAGIC = 0;
	if( this->reqHdrs ){
		curl_slist_free_all(this->reqHdrs);  this->reqHdrs = NULL;
	}
	/* TODO, there's only ONE global 'curl' instance for now. MUST NOT
	 * free yet, but MUST somehow. */
	//curl_easy_cleanup(this->curl);
	FN_Mallocator_realloc(this->mallocator, this, sizeof*this, 0);
}


struct HttpClientReq**
newHttpClientReq( struct HttpClientReq_Opts*opts ){
	LOGT("[TRACE] @ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
	assert(opts);
	assert(opts->deps);
	assert(opts->deps->mallocator);
	assert(opts->mthd);
	static struct HttpClientReq vtRescloneHttpClientReqImpl = {
		.write = Resclone_HttpClientReq_write,
		.awaitResponseComplete = Resclone_HttpClientReq_awaitResponseComplete,
		.pause = Resclone_HttpClientReq_pause,
		.resume = Resclone_HttpClientReq_resume,
	}; assert(4*sizeof(void*) == sizeof vtRescloneHttpClientReqImpl);
	REGISTER int err;
	HttpClientReqImpl*const this = FN_Mallocator_realloc(opts->deps->mallocator, NULL, 0, sizeof*this);
	if( !this )
		goto fail01;
	*this = (struct HttpClientReqImpl){
		.mAGIC = 0x727D3347,
		.vt = &vtRescloneHttpClientReqImpl,
		.mallocator = opts->deps->mallocator,
		.cbCls = opts->cls,
		.onRspHdr = opts->onRspHdr,
		.onRspBodyChunk = opts->onRspBodyChunk,
	};
	{
		static CURL *theOnlyCurl = NULL; /* <- TODO: global-state is EVIL! */
		if( !theOnlyCurl )
			theOnlyCurl = curl_easy_init();
		this->curl = theOnlyCurl;
	}
	if( !this->curl ){
		LOGE("ERROR: curl_easy_init() -> NULL\n\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
		goto fail02;
	}
	int const isSlashNeeded = (opts->url[0] != '/');
	char url[2048];
	err = snprintf(url, sizeof url, "http%s://%s:%d%s%s",
		opts->useTls?"s":"", opts->host, opts->port, isSlashNeeded?"/":"", opts->url);
	if( err >= (int)sizeof url ){
		LOGE("ENOBUFS: url\n\t@ %s:%d (%s)\n", __FILE__, __LINE__, __func__);
		goto fail02;
	}{
		assert(!this->reqHdrs);
		char *it = this->hdrsBuf, *end = this->hdrsBuf + sizeof this->hdrsBuf;
		for( err = 0 ; err < opts->hdrs_cnt ; ++err ){
			#define HDR (opts->hdrs + err)
			int const key_len = strlen(HDR->key);
			int const val_len = strlen(HDR->val);
			char const*const hdr_beg = it;
			assert(it + key_len + val_len + 3 < end); /*TODO*/
			memcpy(it, HDR->key, key_len); it += key_len;
			memcpy(it, ": ", 2); it += 2;
			memcpy(it, HDR->val, val_len); it += val_len;
			*it++ = '\0';
			this->reqHdrs = curl_slist_append(this->reqHdrs, hdr_beg);
			#undef HDR
		}
	}{
		if( CURLE_OK != (err=curl_easy_setopt(this->curl, CURLOPT_URL, url))
		||  CURLE_OK != (err=curl_easy_setopt(this->curl, CURLOPT_HTTPHEADER, this->reqHdrs))
		||  CURLE_OK != (err=curl_easy_setopt(this->curl, CURLOPT_FOLLOWLOCATION, 0L))
		||  CURLE_OK != (err=curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, onCurlDataComesIn))
		||  CURLE_OK != (err=curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, this))
		){
			LOGE("curl_easy_setopt(): %s\n\t@ %s:%d (%s)\n",
				curl_easy_strerror(err), __FILE__, __LINE__, __func__);
			goto fail02;
		}
	}
	if( strcmp(opts->mthd, "GET") ){
		int const nok = 0
			|| CURLE_OK != (err=curl_easy_setopt(this->curl, CURLOPT_UPLOAD, 1L))
			|| CURLE_OK != (err=curl_easy_setopt(this->curl, CURLOPT_READFUNCTION, onUploadChunkRequested))
			|| CURLE_OK != (err=curl_easy_setopt(this->curl, CURLOPT_READDATA, this))
			;
		if( nok ){
			LOGE("curl_easy_setopt(): %s\n\t@ %s:%d (%s)\n",
				curl_easy_strerror(err), __FILE__, __LINE__, __func__);
			goto fail02;
		}
	}
	/**/
	opts->unref = Resclone_HttpClientReq_unref;
	return &this->vt;
fail02:
	Resclone_HttpClientReq_unref(&this->vt);
fail01:
	return NULL;
}




#include "gateleen_resclone.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archive.h"
#include "archive_entry.h"

#define FLG_closeRetvalIsAvail (1<<0)
#define FLG_writeRetvalIsAvail (1<<1)

#define COND  pthread_cond_t
#define COND_BROADCAST  pthread_cond_broadcast
#define COND_INIT  pthread_cond_init
#define COND_WAIT  pthread_cond_wait
#define MUTX  pthread_mutex_t
#define MUTX_INIT  pthread_mutex_init
#define MUTX_LOCK  pthread_mutex_lock
#define MUTX_UNLOCK  pthread_mutex_unlock

#define THIS_TarDec(CLS) TarDec*this = container_of(CLS, TarDec, pimpl); \
	assert(CLS != NULL); \
	assert(this->mAGIC == TarDecByLibarchive_mAGIC); \

#define THIS_TarEnc(CLS) TarEnc*this = container_of(CLS, TarEnc, pimpl); \
	assert(CLS); \
	assert(this->mAGIC == TarEncByLibarchive_mAGIC); \


typedef  struct TarEncByLibarchive  TarEnc;
typedef  struct TarDecByLibarchive  TarDec;


struct Cls476A9D42/* TarEnc_write() */{
	unsigned mAGIC;
	int coroState;
	int err;
	TarEnc *this;
	void *buf;
	int buf_len;
	void (*onDone)(int,CLOSURE);
	CLOSURE onDoneArg;
};


struct Cls50735D7E/* TarEnc_closeSnk() */{
	unsigned mAGIC;
	int err;
	TarEnc *this;
	void (*onDone)(int,CLOSURE);
	CLOSURE onDoneArg;
};


struct ClsC04C3362/* TarEnc_nextEntry() */{
	unsigned mAGIC;
	int err;
	TarEnc *this;
	void (*onDone)(int,CLOSURE);
	CLOSURE onDoneArg;
};


#define TarDecByLibarchive_mAGIC 0xDBC5972F
struct TarDecByLibarchive {
	unsigned mAGIC;
	struct archive *archive;
	struct archive_entry *currEntry;
	/**/
	struct Qntan_TarDec *pimpl;
	struct Qntan_Mallocator **mallocator;
};


#define TarEncByLibarchive_mAGIC 0xDBC5972F
struct TarEncByLibarchive {
	unsigned mAGIC;
	int flg;
	int writeRetval, closeRetval;
	uint_least64_t remainBodyLen;
	struct archive *archive;
	struct archive_entry *entry;
	MUTX mutx;
	COND condChunkRetval;
	struct Qntan_TarEnc *pimpl;
	/**/
	struct Garbage_Env **env;
	struct Qntan_Mallocator **mallocator;
	struct Qntan_Executor **ioWorker;
	/**/
	union {
		struct Cls476A9D42 cls476A9D42;
		struct Cls50735D7E cls50735D7E;
		struct ClsC04C3362 clsC04C3362;
	};
	/**/
	void(*onChunk)(CLOSURE,const char*,int,int,void(*)(int,CLOSURE),CLOSURE);
	CLOSURE onChunkArg;
};


static void
nextLibarchiveHdr(
	struct Qntan_TarDec**cls_,
	void(*onDone)(int,struct Qntan_TarDec_Hdr*,CLOSURE),
	CLOSURE onDoneArg
){
	int err;
	THIS_TarDec(cls_);

	err = archive_read_next_header(this->archive, &this->currEntry);
	if( err == ARCHIVE_FATAL ){
		LOGW("%s: archive_read_next_header(): %s\n\t@ %s:%d\n",
			strerrname(archive_errno(this->archive)), archive_error_string(this->archive),
			__FILE__, __LINE__);
		onDone(archive_errno(this->archive), NULL, onDoneArg);
		return;
	}
	if( err == ARCHIVE_WARN ){
		LOGW("%s: archive_read_next_header(): %s\n\t@ %s:%d\n",
			strerrname(archive_errno(this->archive)), archive_error_string(this->archive),
			__FILE__, __LINE__);
		/* keep going */
	}
	if( err == ARCHIVE_OK || err == ARCHIVE_WARN ){
		char const*name = archive_entry_pathname(this->currEntry);
		int ftype = archive_entry_filetype(this->currEntry);
		struct Qntan_TarDec_Hdr hdr = {
			.path = name,
			.path_len = strlen(name),
			.nBodyOctets = archive_entry_size(this->currEntry),
			.filetype = 0 ? 0
				: (ftype == AE_IFDIR) ? 'd'
				: (ftype == AE_IFREG) ? '0'
				: 0,
		};
		onDone(1, &hdr, onDoneArg);
		return;
	}
	if( err == ARCHIVE_EOF ){
		onDone(0, NULL, onDoneArg);
		return;
	}
	assert(!"unreachable");
}


static void
f5EOnzAu082WtWmRB(
	struct Qntan_TarDec**cls_,
	void*buf,
	int buf_cap,
	void(*onDone)(void*,int,int,CLOSURE),
	CLOSURE onDoneArg
){
	int err;
	THIS_TarDec(cls_);
	err = archive_read_data(this->archive, buf, buf_cap);
	if( err < 0 ){
		err = -archive_errno(this->archive);  assert(err < 0);
		LOGW("%s: archive_read_data(): %s\n\t@ %s:%d\n",
			strerrname(-err), archive_error_string(this->archive),
			__FILE__, __LINE__);
		onDone(buf, -err, 0, onDoneArg);
		return;
	}
	int const flgs = (err == 0) ? 4 : 0;
	onDone(buf, err, flgs, onDoneArg);
}


void
delTarDec( struct Qntan_TarDec**cls_ ){
	THIS_TarDec(cls_);
	if( this->currEntry ){ /*TODO*/ }
	if( this->archive ){ /*TODO*/ }
	FN_Mallocator_realloc(this->mallocator, this, sizeof*this, 0);
}


struct Qntan_TarDec**
newTarDec( struct EnvAndDeps*deps, char const*archivePath )
{
	int err;
	static struct Qntan_TarDec vt = {
		.nextHdr = nextLibarchiveHdr,
		.readBody = f5EOnzAu082WtWmRB,
	};
	assert(2*sizeof(void*) == sizeof vt);
	TarDec*const this = FN_Mallocator_realloc(deps->mallocator, NULL, 0, sizeof*this);
	if( !this ){ return NULL; }
	*this = (TarDec){
		.mAGIC = TarDecByLibarchive_mAGIC,
		.pimpl = &vt,
		.mallocator = deps->mallocator,
		.archive = archive_read_new(),
	};
	if( !this->archive ){
		LOGE("NULL: archive_read_new()\n\t@ %s:%d\n", __FILE__, __LINE__);
		goto endFn;
	}
	err = archive_read_support_format_all(this->archive)
	   || archive_read_open_filename(this->archive, archivePath, 1<<14)
	   ;
	if( err ){
		LOGE("libarchive: %d: %s\n\t@ %s:%d\n", err, archive_error_string(this->archive),
			__FILE__, __LINE__);
		err = -1; goto endFn;
	}

	return &this->pimpl;

endFn:
	assert(this);
	delTarDec(&this->pimpl);
	return NULL;
}






static void
fF0H4lmzEVJgxuvFv( CLOSURE cls_ ){
	struct ClsC04C3362*const cls = (void*)cls_;  assert(cls->mAGIC == 0xC04C3362);
	cls->mAGIC = 0;
	cls->onDone(cls->err, cls->onDoneArg);
}
static void
fazUoq6l4CA1YQBUo( CLOSURE cls_ ){
	struct ClsC04C3362*const cls = (void*)cls_;  assert(cls->mAGIC == 0xC04C3362);
	cls->err = archive_write_header(cls->this->archive, cls->this->entry);
	if( cls->err == ARCHIVE_RETRY || cls->err == ARCHIVE_FATAL ){
		LOGE("%s: archive_write_header(): %s\n\t@ %s:%d\n",
			strerrname(archive_errno(cls->this->archive)),
			archive_error_string(cls->this->archive),
			__FILE__, __LINE__);
		cls->err = archive_errno(cls->this->archive);
	}else if( cls->err == ARCHIVE_WARN ){
		LOGW("%s: archive_write_header(): %s\n\t@ %s:%d\n",
			strerrname(archive_errno(cls->this->archive)),
			archive_error_string(cls->this->archive),
			__FILE__, __LINE__);
		cls->err = 0;
	}else{
		assert(cls->err == ARCHIVE_OK);
	}
	(*cls->this->env)->enque(cls->this->env, fF0H4lmzEVJgxuvFv, QNTAN_CLS(cls));
	(*cls->this->env)->delAwaitToken(cls->this->env);
}
static void
TarEnc_nextEntry(
	struct Qntan_TarEnc**cls_,
	struct Qntan_TarEnc_Hdr*hdr,
	void(*onDone)(int,CLOSURE),
	CLOSURE onDoneArg
){
	THIS_TarEnc(cls_);
	if( !this->entry ){
		this->entry = archive_entry_new();
	}else{
		archive_entry_clear(this->entry);
	}
	archive_entry_set_pathname(this->entry, hdr->path);
	archive_entry_set_filetype(this->entry, AE_IFREG);
	archive_entry_set_size(this->entry, hdr->nBodyOctets);
	archive_entry_set_perm(this->entry, 0644);
	archive_entry_set_mtime(this->entry, hdr->mTimeEpchSec, 0);
	struct ClsC04C3362*cls = &this->clsC04C3362;
	assert(cls->mAGIC == 0);
	*cls = (struct ClsC04C3362){
		.mAGIC = 0xC04C3362,
		.this = this,
		.onDone = onDone,
		.onDoneArg = onDoneArg,
	};
	this->remainBodyLen = hdr->nBodyOctets;
	(*this->env)->addAwaitToken(this->env);
	(*this->ioWorker)->enque(this->ioWorker, fazUoq6l4CA1YQBUo, QNTAN_CLS(cls));
}


static void
f3vckCr8KLx6dKhmu( CLOSURE cls_ ){
	struct Cls476A9D42*const cls = (void*)cls_; assert(cls->mAGIC == 0x476A9D42);
	#define CORO_STATE cls->coroState
	enum { begin=0, s4KyZrpe041PJDBvP, sMzwUMyec9mFefNig, };
	switch( CORO_STATE ){case begin:{
		/* switch to another thread. */
		CORO_STATE = s4KyZrpe041PJDBvP;
		(*cls->this->env)->addAwaitToken(cls->this->env);
		(*cls->this->ioWorker)->enque(cls->this->ioWorker, f3vckCr8KLx6dKhmu, cls_);
		return;
	}case s4KyZrpe041PJDBvP:{
		/* now happily do blocking-IO on ioWorker thread. */
		cls->err = archive_write_data(cls->this->archive, cls->buf, cls->buf_len);
		if( cls->err < 0 ){
			cls->err = archive_errno(cls->this->archive);
			LOGD("%s: archive_write_data(): %s\n\t@ %s:%d\n", strerrname(-cls->err),
				archive_error_string(cls->this->archive), __FILE__, __LINE__);
		}
		/* done with blocking stuff. Switch bach to EvLoop thread */
		CORO_STATE = sMzwUMyec9mFefNig;
		(*cls->this->env)->enque(cls->this->env, f3vckCr8KLx6dKhmu, QNTAN_CLS(cls));
		(*cls->this->env)->delAwaitToken(cls->this->env);
		return;
	}case sMzwUMyec9mFefNig:{
		/* welcome back on EvLoop thread. Ready to call back. */
		cls->mAGIC = 0;
		cls->onDone(cls->err, cls->onDoneArg);
		return;
	}}
	LOGD("assert(s != %d)\n\t@ %s:%d (%s)\n", CORO_STATE, __FILE__, __LINE__, __func__); assert(0);
	#undef CORO_STATE
}
static void
TarEnc_write(
	struct Qntan_TarEnc**cls_,
	void*buf,
	int buf_len, int flgs,
	void(*onDone)(int,CLOSURE),
	CLOSURE onDoneArg
){
	THIS_TarEnc(cls_);
	assert(flgs == 0);
	assert(buf_len >= 0);
	if( (uint_least64_t)buf_len > this->remainBodyLen ){
		LOGD("assert(%d <= %lu)\n\t@ %s:%d\n",
			buf_len, FUCKWINDOOFLONG this->remainBodyLen, __FILE__, __LINE__);
		abort();
	}
	struct Cls476A9D42*const cls = &this->cls476A9D42;
	assert(cls->mAGIC == 0);
	*cls = (struct Cls476A9D42){
		.mAGIC = 0x476A9D42,
		.this = this,
		.buf = buf,
		.buf_len = buf_len,
		.onDone = onDone,
		.onDoneArg = onDoneArg,
	};
	f3vckCr8KLx6dKhmu((CLOSURE)cls);
}


static void
fHFTMFjhZkOmtQoAq( CLOSURE cls_ ){
	struct Cls50735D7E*const cls = (void*)cls_; assert(cls->mAGIC == 0x50735D7E);
	cls->mAGIC = 0;
	cls->onDone(cls->err, cls->onDoneArg);
}
static void
fqRmD2uoLc5rOoCY1( CLOSURE cls_ ){
	struct Cls50735D7E*const cls = (void*)cls_; assert(cls->mAGIC == 0x50735D7E);
	cls->err = archive_write_close(cls->this->archive);
	if( cls->err != ARCHIVE_OK ){
		LOGD("%s: archive_write_close(): %s\n\t@ %s:%d\n",
			strerrname(archive_errno(cls->this->archive)),
			archive_error_string(cls->this->archive),
			__FILE__, __LINE__);
	}
	(*cls->this->env)->enque(cls->this->env, fHFTMFjhZkOmtQoAq, QNTAN_CLS(cls));
	(*cls->this->env)->delAwaitToken(cls->this->env);
}
static void
TarEnc_closeSnk(
	struct Qntan_TarEnc**cls_,
	void(*onDone)(int,CLOSURE),
	CLOSURE onDoneArg
){
	THIS_TarEnc(cls_);
	struct Cls50735D7E*const cls = &this->cls50735D7E;
	assert(cls->mAGIC == 0);
	*cls = (struct Cls50735D7E){
		.mAGIC = 0x50735D7E,
		.this = this,
		.onDone = onDone,
		.onDoneArg = onDoneArg,
	};
	(*this->env)->addAwaitToken(this->env);
	(*this->ioWorker)->enque(this->ioWorker, fqRmD2uoLc5rOoCY1, QNTAN_CLS(cls));
}


static char const* TarEnc_getLastErrorStr( struct Qntan_TarEnc**cls_ ){
	THIS_TarEnc(cls_);
	return archive_error_string(this->archive);
}


void delTarEnc( struct Qntan_TarEnc**cls_ ){
	THIS_TarEnc(cls_);
	/* TODO release more */
	if( this->archive ) archive_write_free(this->archive);
	FN_Mallocator_realloc(this->mallocator, this, sizeof*this, 0);
}


static int
onArchiveWrOpen( struct archive*_, void*cls_ ){
	(void)_; (void)cls_;
	/* TODO anything to do here? */
	return ARCHIVE_OK;
}


static void fJahUpiznu1s0g8bE( int err, CLOSURE cls_ ){
	THIS_TarEnc(cls_);
	MUTX_LOCK(&this->mutx);
	this->writeRetval = err;
	this->flg |= FLG_writeRetvalIsAvail;
	COND_BROADCAST(&this->condChunkRetval);
	MUTX_UNLOCK(&this->mutx);
}


static la_ssize_t
onArchiveWrWrite( struct archive*_, void*cls_, void const*buf, size_t buf_len ){
	(void)_;
	THIS_TarEnc(cls_);
	this->flg &= ~FLG_writeRetvalIsAvail;
	this->onChunk(this->onChunkArg, buf, buf_len, 0, fJahUpiznu1s0g8bE, (CLOSURE)cls_);
	MUTX_LOCK(&this->mutx);
	while(!( this->flg & FLG_writeRetvalIsAvail )){
		COND_WAIT(&this->condChunkRetval, &this->mutx);
	}
	MUTX_UNLOCK(&this->mutx);
	return this->writeRetval;
}

static void
f1MIepBCyqxIfQcUf( int err, CLOSURE cls_ ){
	THIS_TarEnc(cls_);
	MUTX_LOCK(&this->mutx);
	this->closeRetval = err;
	this->flg |= FLG_closeRetvalIsAvail;
	COND_BROADCAST(&this->condChunkRetval);
	MUTX_UNLOCK(&this->mutx);
}
static int
onArchiveWrClose( struct archive*_, void*cls_ ){
	(void)_;
	LOGT("[TRACE] %s()\n", __func__);
	THIS_TarEnc(cls_);
	this->flg &= ~FLG_closeRetvalIsAvail;
	this->onChunk(this->onChunkArg, NULL, 0, 4, f1MIepBCyqxIfQcUf, (CLOSURE)cls_);
	MUTX_LOCK(&this->mutx);
	while(!( this->flg & FLG_closeRetvalIsAvail ))
		COND_WAIT(&this->condChunkRetval, &this->mutx);
	MUTX_UNLOCK(&this->mutx);
	return this->closeRetval;
}


struct Qntan_TarEnc**
newTarEnc(
	struct EnvAndDeps*deps,
	void(*onChunk)(CLOSURE,const char*,int,int,void(*)(int,CLOSURE b),CLOSURE b),
	CLOSURE onChunkArg
){
	assert(deps->mallocator);
	assert(deps->ioWorker);
	assert(onChunk);
	int err;
	static struct Qntan_TarEnc vt = {
		.nextEntry = TarEnc_nextEntry,
		.write = TarEnc_write,
		.getLastErrorStr = TarEnc_getLastErrorStr,
	};
	assert(4*sizeof(void*) == sizeof vt);
	TarEnc*const this = FN_Mallocator_realloc(deps->mallocator, NULL, 0, sizeof*this);
	if( !this ){ return NULL; }
	*this = (TarEnc){
		.mAGIC = TarEncByLibarchive_mAGIC,
		.pimpl = &vt,
		.mallocator = deps->mallocator,
		.env = deps->env,
		.ioWorker = deps->ioWorker,
		.archive = archive_write_new(),
		.onChunk = onChunk,
		.onChunkArg = onChunkArg,
	};
	if( !this->archive ){ goto fail; }
	err = MUTX_INIT(&this->mutx, NULL);  assert(!err); /*TODO free*/
	err = COND_INIT(&this->condChunkRetval, NULL);  assert(!err); /*TODO free*/
	err =  archive_write_set_format_pax_restricted(this->archive)
		|| archive_write_open(this->archive, &this->pimpl,
			onArchiveWrOpen, onArchiveWrWrite, onArchiveWrClose);
	if( err ){
		LOGE("libarchive: %s\n\t@ %s:%d\n", archive_error_string(this->archive), __FILE__, __LINE__);
		goto fail;
	}
	return &this->pimpl;
fail:
	assert(this);
	delTarEnc(&this->pimpl);
	return NULL;
}



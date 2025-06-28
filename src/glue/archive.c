
#include "gateleen_resclone.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "archive.h"
#include "archive_entry.h"


#define THIS_PTR(CLS) TarDec*this = container_of(CLS, TarDec, pimpl); \
	assert(CLS != NULL); \
	assert(this->mAGIC == TarDecByLibarchive_mAGIC); \


typedef  struct TarDecByLibarchive  TarDec;


#define TarDecByLibarchive_mAGIC 0xDBC5972F
struct TarDecByLibarchive {
	unsigned mAGIC;
	struct archive *archive;
	struct archive_entry *currEntry;
	/**/
	struct Garbage_TarDec *pimpl;
	struct Garbage_Mallocator **mallocator;
};


static void
nextLibarchiveHdr(
	struct Garbage_TarDec**cls_,
	void(*onDone)(int,struct Garbage_TarDecHdr*,void*),
	void*onDoneArg
){
	int err;
	THIS_PTR(cls_);

	err = archive_read_next_header(this->archive, &this->currEntry);
	if( err == ARCHIVE_OK ){
		char const*name = archive_entry_pathname(this->currEntry);
		int ftype = archive_entry_filetype(this->currEntry);
		struct Garbage_TarDecHdr hdr = {
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

	/* TODO I guess "no more entries"? But IMHO we should also check for other
	 * (maybe error) values? */

	onDone(0, NULL, onDoneArg);
}


static void
f5EOnzAu082WtWmRB(
	struct Garbage_TarDec**cls_,
	void*buf,
	int buf_cap,
	void(*onDone)(void*,int,int,void*),
	void*onDoneArg
){
	int err;
	THIS_PTR(cls_);
	err = archive_read_data(this->archive, buf, buf_cap);
	int flgs = (err == 0) ? 4 : 0;
	onDone(buf, err, flgs, onDoneArg);
}


void
delTarDec( struct Garbage_TarDec**cls_ ){
	THIS_PTR(cls_);
	if( this->currEntry ){ /*TODO*/ }
	if( this->archive ){ /*TODO*/ }
	(*this->mallocator)->reallocBlocking(this->mallocator, this, sizeof*this, 0);
}


struct Garbage_TarDec**
newTarDec( struct EnvAndDeps*deps, char const*archivePath )
{
	int err;
	static struct Garbage_TarDec vt = {
		.nextHdr = nextLibarchiveHdr,
		.readBody = f5EOnzAu082WtWmRB,
	};
	assert(2*sizeof(void*) == sizeof vt);
	TarDec*const this = (*deps->mallocator)->reallocBlocking(deps->mallocator, NULL, 0, sizeof*this);
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
		LOGE("libarchive: %s\n\t@ %s:%d\n", err, curl_easy_strerror(err), __FILE__, __LINE__);
		err = -1; goto endFn;
	}

	return &this->pimpl;

endFn:
	assert(this);
	delTarDec(&this->pimpl);
	return NULL;
}



# By using this work you agree to the terms and conditions in 'LICENSE.txt'

CC=gcc
LD=gcc
AR=ar
BINEXT=
LIBSEXT=.a
#WINSHITINCLUDE=-Imingw64-include
#WINSHITLIBS=-lpcre -lpcreposix

ifndef PROJECT_VERSION
	PROJECT_VERSION := $(shell git describe | sed 's;^v;;')
endif

CFLAGS= -Os --std=c99 -Wall -Wextra -Werror -fmax-errors=3 -DPROJECT_VERSION=$(PROJECT_VERSION) -Iinclude -Isrc/array -Isrc/common -Isrc/gateleen_resclone -Isrc/mime -Isrc/util_string -Isrc/util_term $(WINSHITINCLUDE)

LDFLAGS= -Wl,--fatal-warnings -Wl,-dn -lGateleenResclone -larchive -lcurl -lcJSON $(WINSHITLIBS) -Wl,-dy -Lbuild/lib

ARCH=$(shell $(CC) -v 2>&1 | egrep '^Target: ' | sed -E 's,^Target: +(.*)$$,\1,')

.SILENT:

default:
default: dist

link:
link: build/bin/gateleen-resclone$(BINEXT)

.PHONY: clean
clean:
	@echo "[INFO ] Clean"
	rm -rf build dist

compile:
compile: build/obj/array/array.o
compile: build/obj/common/commonbase.o
compile: build/obj/entrypoint/gateleenResclone.o
compile: build/obj/gateleen_resclone/gateleen_resclone.o
compile: build/obj/mime/mime.o
compile: build/obj/util_term/util_term.o

build/obj/%.o:
build/obj/%.o: src/%.c
	@echo "[INFO ] Compile '$@'"
	@mkdir -p $(shell dirname build/obj/$*)
	$(CC) -c -o $@ $< $(CFLAGS) \

build/bin/gateleen-resclone$(BINEXT):
build/bin/gateleen-resclone$(BINEXT): build/obj/entrypoint/gateleenResclone.o
build/bin/gateleen-resclone$(BINEXT): build/lib/libGateleenResclone$(LIBSEXT)
	@echo "[INFO ] Linking '$@'"
	@mkdir -p $(shell dirname $@)
	$(LD) -o $@ $^ $(LDFLAGS)

build/lib/libGateleenResclone$(LIBSEXT):
build/lib/libGateleenResclone$(LIBSEXT): build/obj/array/array.o
build/lib/libGateleenResclone$(LIBSEXT): build/obj/gateleen_resclone/gateleen_resclone.o
build/lib/libGateleenResclone$(LIBSEXT): build/obj/mime/mime.o
build/lib/libGateleenResclone$(LIBSEXT): build/obj/util_term/util_term.o
	@echo "[INFO ] Archive '$@'"
	@mkdir -p $(shell dirname $@)
	$(AR) -crs $@ $^

.PHONY: dist
dist: clean link
	@echo "[INFO ] Package"
	@mkdir -p build dist
	@rm -rf build/dist-*
	@echo
	@sh -c 'if test -n "`git status --porcelain`"; then echo "[ERROR] Worktree not clean as it should be (see: git status)"; exit 1; fi'
	# Source bundle.
	git archive --format=tar "--prefix=dist-src/" HEAD | tar -C build -x
	@echo
	rm -f build/dist-src/MANIFEST.INI
	echo "version=$(PROJECT_VERSION)"    >> build/dist-src/MANIFEST.INI
	echo "builtAt=$(shell date -Is)"     >> build/dist-src/MANIFEST.INI
	git log -n1 HEAD | sed -re "s,^,; ," >> build/dist-src/MANIFEST.INI
	@echo
	(cd build/dist-src && find . -type f -exec md5sum -b {} \;) > build/checksums.md5
	mv build/checksums.md5 build/dist-src/checksums.md5
	(cd build/dist-src && tar --owner=0 --group=0 -czf ../../dist/GateleenResclone-$(PROJECT_VERSION).tgz *)
	@echo
	@# Executable bundle.
	rm -rf   build/dist-bin && mkdir -p build/dist-bin
	mv -t build/dist-bin \
		build/dist-src/README* \
		build/dist-src/LICENSE* \
		build/dist-src/MANIFEST.INI
	mkdir build/dist-bin/bin
	cp -t build/dist-bin/bin \
		build/bin/gateleen-resclone$(BINEXT)
	(cd build/dist-bin && find . -type f -exec md5sum -b {} \;) > build/checksums.md5
	mv build/checksums.md5 build/dist-bin/checksums.md5
	(cd build/dist-bin && tar --owner=0 --group=0 -czf ../../dist/GateleenResclone-$(PROJECT_VERSION)-$(ARCH).tgz *)
	@echo "[INFO ] DONE: Artifacts created and placed in 'dist'."
	@echo
	@echo See './dist/' for result.

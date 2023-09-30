# By using this work you agree to the terms and conditions in 'LICENSE.txt'

CC ?=gcc
LD ?=ld
AR ?=ar
BINEXT ?=
LIBSEXT ?= .a
TOOLCHAIN ?= lxGcc64

ifndef PROJECT_VERSION
	PROJECT_VERSION := $(shell git describe | sed 's;^v;;')
endif

CFLAGS ?= --std=c99                                                           \
	-Wall -Wextra -Werror -fmax-errors=3                                      \
	-Wno-error=unused-function -Wno-error=unused-label -Wno-error=unused-variable \
	-Wno-error=unused-parameter -Wno-error=sign-compare                       \
	-Wno-error=unused-const-variable -Wno-error=pointer-sign                  \
	-Werror=implicit-fallthrough=1                                            \
	-Wno-error=unused-but-set-variable                                        \
	-Wno-unused-function -Wno-unused-parameter                                \
	-DPROJECT_VERSION=$(PROJECT_VERSION)

LDFLAGS ?= -Wl,--no-demangle,--fatal-warnings

ifndef NDEBUG
	CFLAGS := $(CFLAGS) -ggdb -O0 -g3
else
	CFLAGS := $(CFLAGS) -ffunction-sections -fdata-sections -Os "-DNDEBUG=1"
	LDFLAGS := $(LDFLAGS) -Wl,--gc-sections,--as-needed
endif

OBJFILES ?= $(shell find src -name "*\.c" | sed -re "s;^src/(.*).c$$;build/obj/\1.o;")

INCDIRS ?= -Iinclude  \
		   $(shell find src -type d | sed "s;^;-I;")  \
	       -Iexternal/$(TOOLCHAIN)/include

LIBSDIR ?= -Lexternal/$(TOOLCHAIN)/lib

ifeq ($(BINEXT),.exe)
	INCDIRS := $(INCDIRS) -Imingw64-include
	LPCRE       := -lpcre
	LPCREPOSIX  := -lpcreposix
endif

.SILENT:

default: dist

link:                                    \
	build/bin/gateleen-resclone$(BINEXT) \

.PHONY: clean
clean:
	@echo "\n[\033[34mINFO \033[0m] Clean"
	rm -rf build dist

compile: $(OBJFILES)

build/obj/%.o: src/%.c
	@echo "\n[\033[34mINFO \033[0m] Compile '$@'"
	@mkdir -p $(shell dirname build/obj/$*)
	$(CC) -c -o $@ $< $(CFLAGS) $(INCDIRS) \

build/bin/gateleen-resclone$(BINEXT): \
		build/obj/entrypoint/gateleenResclone.o \
		build/lib/libGateleenResclone$(LIBSEXT)
	@echo "\n[\033[34mINFO \033[0m] Linking '$@'"
	@mkdir -p $(shell dirname $@)
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBSDIR) \
		-Wl,-Bstatic \
		-larchive -lcurl -lcJSON $(LPCREPOSIX) $(LPCRE) \
		-Wl,-Bdynamic \

build/lib/libGateleenResclone$(LIBSEXT): \
		build/obj/array/array.o \
		build/obj/gateleen_resclone/gateleen_resclone.o \
		build/obj/mime/mime.o \
		build/obj/util_term/util_term.o
	@echo "\n[\033[34mINFO \033[0m] Archive '$@'"
	@mkdir -p $(shell dirname $@)
	$(AR) -crs $@ $^

.PHONY: dist
dist: clean link
	@echo "\n[\033[34mINFO \033[0m] Package"
	@mkdir -p build dist
	@rm -rf build/dist-*
	@echo
	@sh -c 'if test -n `git status --porcelain`; then echo "[ERROR] Worktree not clean as it should be (see: git status)"; exit 1; fi'
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
	mv -t build/dist-bin/bin \
		build/bin/gateleen-resclone$(BINEXT)
	(cd build/dist-bin && find . -type f -exec md5sum -b {} \;) > build/checksums.md5
	mv build/checksums.md5 build/dist-bin/checksums.md5
	(cd build/dist-bin && tar --owner=0 --group=0 -czf ../../dist/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN).tgz *)
	@echo "\n[\033[34mINFO \033[0m] DONE: Artifacts created and placed in 'dist'."
	@# Dependency Bundle.
	$(eval PCKROOT := build/dist-rt)
	@sh -c 'if test ".exe" = "$(BINEXT)"; then \
		rm -rf ./$(PCKROOT); \
		mkdir -p ./$(PCKROOT)/bin; \
		cp external/$(TOOLCHAIN)/rt/bin/libarchive-13.dll $(PCKROOT)/bin/; \
		cp external/$(TOOLCHAIN)/rt/bin/libcurl-4.dll $(PCKROOT)/bin/; \
		cp external/$(TOOLCHAIN)/rt/bin/libiconv-2.dll $(PCKROOT)/bin/; \
		cp external/$(TOOLCHAIN)/rt/bin/libpcreposix-0.dll $(PCKROOT)/bin/; \
		cp external/$(TOOLCHAIN)/rt/bin/libpcre-1.dll $(PCKROOT)/bin/; \
		cp external/$(TOOLCHAIN)/rt/bin/*pthread*.dll $(PCKROOT)/bin/libwinpthread-1.dll; \
		(cd $(PCKROOT) && find . -type f -exec md5sum -b {} \;) > build/checksums.md5; \
		mv build/checksums.md5 $(PCKROOT)/checksums.md5; \
		(cd build/dist-rt && tar --owner=0 --group=0 -czf ../../dist/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)-rt.tgz *); \
	fi'
	@echo
	@echo See './dist/' for result.


CC ?=gcc
LD ?=ld
BINEXT ?= .elf
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
	CFLAGS := $(CFLAGS) -ffunction-sections -fdata-sections -O2 "-DNDEBUG=1"
	LDFLAGS := $(LDFLAGS) -Wl,--gc-sections,--as-needed
endif

OBJFILES ?= $(shell find src -name "*\.c" | sed -re "s;^src/(.*).c$$;build/\1.o;")

INCDIRS ?= -Iinclude  \
		   $(shell find src -type d | sed "s;^;-I;")  \
	       -Iexternal/$(TOOLCHAIN)/include

LIBSDIR ?= -Lexternal/$(TOOLCHAIN)/lib

ifeq ($(BINEXT),.exe)
	INCDIRS := $(INCDIRS) -Imingw64-include
	LPCRE       := -lpcre
	LPCREPOSIX  := -lpcreposix
endif


default: dist

link:                                          \
	build/entrypoint/gateleenResclone$(BINEXT) \

.PHONY: clean
clean:
	@echo "\n[\033[34mINFO \033[0m] Clean"
	rm -rf build dist

compile: $(OBJFILES)

build/%.o: src/%.c
	@echo "\n[\033[34mINFO \033[0m] Compile '$@'"
	@mkdir -p $(shell dirname build/$*)
	$(CC) -c -o $@ $< $(CFLAGS) $(INCDIRS) \

build/entrypoint/gateleenResclone$(BINEXT): \
		build/array/array.o \
		build/entrypoint/gateleenResclone.o \
		build/gateleen_resclone/gateleen_resclone.o \
		build/log/log.o \
		build/mime/mime.o \
		build/util_term/util_term.o
	@echo "\n[\033[34mINFO \033[0m] Linking '$@'"
	@mkdir -p $(shell dirname $@)
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBSDIR) \
		-larchive -lcurl -lyajl $(LPCREPOSIX) $(LPCRE) \


.PHONY: dist
dist: clean link
	@echo "\n[\033[34mINFO \033[0m] Package"
	@mkdir -p build dist
	@rm -rf build/GateleenResclone-$(PROJECT_VERSION)
	@echo
	@bash -c 'if [[ -n `git status --porcelain` ]]; then echo "[ERROR] Worktree not clean as it should be (see: git status)"; exit 1; fi'
	# Source bundle.
	git archive --format=tar "--prefix=GateleenResclone-$(PROJECT_VERSION)/" HEAD | tar -C build -x
	@echo
	rm -f build/GateleenResclone-$(PROJECT_VERSION)/MANIFEST.INI
	echo "version=$(PROJECT_VERSION)"    >> build/GateleenResclone-$(PROJECT_VERSION)/MANIFEST.INI
	echo "builtAt=$(shell date -Is)"     >> build/GateleenResclone-$(PROJECT_VERSION)/MANIFEST.INI
	git log -n1 HEAD | sed -re "s,^,; ," >> build/GateleenResclone-$(PROJECT_VERSION)/MANIFEST.INI
	@echo
	(cd build/GateleenResclone-$(PROJECT_VERSION) && find . -type f -exec md5sum -b {} \;) > build/checksums.md5
	mv build/checksums.md5 build/GateleenResclone-$(PROJECT_VERSION)/checksums.md5
	tar --owner=0 --group=0 -czf dist/GateleenResclone-$(PROJECT_VERSION).tgz -C build GateleenResclone-$(PROJECT_VERSION)
	@echo
	@# Executable bundle.
	rm -rf   build/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)
	mkdir -p build/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)
	mv -t build/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN) \
		build/GateleenResclone-$(PROJECT_VERSION)/README* \
		build/GateleenResclone-$(PROJECT_VERSION)/MANIFEST.INI
	mkdir build/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)/bin
	mv -t build/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)/bin \
		$(shell find build -name "*$(BINEXT)")
	(cd build/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN) && find . -type f -exec md5sum -b {} \;) > build/checksums.md5
	mv build/checksums.md5 build/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)/checksums.md5
	tar --owner=0 --group=0 -czf dist/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN).tgz \
		-C build GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)
	@echo "\n[\033[34mINFO \033[0m] DONE: Artifacts created and placed in 'dist'."
	@# Dependency Bundle.
	$(eval PCKROOT := build/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)-rt)
	@bash -c 'if [ ".exe" = "$(BINEXT)" ]; then \
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
		tar --owner=0 --group=0 -czf dist/GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)-rt.tgz \
			-C build GateleenResclone-$(PROJECT_VERSION)-$(TOOLCHAIN)-rt; \
	fi'
	@echo
	@echo See './dist/' for result.


Showcase how to build and install
=================================

WARN: Do NOT perform any of these steps on your host system! This script
      MUST only be run on a system which is a
      just-throw-it-away-if-broken system.

Sometimes happy developers (like me) have no choice but using terribly
restricted systems where setting up tools to run even something as
trivial as configure/make/install becomes a nightmare if not impossible.
I found it to be very handy to have some independent qemu VM at hand
which lets me install whatever I need, neither with any special software
nor any annoying privileges on a host machine. Qemu runs portable and in
user mode even doesn't need any annoying permissions at all.



## Setup a minimal system in your qemu VM

This setup mainly targets debian. Nevertheless it tries to stay POSIX
compatible as possible. So setup a minimal install of your system of
choice and then as soon you've SSH access to a (posix) shell, you're
ready for the next step.

Still not sure which system to use? Link below provides some candidates.
HINT: Windows IMHO is a terrible choice. So stop complaining if you go
this route.

https://en.wikipedia.org/wiki/POSIX#POSIX-oriented_operating_systems



## Start VM with SSH access

Easiest way to work with your machine is via SSH. Therefore if you've
chosen to use a qemu VM, make sure you've setup and configured sshd
properly inside the VM. Then just pass args like those to qemu:

  --device e1000,netdev=n0 --netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22

Started this way, the SSHDaemon inside the VM is accessible from your
host via "localhost" at port "2222":

  ssh localhost -p2222

I strongly recommend using SSH paired with a well-designed
terminal-emulator in place of some fancy VGA/GUI emulators or similar.
Those commandline tools give immediate benefits like copy-pasting of
script snippets which becomes very handy with the scripts which follow,
or also with file transfers to get the final build result out the VM. My
condolence to users which still think windows is the only way to use a
computer.



## Configure build environment

Setup environ vars according to your chosen build system. Here are few examlpes:

### Config for debian
true \
  && PKGS_TO_ADD="git make gcc ca-certificates libc6-dev cmake autoconf automake libtool m4" \
  && PKGS_TO_DEL="cmake autoconf automake libtool m4" \
  && SUDO=sudo \
  && PKGINIT="$SUDO apt update" \
  && PKGADD="$SUDO apt install -y --no-install-recommends" \
  && PKGDEL="$SUDO apt purge -y" \
  && PKGCLEAN="$SUDO apt clean" \
  && HOST= \
  && true

### Config for alpine with mingw
true \
  && PKGS_TO_ADD="git make mingw-w64-gcc curl tar musl-dev cmake autoconf automake libtool m4" \
  && PKGS_TO_DEL="cmake autoconf automake libtool m4" \
  && SUDO="/home/$USER/.local/bin/sudo" \
  && PKGINIT=true \
  && PKGADD="$SUDO apk add" \
  && PKGDEL="$SUDO apk del" \
  && PKGCLEAN="$SUDO apk cache clean 2>&1| grep -v 'ERROR: Package cache is not enabled'" \
  && HOST=x86_64-w64-mingw32 \
  && true

### Setup build env
true \
  && GIT_TAG="master" \
  && LIBARCHIVE_VERSION="3.6.2" \
  && CURL_VERSION="8.3.0" \
  && CJSON_VERSION="1.7.15" \
  && PCRE_VERSION="8.45" \
  \
  && CACHE_DIR="/var/tmp" \
  && INSTALL_ROOT="/usr/${HOST:-local}" \
  && if test -n "$HOST"; then HOST_="${HOST:?}-" ;fi \
  && CJSON_URL="https://github.com/DaveGamble/cJSON/archive/refs/tags/v${CJSON_VERSION:?}.tar.gz" \
  && CJSON_SRCTGZ="${CACHE_DIR:?}/cJSON-${CJSON_VERSION:?}.tgz" \
  && CJSON_BINTGZ="${CJSON_SRCTGZ%.*}-bin.tgz" \
  && CURL_VERSION_UGLY="$(echo "$CURL_VERSION"|sed 's;\.;_;g')" \
  && CURL_URL="https://github.com/curl/curl/archive/refs/tags/curl-${CURL_VERSION_UGLY:?}.tar.gz" \
  && CURL_SRCTGZ="${CACHE_DIR:?}/curl-${CURL_VERSION:?}.tgz" \
  && CURL_BINTGZ="${CURL_SRCTGZ%.*}-bin.tgz" \
  && LIBARCHIVE_URL="https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION:?}/libarchive-${LIBARCHIVE_VERSION:?}.tar.gz" \
  && LIBARCHIVE_SRCTGZ="${CACHE_DIR:?}/libarchive-${LIBARCHIVE_VERSION:?}.tgz" \
  && LIBARCHIVE_BINTGZ="${LIBARCHIVE_SRCTGZ%.*}-bin.tgz" \
  && PCRE_URL="https://sourceforge.net/projects/pcre/files/pcre/${PCRE_VERSION:?}/pcre-${PCRE_VERSION:?}.tar.gz/download" \
  && PCRE_SRCTGZ="${CACHE_DIR:?}/pcre-${PCRE_VERSION:?}.tgz" \
  && PCRE_BINTGZ="${PCRE_SRCTGZ%.*}-bin.tgz" \
  \
  && ${PKGINIT:?} && ${PKGADD:?} $PKGS_TO_ADD \
  && printf '\n  Download Dependency Sources\n\n' \
  && if test ! -e "${CJSON_SRCTGZ:?}"; then true \
    && echo "Download ${CJSON_URL:?}" \
    && curl -sSLo "${CJSON_SRCTGZ:?}" "${CJSON_URL:?}" \
    ;fi \
  && if test ! -e "${LIBARCHIVE_SRCTGZ:?}"; then true \
    && echo "Download ${LIBARCHIVE_URL:?}" \
    && curl -sSLo "${LIBARCHIVE_SRCTGZ:?}" "${LIBARCHIVE_URL:?}" \
    ;fi \
  && if test ! -e "${CURL_SRCTGZ:?}"; then true \
     && echo "Download ${CURL_URL:?}" \
     && curl -sSLo "${CURL_SRCTGZ:?}" "${CURL_URL:?}" \
     ;fi \
  && if test ! -e "${PCRE_SRCTGZ:?}"; then true \
     && echo "Download ${PCRE_URL:?}" \
     && curl -sSLo "${PCRE_SRCTGZ:?}" "${PCRE_URL:?}" \
     ;fi \
  && if test ! -e "${CJSON_BINTGZ:?}"; then (true \
     && printf '\n  Build cJSON\n\n' \
     && cd /tmp \
     && tar xf "${CJSON_SRCTGZ:?}" \
     && cd "cJSON-${CJSON_VERSION:?}" \
     && mkdir build build/obj build/lib build/include \
     && CFLAGS="-Wall -pedantic -fPIC" \
     && ${HOST_}cc $CFLAGS -c -o build/obj/cJSON.o cJSON.c \
     && ${HOST_}cc $CFLAGS -shared -o build/lib/libcJSON.so.${CJSON_VERSION:?} build/obj/cJSON.o \
     && unset CFLAGS \
     && (cd build/lib \
        && MIN=${CJSON_VERSION%.*} && MAJ=${MIN%.*} \
        && ln -s libcJSON.so.${CJSON_VERSION:?} libcJSON.so.${MIN:?} \
        && ln -s libcJSON.so.${MIN:?} libcJSON.so.${MAJ} \
        ) \
     && ${HOST_}ar rcs build/lib/libcJSON.a build/obj/cJSON.o \
     && cp -t build/. LICENSE README.md \
     && cp -t build/include/. cJSON.h \
     && rm -rf build/obj \
     && (cd build && tar --owner=0 --group=0 -czf "${CJSON_BINTGZ:?}" *) \
     && cd /tmp \
     && rm -rf "cJSON-${CJSON_VERSION:?}" \
     && $SUDO tar -C "${INSTALL_ROOT:?}" -xzf "${CJSON_BINTGZ:?}" \
     );fi \
  && if test ! -e "${LIBARCHIVE_BINTGZ}"; then (true \
     && printf '\n  Build libarchive\n\n' \
     && tar xf "${LIBARCHIVE_SRCTGZ:?}" \
     && cd "libarchive-${LIBARCHIVE_VERSION:?}" \
     && ./configure --prefix="${PWD:?}/build/usr_local" --host=${HOST} \
            --enable-bsdtar=static --enable-bsdcat=static --enable-bsdcpio=static \
            --disable-rpath --enable-posix-regex-lib \
            CC=${HOST_}gcc \
            CPP=${HOST_}cpp \
     && make clean && make -j$(nproc) && make install \
     && (cd build/usr_local \
        && rm -rf lib/pkgconfig lib/libarchive.la \
        && tar --owner=0 --group=0 -czf "${LIBARCHIVE_BINTGZ:?}" * \
        && md5sum -b "${LIBARCHIVE_BINTGZ:?}" > "${LIBARCHIVE_BINTGZ:?}.md5"  \
        ) \
     && cd .. && rm -rf "libarchive-${LIBARCHIVE_VERSION:?}" \
     && $SUDO tar -C "${INSTALL_ROOT:?}" -xzf "${LIBARCHIVE_BINTGZ:?}" \
     );fi \
  && if test ! -e "${CURL_BINTGZ:?}"; then (true \
     && printf '\n  Build curl\n\n' \
     && cd /tmp \
     && tar xf "${CURL_SRCTGZ:?}" \
     && cd "curl-curl-${CURL_VERSION_UGLY:?}" \
     && autoreconf -fi \
     && if test -n "$HOST"; then HORSCHT="--host=${HOST:?}";fi \
     && ./configure --prefix="$PWD/build/usr_local" --enable-http --with-nghttp2 --with-nghttp3 --disable-alt-svc --disable-ares --disable-aws --disable-basic-auth --disable-bearer-auth --disable-bindlocal --disable-cookies --disable-curldebug --disable-dateparse --disable-debug --disable-dependency-tracking --disable-dict --disable-digest-auth --disable-dnsshuffle --disable-doh --disable-ech --disable-file --disable-form-api --disable-ftp --disable-get-easy-options --disable-gopher --disable-headers-api --disable-hsts --disable-http-auth --disable-imap --enable-ipv6 --disable-kerberos-auth --disable-largefile --disable-ldap --disable-ldaps --disable-libcurl-option --disable-libtool-lock --enable-manual --disable-mime --disable-mqtt --disable-negotiate-auth --disable-netrc --enable-ntlm --enable-ntlm-wb --disable-openssl-auto-load-config --disable-optimize --disable-pop3 --disable-progress-meter --enable-proxy --disable-pthreads --disable-rt --disable-rtsp --disable-smb --enable-smtp --disable-socketpair --disable-sspi --disable-symbol-hiding --disable-telnet --disable-tftp --disable-threaded-resolver --disable-tls-srp --disable-unix-sockets --disable-verbose --disable-versioned-symbols --disable-warnings --disable-websockets --disable-werror --without-schannel --without-secure-transport --without-amissl --without-ssl --without-openssl --without-gnutls --without-mbedtls --without-wolfssl --without-bearssl --without-rustls --without-test-nghttpx --without-test-caddy --without-test-httpd --without-pic --without-aix-soname --without-gnu-ld --without-sysroot --without-mingw1-deprecated --without-hyper --without-zlib --without-brotli --without-zstd --without-ldap-lib --without-lber-lib --without-gssapi-includes --without-gssapi-libs --without-gssapi --without-default-ssl-backend --without-random --without-ca-bundle --without-ca-path --without-ca-fallback --without-libpsl --without-libgsasl --without-librtmp --without-winidn --without-libidn2 --without-ngtcp2 --without-quiche --without-msh3 \
        --without-zsh-functions-dir --without-fish-functions-dir CFLAGS=-fPIC $HORSCHT \
     && make clean && make -j$(nproc) && make install \
     && (cd build/usr_local && rm -rf share/aclocal bin/curl-config lib/libcurl.la lib/pkgconfig) \
     && (cd build/usr_local && tar --owner=0 --group=0 -czf "${CURL_BINTGZ:?}" *) \
     && cd /tmp \
     && rm -rf "curl-curl-${CURL_VERSION_UGLY:?}" \
     && $SUDO tar -C "${INSTALL_ROOT:?}" -xzf "${CURL_BINTGZ:?}" \
     );fi \
  && if test -n "$HOST" -a ! -e "${PCRE_BINTGZ:?}"; then (true \
     && printf '\n  Build pcre\n\n' \
     && cd /tmp \
     && tar xf "${PCRE_SRCTGZ:?}" \
     && cd "pcre-${PCRE_VERSION:?}" \
     && ./configure --prefix="$PWD/build/usr_local" --host=${HOST:?} --disable-cpp --enable-utf \
     && make clean && make -j$(nproc) && make install \
     && (cd build/usr_local \
        && rm -rf lib/libpcre.la lib/pkgconfig lib/libpcreposix.la bin/pcre-config \
        && tar --owner=0 --group=0 -czf "${PCRE_BINTGZ:?}" * \
        ) \
     && cd /tmp \
     && rm -rf "pcre-${PCRE_VERSION:?}" \
     && $SUDO tar -C "${INSTALL_ROOT:?}" -xzf "${PCRE_BINTGZ:?}" \
     );fi \
  && ${PKGDEL:?} $PKGS_TO_DEL && ${PKGCLEAN:?} \
  && true

### Make
true \
  && WORKDIR="/home/${USER:?}/work" \
  \
  && printf '\n  Build GateleenResclone\n\n' \
  && mkdir -p "${WORKDIR:?}" && cd "${WORKDIR:?}" \
  && git clone --depth 42 --branch "${GIT_TAG:?}" https://github.com/hiddenalpha/GateleenResclone.git . \
  && git config advice.detachedHead false \
  && git checkout --detach "${GIT_TAG:?}" \
  && if echo $HOST|grep -q '\-mingw'; then true \
     && HORSCHT="TOOLCHAIN=mingw CC=$HOST-cc LD=$HOST-ld AR=$HOST-ar BINEXT=.exe LIBSEXT=.lib" \
     && tmpfile="$(mktemp "/tmp/XXXXXXXXXXXXXXXX")" \
     && mv Makefile "${tmpfile:?}" && cat "${tmpfile:?}" \
        | sed -E 's;^CFLAGS \?=(.*)?$;CFLAGS ?= -DPCRE_STATIC=1 -DCURL_STATICLIB=1 \1;' \
        | sed -E 's;-larchive;/usr/x86_64-w64-mingw32/lib/libarchive.a;' \
        | sed -E 's;(-Wl,-Bdynamic);\1 -lws2_32 -lbcrypt;' \
        | grep -v 'Worktree not clean as it should be' \
        > Makefile \
     && rm "${tmpfile:?}" \
     ;fi \
  && make -j8 clean $HORSCHT \
  && make -j8 -j$(nproc) $HORSCHT \
  && dirOfDistBundle="$(realpath dist)" \
  && printf '\n  SUCCESS  :)  Distribution bundle is ready in:\n\n  %s\n\n  Tip: Before pulling out your hair about how to get that archive out of\n       your qemu VM. STOP kluding around with silly tools and learn how\n       basic tools do the job perfectly fine. So go to your host and run:\n\n  ssh %s@localhost -p22 -T '\''true && cd "%s" && tar c *'\'' | tar x\n\n' "${dirOfDistBundle:?}" "$USER" "${dirOfDistBundle:?}" \
  && true



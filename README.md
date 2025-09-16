
Gateleen Resclone
=================

Commandline utility to clone subtrees from gateleen instances.

[Downloads](https://github.com/hiddenalpha/GateleenResclone/releases)



## Cited from `gateleen-resclone --help`:

```
--pull|--push
    Choose to download or upload.

--url <url>
    Root node of remote tree.

--filter-part <path-filter>
    Regex pattern applied as predicate to the path starting after
    the path specified in '--url'. Each path segment will be
    handled as its individual pattern. If there are longer paths to
    process, they will be accepted, as long they at least
    start-with specified filter.
    Example:  /foo/[0-9]+/bar

--filter-full <path-filter>
    Nearly same as '--filter-part'. But paths with more segments
    than the pattern, will be rejected.

--file <path.tar>
    (optional) Path to the archive file to read/write. Defaults to
    stdin/stdout if ommitted. Special value "-" means to use
    stdin/stdout, even a tty got detected.

--print-path
    Print path for every entry actually taken. This does NOT
    include intermediates like collections or those which responded
    non-200 codes.
```


## Stats For Nerds

```
github.com/AlDanial/cloc
---------------------------------------------
Language       files   blank   comment   code
---------------------------------------------
C                  5     134        58   1719
C/C++ Header       1     132        12     97
make               1      15         5     80
---------------------------------------------
SUM:               7     281        75   1896
---------------------------------------------
```


## Build

```
make clean
make
make install
```

Just in case you've no build machine at hand. I've uploaded my build
machines alongside the released artifacts. Just look out for "qcow2"
files alongside the artifacts.


## Dependencies

- libc
- libcJSON
- libcurl
- libpcre
- libgarbage


## Dockerimage?

Usually there's no need to have a dockerimage.
But anyway, for the insisting here a Dockerfile:

```Dockerfile
FROM docker.io/alpine:3.21.2
RUN true \
  && apk add gcompat \
  && wget -O- 'https://github.com/hiddenalpha/GateleenResclone/releases/download/v0.0.5/GateleenResclone-0.0.5+x86_64-linux-gnu.tgz' \
     | tar -C /usr -xz -- bin \
  && true
```


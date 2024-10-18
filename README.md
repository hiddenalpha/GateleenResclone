
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
    Regex pattern applied as predicate to the path starting after the path
    specified in '--url'. Each path segment will be handled as its
    individual pattern. If there are longer paths to process, they will be
    accepted, as long they at least start-with specified filter.
    Example:  /foo/[0-9]+/bar

--filter-full <path-filter>
    Nearly same as '--filter-part'. But paths with more segments than the
    pattern, will be rejected.

--file <path.tar>
    (optional) Path to the archive file to read/write. Defaults to
    stdin/stdout if ommitted.
```


## Stats For Nerds

```
github.com/AlDanial/cloc v 1.96  T=0.01 s (1133.6 files/s, 115203.6 lines/s)
-------------------------------------------------
Language        files    blank    comment    code
-------------------------------------------------
C                   6      146         79    1064
make                1       15          2     101
C/C++ Header        7       44         35      55
Markdown            1       24          0      53
Bourne Shell        1        2          5       1
-------------------------------------------------
SUM:               16      231        121    1274
-------------------------------------------------
```


## Build

```
./configure
make clean
make
make install
```

Just in case you've no build machine at hand. I've uploaded my qemu
build machines alongside the released artifacts. Just look out for
"qcow2" files at the github release page.


## Dependencies

- libc
- cJSON
- libcurl
- libarchive
- pcre




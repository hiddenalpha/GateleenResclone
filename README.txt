
Gateleen Resclone
=================

Commandline utility to clone subtrees from gateleen instances.

For release artifacts see:

  https://github.com/hiddenalpha/GateleenResclone/releases



## Cited from `gateleen-resclone --help`:

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



## Some stats for nerds (v0.0.5)

------------------------------------------
Language       files  blank  comment  code
------------------------------------------
C                  6    146       77  1065
make               1     15        2   101
C/C++ Header       7     44       35    55
------------------------------------------
SUM:              14    205      114  1221
------------------------------------------



## Build

  ./configure
  make clean
  make
  make install

Just in case you've no build machine at hand. I've uploaded my qemu
build machines alongside the released artifacts. Just look out for
"qcow2" files at the github releases page:

  https://github.com/hiddenalpha/GateleenResclone/releases



## Dependencies

- libc
- cJSON
- libcurl
- libarchive
- pcre




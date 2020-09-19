# By using this work you agree to the terms and conditions in 'LICENCE.txt'

Gateleen Resclone
=================

Commandline utility to clone subtrees from gateleen instances.

From "gateleenResclone --help":
+------------------------------------------------------------------------------
| Options:
| 
|     --pull|--push
|         Choose to download or upload.
| 
|     --url <url>
|         Root node of remote tree.
| 
|     --filter-part <path-filter>
|         Regex pattern applied as predicate to the path starting after the path
|         specified in '--url'. Each path segment will be handled as its
|         individual pattern. If there are longer paths to process, they will be
|         accepted, as long they at least start-with specified filter.
|         Example:  /foo/[0-9]+/bar
| 
|     --filter-full <path-filter>
|         Nearly same as '--filter-part'. But paths with more segments than the
|         pattern, will be rejected.
| 
|     --file <path.tar>
|         (optional) Path to the archive file to read/write. Defaults to
|         stdin/stdout if ommitted.
| 
+------------------------------------------------------------------------------


## Build

1. Make sure dependencies are in place.
2. Run "make dist"
3. Result gets placed in 'dist' directory. There are multiple tarballs:
   - Source.
   - Binaries.
   - Dependencies (will only contain those which got placed in external subdir)


## Dependencies

- libcurl     7.67.0 "http://curl.haxx.se/libcurl"
- libyajl     2.1.0  "http://lloyd.github.io/yajl"
- libarchive  3.3.3  "https://www.libarchive.org/"

If you prefer to NOT cluttering your system with tons of never again used
packages, you have the option to provide the dependencies from within the
project tree itself. For this, place the files as described below.

+------------------------------------------------------------------------------
| external/
|  '- lxGcc64/
|      |- include/
|      |   |- curl/
|      |   |   '- {curl.h, easy.h, multi.h, ...}
|      |   |- yajl/
|      |   |   '- {yajl_gen.h, yajl_parse.h, ...}
|      |   |- archive.h
|      |   '- archive_entry.h
|      '- lib/
|          '- {libarchive.a, libcurl.a, libyajl.a, libz.a}
+------------------------------------------------------------------------------


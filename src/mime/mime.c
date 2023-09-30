/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

#include "mime.h"

#include <stdlib.h>


const char* fileExtToMime( const char*ext )
{
    if( ext[0]=='7' ){
        if( ext[1]=='z' ){
            if( ext[2]=='\0' ){
                return "application/x-7z-compressed";
            }
        }
    }
    if( ext[0]=='b' ){
        if( ext[1]=='i' ){
            if( ext[2]=='n' ){
                if( ext[3]==0 ){
                    return "application/octet-stream";
                }
            }
        }
    }
    if( ext[0]=='c' ){
        if( ext[1]=='s' ){
            if( ext[2]=='s' ){
                if( ext[3]==0 ){
                    return "text/css";
                }
            }
            if( ext[2]=='v' ){
                if( ext[3]==0 ){
                    return "text/csv";
                }
            }
        }
    }
    if( ext[0]=='g' ){
        if( ext[1]=='i' ){
            if( ext[2]=='f' ){
                if( ext[3]==0 ){
                    return "image/gif";
                }
            }
        }
        if( ext[1]=='z' ){
            if( ext[2]==0 ){
                return "application/gzip";
            }
        }
    }
    if( ext[0]=='h' ){
        if( ext[1]=='t' ){
            if( ext[2]=='m' ){
                if( ext[3]==0 ){
                    return "text/html";
                }
                if( ext[3]=='l' ){
                    return "text/html";
                }
            }
        }
    }
    if( ext[0]=='i' ){
        if( ext[1]=='c' ){
            if( ext[2]=='o' ){
                if( ext[3]==0 ){
                    return "image/vnd.microsoft.icon";
                }
            }
        }
    }
    if( ext[0]=='j' ){
        if( ext[1]=='a' ){
            if( ext[2]=='r' ){
                if( ext[3]==0 ){
                    return "application/java-archive";
                }
            }
        }
        if( ext[1]=='p' ){
            if( ext[2]=='e' ){
                if( ext[3]=='g' ){
                    if( ext[4]==0 ){
                        return "image/jpeg";
                    }
                }
            }
            if( ext[2]=='g' ){
                if( ext[3]==0 ){
                    return "image/jpeg";
                }
            }
        }
    }
    if( ext[0]=='m' ){
        if( ext[1]=='p' ){
            if( ext[2]=='3' ){
                if( ext[3]==0 ){
                    return "audio/mpeg";
                }
            }
            if( ext[2]=='e' ){
                if( ext[3]=='g' ){
                    if( ext[4]==0 ){
                        return "video/mpeg";
                    }
                }
            }
        }
    }
    if( ext[0]=='o' ){
        if( ext[1]=='d' ){
            if( ext[2]=='p' ){
                if( ext[3]==0 ){
                    return "application/vnd.oasis.opendocument.presentation";
                }
            }
            if( ext[2]=='s' ){
                if( ext[3]==0 ){
                    return "application/vnd.oasis.opendocument.spreadsheet";
                }
            }
            if( ext[2]=='t' ){
                if( ext[3]==0 ){
                    return "application/vnd.oasis.opendocument.text";
                }
            }
        }
    }
    if( ext[0]=='p' ){
        if( ext[1]=='d' ){
            if( ext[2]=='f' ){
                if( ext[3]==0 ){
                    return "application/pdf";
                }
            }
        }
        if( ext[1]=='n' ){
            if( ext[2]=='g' ){
                if( ext[3]==0 ){
                    return "image/png";
                }
            }
        }
    }
    if( ext[0]=='s' ){
        if( ext[1]=='v' ){
            if( ext[2]=='g' ){
                if( ext[3]==0 ){
                    return "image/svg+xml";
                }
            }
        }
    }
    if( ext[0]=='t' ){
        if( ext[1]=='a' ){
            if( ext[2]=='r' ){
                if( ext[3]==0 ){
                    return "application/x-tar";
                }
            }
        }
        if( ext[1]=='x' ){
            if( ext[2]=='t' ){
                if( ext[3]==0 ){
                    return "text/plain";
                }
            }
        }
    }
    if( ext[0]=='j' ){
        if( ext[1]=='s' ){
            if( ext[2]==0 ){
                return "text/javascript";
            }
            if( ext[2]=='o' ){
                if( ext[3]=='n' ){
                    if( ext[4]==0 ){
                        return "application/json";
                    }
                }
            }
        }
    }
    if( ext[0]=='w' ){
        if( ext[1]=='a' ){
            if( ext[2]=='v' ){
                if( ext[3]==0 ){
                    return "audio/wav";
                }
            }
        }
        if( ext[1]=='e' ){
            if( ext[2]=='b' ){
                if( ext[3]=='a' ){
                    if( ext[4]==0 ){
                        return "audio/webm";
                    }
                }
                if( ext[3]=='m' ){
                    if( ext[4]==0 ){
                        return "video/webm";
                    }
                }
                if( ext[3]=='p' ){
                    if( ext[4]==0 ){
                        return "image/webp";
                    }
                }
            }
        }
        if( ext[1]=='o' ){
            if( ext[2]=='f' ){
                if( ext[3]=='f' ){
                    if( ext[4]=='\0' ){
                        return "font/woff";
                    }
                    if( ext[4]=='2' ){
                        if( ext[5]=='\0' ){
                            return "font/woff2";
                        }
                    }
                }
            }
        }
    }
    if( ext[0]=='x' ){
        if( ext[1]=='h' ){
            if( ext[2]=='t' ){
                if( ext[3]=='m' ){
                    if( ext[4]=='l' ){
                        if( ext[5]=='\0' ){
                            return "application/xhtml+xml";
                        }
                    }
                }
            }
        }
        if( ext[1]=='m' ){
            if( ext[2]=='l' ){
                if( ext[3]==0 ){
                    return "text/xml";
                }
            }
        }
    }
    if( ext[0]=='z' ){
        if( ext[1]=='i' ){
            if( ext[2]=='p' ){
                if( ext[3]=='\0' ){
                    return "application/zip";
                }
            }
        }
    }
    return NULL;
}


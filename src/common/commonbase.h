/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

#ifndef INCGUARD_0835dec38b8927b0daeba484a1eb21e7
#define INCGUARD_0835dec38b8927b0daeba484a1eb21e7


#define _POSIX_C_SOURCE 200809L


#ifdef _WIN32
    /* Include stuff from strange systems here to prevent errors as:
     *     winsock2.h: error: #warning Please include winsock2.h before windows.h */
    #include <winsock2.h>
#endif


typedef  unsigned int  uint_t;


#endif /* INCGUARD_0835dec38b8927b0daeba484a1eb21e7 */

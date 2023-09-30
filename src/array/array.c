/* By using this work you agree to the terms and conditions in 'LICENSE.txt' */

/* This */
#include "array.h"

/* System */
#include <stdlib.h>
#include <string.h>


ssize_t
array_add( void*arr_ , size_t*count , size_t*capacity , void*elem , unsigned int elemSize , unsigned int allocStep )
{
    void** arr = arr_;
    if( *count >= *capacity || *arr==NULL ){
        size_t newLen = *count + allocStep;
        void *newArr = realloc( *arr , newLen*elemSize );
        if( ! newArr ){ return -1; }
        *arr = newArr;
        *capacity = newLen;
    }
    void *dstAddr = ((char*)*arr) + (*count)++ * elemSize;
    memcpy( dstAddr , (char*)elem , elemSize );
    return 0;
}


#ifndef INCGUARD_6dff7e511be9d0848804e8d1e2dc23a4
#define INCGUARD_6dff7e511be9d0848804e8d1e2dc23a4

#include <commonbase.h>

#include <stdlib.h>
#include <sys/types.h>


#define TPL_ARRAY( name , type , allocStep ) \
    static inline type* STR_CAT(array_malloc_,name)( size_t count ){  \
        return malloc( count * sizeof(type) );  \
    }  \
    static inline type* STR_CAT(array_calloc_,name)( size_t count ){  \
        return calloc( count , sizeof(type) );  \
    }  \
    static inline int STR_CAT(array_add_,name)( type**arr , size_t*arrCount , size_t*arrLen , type elem ){ \
        return array_add( arr , arrCount , arrLen , &elem , sizeof(type) , allocStep ); \
    }  \


/**
 * @param arr
 *      WARN: Must be ptr to the array ptr. Not the array itself!
 * @param arr_count
 *      Count of elements already present in the array.
 * @param arr_capacity
 *      Count of elements where space is allocated for in this array.
 * @param elem
 *      A pointer to the element to add.
 * @param elemSize
 *      Size of the element to add. MUST always be the same size for all
 *      elements in this array.
 * @param allocStep
 *      Count of elements to additionally realloc in case array runs out of
 *      space.
 * @return
 *      =0: OK.
 *      <0: Error.
 *      >0: Reserved.
 */
ssize_t
array_add( void*arr , size_t*arr_count , size_t*arr_capacity , void*elem , unsigned int elemSize , unsigned int allocStep );


#endif /* INCGUARD_6dff7e511be9d0848804e8d1e2dc23a4 */

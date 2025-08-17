#ifndef TYPEDEFS_H
#define TYPEDEFS_H

typedef signed char        S8;
typedef unsigned char      U8;
typedef signed short       S16;
typedef unsigned short     U16;
typedef signed long        S32;
typedef unsigned long      U32;
typedef signed long long   S64;
typedef unsigned long long U64;

#define  S_TO_MS(a)         (a * 1000U)
#define NS_TO_MS(a)         ((U16)(a / 1000000U))
#define US_TO_MS(a)         (a * 1000U)
#define TIMESPEC_TO_S(a,b)  ((U16)(a + (U16)(b / 1000000000U)))

typedef enum
{
    FALSE,
    TRUE
} Boolean;

typedef enum
{
    FAIL = -1,
    PASS = 0
} Result;



#endif /* TYPEDEFS_H */

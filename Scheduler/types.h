typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef uint pde_t;

#ifndef _STDIO_H 
typedef int off_t;  // off_t 타입 정의
#endif

#define SEEK_SET 0 // SEEK_SET을 0으로 정의
#define SEEK_CUR 1 // SEEK_CUR을 1로 정의
#define SEEK_END 2 // SEEK_END을 2로 정의

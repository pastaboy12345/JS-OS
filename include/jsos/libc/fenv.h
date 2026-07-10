#ifndef JSOS_FENV_H
#define JSOS_FENV_H

typedef unsigned int fenv_t;
typedef unsigned int fexcept_t;

#define FE_TONEAREST 0
#define FE_DOWNWARD 1
#define FE_UPWARD 2
#define FE_TOWARDZERO 3
#define FE_ALL_EXCEPT 0x3f

int fegetround(void);
int fesetround(int mode);

#endif

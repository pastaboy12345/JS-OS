#ifndef JSOS_ASSERT_H
#define JSOS_ASSERT_H

#include <stdlib.h>

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
void jsos_assert_fail(const char *expression, const char *file, int line);
#define assert(expression) \
    ((expression) ? (void)0 : jsos_assert_fail(#expression, __FILE__, __LINE__))
#endif

#endif

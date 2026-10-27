#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// mbedTLS 4 wraps these in its own mbedtls_threading_mutex_t, which records
// whether each was initialised, and asks for condition variables too.
typedef SRWLOCK mbedtls_platform_mutex_t;
typedef CONDITION_VARIABLE mbedtls_platform_condition_variable_t;

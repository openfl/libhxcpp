/*
 * hxcpp builds mbedTLS with MBEDTLS_THREADING_C, and on Windows, where
 * mbedTLS has no mutexes of its own, with MBEDTLS_THREADING_ALT: these are
 * those mutexes, slim reader/writer locks taken exclusively, and the
 * condition variables mbedTLS 4 asks for beside them. mbedTLS reaches them
 * through function pointers that fail every lock until
 * mbedtls_threading_set_alt() installs them -- and PSA, where all of
 * mbedTLS 4's cryptography runs, locks one for its key slots and its
 * random generator.
 *
 * hxcpp's SSL installed them in _hx_ssl_init(), so a program using sys.ssl
 * had them. Code that is not hxcpp's had them only if something called that
 * first: a static Lime build's curl, compiled against this mbedTLS, failed
 * every HTTPS request with "CTR_DRBG - The entropy source failed" in a
 * program that used nothing of sys.ssl. So they are installed at load,
 * before main, by an initializer that every object built against hxcpp's
 * mbedTLS configuration pulls into an MSVC link (see
 * include/tf_psa_crypto_config.h). MinGW has no such directive: there the
 * constructor runs when this object is linked, as it is beside hxcpp's SSL,
 * which still installs them itself.
 */
#include "mbedtls/threading.h"

#if defined(MBEDTLS_THREADING_ALT) && defined(_WIN32)

static int hxcpp_mutex_init(mbedtls_platform_mutex_t *mutex)
{
	InitializeSRWLock(mutex);
	return 0;
}

/* An SRW lock holds nothing to release. */
static void hxcpp_mutex_destroy(mbedtls_platform_mutex_t *mutex)
{
	(void)mutex;
}

static int hxcpp_mutex_lock(mbedtls_platform_mutex_t *mutex)
{
	AcquireSRWLockExclusive(mutex);
	return 0;
}

static int hxcpp_mutex_unlock(mbedtls_platform_mutex_t *mutex)
{
	ReleaseSRWLockExclusive(mutex);
	return 0;
}

static int hxcpp_cond_init(mbedtls_platform_condition_variable_t *cond)
{
	InitializeConditionVariable(cond);
	return 0;
}

static void hxcpp_cond_destroy(mbedtls_platform_condition_variable_t *cond)
{
	(void)cond;
}

static int hxcpp_cond_signal(mbedtls_platform_condition_variable_t *cond)
{
	WakeConditionVariable(cond);
	return 0;
}

static int hxcpp_cond_broadcast(mbedtls_platform_condition_variable_t *cond)
{
	WakeAllConditionVariable(cond);
	return 0;
}

static int hxcpp_cond_wait(mbedtls_platform_condition_variable_t *cond, mbedtls_platform_mutex_t *mutex)
{
	if (!SleepConditionVariableSRW(cond, mutex, INFINITE, 0))
		return MBEDTLS_ERR_THREADING_USAGE_ERROR;
	return 0;
}

/* Once only: installing them again would initialise afresh the mutexes
   mbedTLS keeps for itself -- PSA's key slots and generator -- under whoever
   holds them. */
void hxcpp_mbedtls_threading_init(void)
{
	static volatile LONG installed = 0;
	if (InterlockedCompareExchange(&installed, 1, 0) == 0)
		mbedtls_threading_set_alt(hxcpp_mutex_init, hxcpp_mutex_destroy, hxcpp_mutex_lock, hxcpp_mutex_unlock,
			hxcpp_cond_init, hxcpp_cond_destroy, hxcpp_cond_signal, hxcpp_cond_broadcast, hxcpp_cond_wait);
}

#if defined(_MSC_VER)
static void __cdecl hxcpp_mbedtls_threading_at_load(void)
{
	hxcpp_mbedtls_threading_init();
}

/* An entry in the C runtime's initializer table, run before main. */
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void (__cdecl *hxcpp_mbedtls_threading_initializer)(void) = hxcpp_mbedtls_threading_at_load;
#elif defined(__GNUC__)
__attribute__((constructor)) static void hxcpp_mbedtls_threading_at_load(void)
{
	hxcpp_mbedtls_threading_init();
}
#endif

#else

/* Elsewhere mbedTLS has its own mutexes (MBEDTLS_THREADING_PTHREAD), set up
   statically. */
typedef int hxcpp_mbedtls_threading_unused;

#endif

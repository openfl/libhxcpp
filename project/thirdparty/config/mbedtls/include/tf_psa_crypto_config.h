// hxcpp's additions to TF-PSA-Crypto's default configuration, read through
// TF_PSA_CRYPTO_USER_CONFIG_FILE (../../../mbedtls-flags.xml). mbedTLS 4
// keeps its threading options here, beside the cryptography, and refuses
// them in its own TLS configuration. The TLS configuration is upstream's.
#ifdef HX_WINDOWS
#define MBEDTLS_THREADING_ALT
#endif
#ifndef HX_WINDOWS
#define MBEDTLS_THREADING_PTHREAD
#endif

#define MBEDTLS_THREADING_C

#if defined(MBEDTLS_THREADING_ALT) && defined(_MSC_VER)
// Every object built against this configuration -- mbedTLS's own, hxcpp's
// SSL, a native extension -- pulls into the link the initializer that
// installs the Windows mutexes before main (../../threading_alt.c), so they
// are in place for code that never calls hxcpp's _hx_ssl_init().
#if defined(_M_IX86)
#pragma comment(linker, "/include:_hxcpp_mbedtls_threading_initializer")
#else
#pragma comment(linker, "/include:hxcpp_mbedtls_threading_initializer")
#endif
#endif

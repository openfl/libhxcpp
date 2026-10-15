#include <string.h>
#include <atomic>

#ifdef HX_WINDOWS
#   include <winsock2.h>
#   include <wincrypt.h>
#else
#   include <sys/socket.h>
#   include <strings.h>
#   include <errno.h>
typedef int SOCKET;
#endif


#include <hxcpp.h>
#include <hx/OS.h>

#if defined(NEKO_MAC)
#include <Security/Security.h>
#endif

typedef size_t socket_int;

// As std's Socket.cpp sends: a write to a peer that has gone raises SIGPIPE
// on Linux, whose default is to end the process, where it should fail with
// EPIPE. macOS has no MSG_NOSIGNAL and std sets SO_NOSIGPIPE on the socket
// instead; Windows raises no signal.
#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

#define SOCKET_ERROR (-1)
#define NRETRYS	20

#include "mbedtls/error.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/oid.h"
#include "mbedtls/x509_crt.h"
#ifndef HXCPP_SSL_NO_TICKETS
#include "mbedtls/ssl_ticket.h"
#endif
#include "mbedtls/ssl.h"
#include "mbedtls/debug.h"
#include "psa/crypto.h"

#define val_ssl(o)	((sslctx*)o.mPtr)
#define val_conf(o)	((sslconf*)o.mPtr)
#define val_cert(o) ((sslcert*)o.mPtr)
#define val_pkey(o) ((sslpkey*)o.mPtr)

size_t _hx_std_socket_handle(Dynamic inSocket);

// A configuration's mbedTLS state, owned jointly by its handle and by every
// context set up on it. mbedTLS keeps a pointer to the configuration in each
// context and reads it for the life of the connection -- every read checks
// its renegotiation settings, and freeing the context reads it too -- so the
// configuration has to outlive them all. It did not: closing a listening
// socket closed its configuration, freeing it under the connections it had
// accepted, which went on reading freed memory. A server that stops
// listening while it finishes its connections, as a graceful shutdown does,
// did exactly that. The handle and each context hold a reference, and the
// last to let go frees it.
struct sslconf_shared
{
	mbedtls_ssl_config config;
#ifndef HXCPP_SSL_NO_TICKETS
	// A server configuration's session tickets: see _hx_ssl_conf_new.
	mbedtls_ssl_ticket_context *ticket;
#endif
	std::atomic<int> refs;
	// The objects whose native data the configuration points at, and the SNI
	// callback it calls. The handle and every context set up on the
	// configuration mark them, so they live, and follow a moving collection,
	// for as long as the configuration is in use.
	Dynamic cert;
	Dynamic key;
	Dynamic ca;
	Dynamic sni;
};

static void sslconf_shared_mark( sslconf_shared *shared, hx::MarkContext *__inCtx )
{
	HX_MARK_MEMBER( shared->cert );
	HX_MARK_MEMBER( shared->key );
	HX_MARK_MEMBER( shared->ca );
	HX_MARK_MEMBER( shared->sni );
}

#ifdef HXCPP_VISIT_ALLOCS
static void sslconf_shared_visit( sslconf_shared *shared, hx::VisitContext *__inCtx )
{
	HX_VISIT_MEMBER( shared->cert );
	HX_VISIT_MEMBER( shared->key );
	HX_VISIT_MEMBER( shared->ca );
	HX_VISIT_MEMBER( shared->sni );
}
#endif

static sslconf_shared *sslconf_shared_retain( sslconf_shared *shared )
{
	shared->refs.fetch_add( 1, std::memory_order_relaxed );
	return shared;
}

// From any thread: a context may be finalized by whichever thread collects.
static void sslconf_shared_release( sslconf_shared *shared )
{
	if( shared->refs.fetch_sub( 1, std::memory_order_acq_rel ) != 1 )
		return;
	mbedtls_ssl_config_free( &shared->config );
#ifndef HXCPP_SSL_NO_TICKETS
	if( shared->ticket )
	{
		mbedtls_ssl_ticket_free( shared->ticket );
		free( shared->ticket );
	}
#endif
	delete shared;
}

struct sslctx : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdSsl };

	// First: native code outside this file reads it at that position.
	mbedtls_ssl_context *s;
	// The configuration s was set up on, held until s is freed.
	sslconf_shared *conf;

	void create()
	{
		s = (mbedtls_ssl_context *)malloc(sizeof(mbedtls_ssl_context));
		mbedtls_ssl_init(s);
		conf = 0;
		_hx_set_finalizer(this, finalize);
	}

	void destroy()
	{
		if( s )
		{
			mbedtls_ssl_free( s );
			free(s);
			s = 0;
		}
		// After the context: freeing it reads the configuration
		if( conf )
		{
			sslconf_shared_release( conf );
			conf = 0;
		}
	}

	static void finalize(Dynamic obj)
	{
		((sslctx *)(obj.mPtr))->destroy();
	}

	void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE
	{
		if( conf )
			sslconf_shared_mark( conf, __inCtx );
	}
#ifdef HXCPP_VISIT_ALLOCS
	void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE
	{
		if( conf )
			sslconf_shared_visit( conf, __inCtx );
	}
#endif

	String toString() HXCPP_OVERRIDE { return HX_CSTRING("sslctx"); }
};

struct sslconf : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdSslConf };

	// First: native code outside this file reads it at that position. Null
	// once the handle is closed, as before.
	mbedtls_ssl_config *c;
	sslconf_shared *shared;

	void create()
	{
		shared = new sslconf_shared();
		mbedtls_ssl_config_init( &shared->config );
#ifndef HXCPP_SSL_NO_TICKETS
		shared->ticket = 0;
#endif
		shared->refs.store( 1, std::memory_order_relaxed );
		c = &shared->config;
		_hx_set_finalizer(this, finalize);
	}

	// Lets go of the handle's reference: the configuration itself goes when
	// the last context set up on it does.
	void destroy()
	{
		c = 0;
		if( shared )
		{
			sslconf_shared_release( shared );
			shared = 0;
		}
	}

	static void finalize(Dynamic obj)
	{
		((sslconf *)(obj.mPtr))->destroy();
	}

	void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE
	{
		if( shared )
			sslconf_shared_mark( shared, __inCtx );
	}
#ifdef HXCPP_VISIT_ALLOCS
	void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE
	{
		if( shared )
			sslconf_shared_visit( shared, __inCtx );
	}
#endif

	String toString() HXCPP_OVERRIDE { return HX_CSTRING("sslconfig"); }
};

struct sslcert : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdSslCert };

	mbedtls_x509_crt *c;
	bool head;

	void create(const mbedtls_x509_crt *inC)
	{

		if( inC ){
			c = (mbedtls_x509_crt *)inC;
			head = false;
		}else{
			c = (mbedtls_x509_crt *)malloc(sizeof(mbedtls_x509_crt));
			mbedtls_x509_crt_init(c);
			head = true;
		}
		_hx_set_finalizer(this, finalize);
	}

	void destroy()
	{
		if( c && head )
		{
			mbedtls_x509_crt_free( c );
			free(c);
			head = 0;
		}
		c = 0;
	}

	static void finalize(Dynamic obj)
	{
		((sslcert *)(obj.mPtr))->destroy();
	}

	String toString() HXCPP_OVERRIDE { return HX_CSTRING("sslcert"); }
};

struct sslpkey : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdSslKey };

	mbedtls_pk_context *k;

	void create()
	{
		k = (mbedtls_pk_context *)malloc(sizeof(mbedtls_pk_context));
		mbedtls_pk_init(k);
		_hx_set_finalizer(this, finalize);
	}

	void destroy()
	{
		if( k )
		{
			mbedtls_pk_free(k);
			free(k);
			k = 0;
		}
	}

	static void finalize(Dynamic obj)
	{
		((sslpkey *)(obj.mPtr))->destroy();
	}

	String toString() HXCPP_OVERRIDE { return HX_CSTRING("sslpkey"); }
};

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

static bool is_ssl_blocking( int r ) {
	return r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE;
}

static bool is_block_error() {
	#ifdef NEKO_WINDOWS
	int err = WSAGetLastError();
	if( err == WSAEWOULDBLOCK || err == WSAEALREADY || err == WSAETIMEDOUT )
	#else
	if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EALREADY )
	#endif
		return true;
	return false;
}

static void ssl_error( int ret ){
	char buf[256];
	mbedtls_strerror(ret, buf, sizeof(buf));
	hx::Throw( String(buf) );
}

Dynamic _hx_ssl_new( Dynamic hconf ) {
	int ret;
	sslconf *conf = val_conf(hconf);
	// mbedtls_ssl_setup with a closed configuration's null crashed
	if( !conf->shared )
		hx::Throw( HX_CSTRING("ssl_new: the configuration is closed") );
	sslctx *ssl = new sslctx();
	ssl->create();
	if( (ret = mbedtls_ssl_setup(ssl->s, conf->c)) != 0 ){
		ssl->destroy();
		ssl_error(ret);
	}
	ssl->conf = sslconf_shared_retain( conf->shared );
	return ssl;
}

void _hx_ssl_close( Dynamic hssl ) {
	sslctx *ssl = val_ssl(hssl);
	ssl->destroy();
}

void _hx_ssl_debug_set (int i) {
	mbedtls_debug_set_threshold(i);
}

void _hx_ssl_handshake( Dynamic hssl ) {
	int r;
	sslctx *ssl = val_ssl(hssl);
	POSIX_LABEL(handshake_again);
	r = mbedtls_ssl_handshake( ssl->s );
	if ( is_ssl_blocking(r) ) {
		HANDLE_EINTR(handshake_again);
		hx::Throw(HX_CSTRING("Blocking"));
	}else if( r == SOCKET_ERROR ) {
		HANDLE_EINTR(handshake_again);
		hx::Throw(HX_CSTRING("ssl network error"));
	}else if( r != 0 )
		ssl_error(r);
}

int net_read( void *fd, unsigned char *buf, size_t len ){
	hx::EnterGCFreeZone();
	int r = recv((SOCKET)(socket_int)fd, (char *)buf, len, 0);
 	if( r == SOCKET_ERROR && is_block_error() )
 		r = MBEDTLS_ERR_SSL_WANT_READ;
	hx::ExitGCFreeZone();
	return r;
}

int net_write( void *fd, const unsigned char *buf, size_t len ){
	hx::EnterGCFreeZone();
	int r = send((SOCKET)(socket_int)fd, (char *)buf, len, MSG_NOSIGNAL);
	if( r == SOCKET_ERROR && is_block_error() )
 		r = MBEDTLS_ERR_SSL_WANT_WRITE;
	hx::ExitGCFreeZone();
	return r;
}

void _hx_ssl_set_socket( Dynamic hssl, Dynamic hsocket ) {
	sslctx *ssl = val_ssl(hssl);
	mbedtls_ssl_set_bio( ssl->s, (void *)(socket_int)_hx_std_socket_handle(hsocket), net_write, net_read, NULL );
}

void _hx_ssl_set_hostname( Dynamic hssl, String hostname ){
	int ret;
	sslctx *ssl = val_ssl(hssl);
	hx::strbuf buf;
	if( (ret = mbedtls_ssl_set_hostname(ssl->s, hostname.utf8_str(&buf))) != 0 )
		ssl_error(ret);
}

Dynamic _hx_ssl_get_peer_certificate( Dynamic hssl ){
	sslctx *ssl = val_ssl(hssl);
	const mbedtls_x509_crt *crt = mbedtls_ssl_get_peer_cert(ssl->s);
	if( crt == NULL )
		return null();
	sslcert *cert = new sslcert();
	cert->create( crt );
	return cert;
}

bool _hx_ssl_get_verify_result( Dynamic hssl ){
	sslctx *ssl = val_ssl(hssl);
	int r = mbedtls_ssl_get_verify_result( ssl->s );
	if( r == 0 )
		return true;
	else if( r != -1 )
		return false;
	hx::Throw( HX_CSTRING("not available") );
	return false;
}

void _hx_ssl_send_char( Dynamic hssl, int c ) {
	if( c < 0 || c > 255 )
		hx::Throw( HX_CSTRING("invalid char") );
	sslctx *ssl = val_ssl(hssl);
	const unsigned char cc = c;
	// Check the result like every other write path - dropping it loses the
	// byte silently on would-block or error and desyncs the stream
	POSIX_LABEL(send_char_again);
	int r = mbedtls_ssl_write( ssl->s, &cc, 1 );
	if( is_ssl_blocking(r) ) {
		HANDLE_EINTR(send_char_again);
		hx::Throw(HX_CSTRING("Blocking"));
	}
	if( r <= 0 )
		hx::Throw( HX_CSTRING("ssl_send_char") );
}

int _hx_ssl_send( Dynamic hssl, Array<unsigned char> buf, int p, int l ) {
	sslctx *ssl = val_ssl(hssl);
	int dlen = buf->length;
	// l > dlen - p, not p + l > dlen: the addition can overflow and skip the check
	if( p < 0 || l < 0 || p > dlen || l > dlen - p )
		hx::Throw( HX_CSTRING("ssl_send") );
	// Stage through a stack buffer: mbedtls drives the socket BIO inside a
	// GC free zone mid-call, so a pointer into the movable Array must not
	// be held across the call.  One TLS record carries at most 16K, so a
	// short write just reports fewer bytes and the caller continues
	unsigned char tmp[16384];
	if( l > (int)sizeof(tmp) )
		l = (int)sizeof(tmp);
	memcpy( tmp, &buf[0] + p, l );
	POSIX_LABEL(send_again);
	dlen = mbedtls_ssl_write( ssl->s, tmp, l );
	if ( is_ssl_blocking(dlen) ) {
		HANDLE_EINTR(send_again);
		hx::Throw(HX_CSTRING("Blocking"));
	}else if( dlen == SOCKET_ERROR ) {
		HANDLE_EINTR(send_again);
		hx::Throw(HX_CSTRING("ssl network error"));
	}
	if( dlen < 0 )
		ssl_error( dlen );
	return dlen;
}

void _hx_ssl_write( Dynamic hssl, Array<unsigned char> buf ) {
	sslctx *ssl = val_ssl(hssl);
	int len = buf->length;
	int pos = 0;
	unsigned char tmp[16384];
	while( len > 0 ) {
		// Re-fetch the array data each chunk and copy to the stack - see
		// _hx_ssl_send: no GC pointer may live across the mbedtls call
		int chunk = len > (int)sizeof(tmp) ? (int)sizeof(tmp) : len;
		memcpy( tmp, &buf[0] + pos, chunk );
		POSIX_LABEL( write_again );
		int slen = mbedtls_ssl_write( ssl->s, tmp, chunk );
		if ( is_ssl_blocking(slen) ) {
			HANDLE_EINTR( write_again );
			hx::Throw(HX_CSTRING("Blocking"));
		}else if( slen == SOCKET_ERROR ) {
			HANDLE_EINTR( write_again );
			hx::Throw(HX_CSTRING("ssl network error"));
		}
		if( slen <= 0 )
			ssl_error( slen );
		pos += slen;
		len -= slen;
	}
}

int _hx_ssl_recv_char( Dynamic hssl ) {
	sslctx *ssl = val_ssl(hssl);
	unsigned char cc;
	// Mirror _hx_ssl_recv - a would-block read is "Blocking", not a fatal
	// error that the input wrapper turns into a spurious Eof
	POSIX_LABEL(recv_char_again);
	int r = mbedtls_ssl_read( ssl->s, &cc, 1 );
	if( is_ssl_blocking(r) ) {
		HANDLE_EINTR(recv_char_again);
		hx::Throw(HX_CSTRING("Blocking"));
	}
	if( r <= 0 )
		hx::Throw( HX_CSTRING("ssl_recv_char") );
	return (int)cc;
}

int _hx_ssl_recv( Dynamic hssl, Array<unsigned char> buf, int p, int l ) {
	sslctx *ssl = val_ssl(hssl);
	int dlen = buf->length;
	if( p < 0 || l < 0 || p > dlen || l > dlen - p )
		hx::Throw( HX_CSTRING("ssl_recv") );

	// Decrypt into a stack buffer - see _hx_ssl_send: mbedtls enters a GC
	// free zone mid-call, so no pointer into the movable Array may be
	// held across it.  One record is at most 16K; a short read is fine
	unsigned char tmp[16384];
	if( l > (int)sizeof(tmp) )
		l = (int)sizeof(tmp);
	POSIX_LABEL(recv_again);
	dlen = mbedtls_ssl_read( ssl->s, tmp, l );
	if ( is_ssl_blocking(dlen) ) {
		HANDLE_EINTR(recv_again);
		hx::Throw(HX_CSTRING("Blocking"));
	}else if( dlen == SOCKET_ERROR ) {
		HANDLE_EINTR(recv_again);
		hx::Throw(HX_CSTRING("ssl network error"));
	}
	if( dlen < 0 ) {
                if( dlen == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ) {
                  mbedtls_ssl_close_notify( ssl->s );
	  	  return 0;
                }
		hx::Throw( HX_CSTRING("ssl_recv") );
	}
	if( dlen > 0 )
		memcpy( &buf[0] + p, tmp, dlen );
	return dlen;
}

Array<unsigned char> _hx_ssl_read( Dynamic hssl ) {
	sslctx *ssl = val_ssl(hssl);
	Array<unsigned char> result = Array_obj<unsigned char>::__new();
	// One TLS record is up to 16K - draining it through a 256-byte buffer
	// paid 64 mbedtls calls and 64 array growths per record
	unsigned char buf[16384];

	while( true ) {
		POSIX_LABEL(read_again);
		int len = mbedtls_ssl_read( ssl->s, buf, sizeof(buf) );
		if ( is_ssl_blocking(len) ) {
			HANDLE_EINTR(read_again);
			hx::Throw(HX_CSTRING("Blocking"));
		}else if( len == SOCKET_ERROR ) {
			HANDLE_EINTR(read_again);
			hx::Throw(HX_CSTRING("ssl network error"));
		}
                if( len == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ) {
                  mbedtls_ssl_close_notify( ssl->s );
	  	  len = 0;
                }
		if( len == 0 )
			break;
		if( len < 0 )
			ssl_error( len );
		result->memcpy(result->length, buf, len);
	}
	return result;
}

#ifdef NEKO_WINDOWS
static int verify_callback(void* param, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
	if (*flags == 0 || *flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) {
		return 0;
	}

	HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_DEFER_CLOSE_UNTIL_LAST_FREE_FLAG, NULL);
	if(store == NULL) {
		return MBEDTLS_ERR_X509_FATAL_ERROR;
	}
	PCCERT_CONTEXT primary_context = {0};
	if(!CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, crt->raw.p, crt->raw.len, CERT_STORE_ADD_REPLACE_EXISTING, &primary_context)) {
		CertCloseStore(store, 0);
		return MBEDTLS_ERR_X509_FATAL_ERROR;
	}
	PCCERT_CHAIN_CONTEXT chain_context = {0};
	CERT_CHAIN_PARA parameters = {0};
	if(!CertGetCertificateChain(NULL, primary_context, NULL, store, &parameters, 0, NULL, &chain_context)) {
		CertFreeCertificateContext(primary_context);
		CertCloseStore(store, 0);
		return MBEDTLS_ERR_X509_FATAL_ERROR;
	}
	CERT_CHAIN_POLICY_PARA policy_parameters = {0};
	CERT_CHAIN_POLICY_STATUS policy_status = {0};
	if(!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain_context, &policy_parameters, &policy_status)) {
		CertFreeCertificateChain(chain_context);
		CertFreeCertificateContext(primary_context);
		CertCloseStore(store, 0);
		return MBEDTLS_ERR_X509_FATAL_ERROR;
	}
	if(policy_status.dwError == 0) {
		*flags = 0;
	} else {
		// if we ever want to read the verification result,
		// we need to properly map dwError to flags
		*flags |= MBEDTLS_X509_BADCERT_OTHER;
	}
	CertFreeCertificateChain(chain_context);
	CertFreeCertificateContext(primary_context);
	CertCloseStore(store, 0);
	return 0;
}
#elif defined(IPHONE) || defined(APPLETV)
// iOS and tvOS have no root store to load, so when mbedTLS cannot trust a chain the
// system judges it, and only that judgement replaces mbedTLS's: a wrong hostname, an
// expired certificate or a bad signature stays an error. mbedTLS reports the untrusted
// top of the chain first and the leaf last, so the decision waits for the leaf.
static thread_local bool sChainUntrusted = false;

static int verify_callback(void* param, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
	if (depth > 0) {
		if (*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
			sChainUntrusted = true;
			*flags &= ~MBEDTLS_X509_BADCERT_NOT_TRUSTED;
		}
		return 0;
	}

	bool untrusted = sChainUntrusted || (*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED);
	sChainUntrusted = false;
	if (!untrusted || (*flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)) {
		if (untrusted)
			*flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
		return 0;
	}

	// The leaf, then the rest of the chain the peer sent, to find its issuers in
	CFMutableArrayRef certs = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	if (!certs)
		return MBEDTLS_ERR_X509_FATAL_ERROR;
	for (const mbedtls_x509_crt *c = crt; c && c->raw.p; c = c->next) {
		CFDataRef der = CFDataCreate(NULL, c->raw.p, c->raw.len);
		SecCertificateRef cert = der ? SecCertificateCreateWithData(NULL, der) : NULL;
		if (der)
			CFRelease(der);
		if (cert) {
			CFArrayAppendValue(certs, cert);
			CFRelease(cert);
		} else if (c == crt) {
			CFRelease(certs);
			return MBEDTLS_ERR_X509_FATAL_ERROR;
		}
	}

	SecPolicyRef policy = SecPolicyCreateSSL(true, NULL);
	SecTrustRef trust = NULL;
	OSStatus status = SecTrustCreateWithCertificates(certs, policy, &trust);
	CFRelease(certs);
	CFRelease(policy);
	if (status != errSecSuccess || !trust)
		return MBEDTLS_ERR_X509_FATAL_ERROR;

	bool trusted;
	if (__builtin_available(iOS 12.0, tvOS 12.0, *)) {
		trusted = SecTrustEvaluateWithError(trust, NULL);
	} else {
		SecTrustResultType result;
		trusted = SecTrustEvaluate(trust, &result) == errSecSuccess &&
			(result == kSecTrustResultUnspecified || result == kSecTrustResultProceed);
	}
	CFRelease(trust);

	*flags &= ~MBEDTLS_X509_BADCERT_NOT_TRUSTED;
	if (!trusted)
		*flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
	return 0;
}
#endif

Dynamic _hx_ssl_conf_new( bool server ) {
	int ret;
	sslconf *conf = new sslconf();
	conf->create();
	if( (ret = mbedtls_ssl_config_defaults( conf->c,
		server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
		MBEDTLS_SSL_TRANSPORT_STREAM, 0 )) != 0 ){
		conf->destroy();
		ssl_error( ret );
	}
#if defined(NEKO_WINDOWS) || defined(IPHONE) || defined(APPLETV)
	mbedtls_ssl_conf_verify(conf->c, verify_callback, NULL);
#endif
	// No version floor to set: mbedTLS 3 speaks TLS 1.2 and 1.3 only, and the
	// preset negotiates 1.3 whenever the peer can
	mbedtls_ssl_conf_rng( conf->c, mbedtls_ctr_drbg_random, &ctr_drbg );
#ifndef HXCPP_SSL_NO_TICKETS
	// Session tickets (RFC 5077), so a client that has been here before
	// resumes without the handshake's signature and key exchange: 4-7 ms of
	// CPU each on an MSVC build, where neither has assembly. A client only
	// gets one if it asks, and a ticket that cannot be opened -- expired,
	// from another process -- means a full handshake, as before.
	//
	// One key per server configuration, which its accepted connections
	// share, so a ticket opens only on the server that issued it: one
	// server's session cannot be resumed on another in the same process
	// with a different certificate or client-certificate policy. The key is
	// random, lives only in memory, and rotates every TICKET_LIFETIME
	// seconds, which also bounds how long a ticket is honoured.
	if( server )
	{
		static const uint32_t TICKET_LIFETIME = 3600;
		mbedtls_ssl_ticket_context *ticket = (mbedtls_ssl_ticket_context *)malloc(sizeof(mbedtls_ssl_ticket_context));
		mbedtls_ssl_ticket_init( ticket );
		if( mbedtls_ssl_ticket_setup( ticket, mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_CIPHER_AES_256_GCM, TICKET_LIFETIME ) == 0 )
		{
			conf->shared->ticket = ticket;
			mbedtls_ssl_conf_session_tickets_cb( conf->c, mbedtls_ssl_ticket_write, mbedtls_ssl_ticket_parse, ticket );
		}
		else
		{
			// Without tickets, every handshake is a full one, as before.
			mbedtls_ssl_ticket_free( ticket );
			free( ticket );
		}
	}
#endif
	return conf;
}

void _hx_ssl_conf_close( Dynamic hconf ) {
	sslconf *conf = val_conf(hconf);
	conf->destroy();
}

void _hx_ssl_conf_set_ca( Dynamic hconf, Dynamic hcert ) {
	sslconf *conf = val_conf(hconf);
	// Branch on the cert, not the config - testing hconf (always non-null
	// here) made the null-CA branch dead and a null cert a crash
	if( hcert.mPtr ){
		sslcert *cert = val_cert(hcert);
		mbedtls_ssl_conf_ca_chain( conf->c, cert->c, NULL );
	}else{
		mbedtls_ssl_conf_ca_chain( conf->c, NULL, NULL );
	}
	conf->shared->ca = hcert;
	HX_OBJ_WB_GET( conf, hcert.mPtr );
}

void _hx_ssl_conf_set_verify( Dynamic hconf, int mode ) {
	sslconf *conf = val_conf(hconf);
	if( mode == 2 )
		mbedtls_ssl_conf_authmode(conf->c, MBEDTLS_SSL_VERIFY_OPTIONAL);
	else if( mode == 1 )
		mbedtls_ssl_conf_authmode(conf->c, MBEDTLS_SSL_VERIFY_REQUIRED);
	else
		mbedtls_ssl_conf_authmode(conf->c, MBEDTLS_SSL_VERIFY_NONE);
}

void _hx_ssl_conf_set_cert( Dynamic hconf, Dynamic hcert, Dynamic hpkey ) {
	int r;
	sslconf *conf = val_conf(hconf);
	sslcert *cert = val_cert(hcert);
	sslpkey *pkey = val_pkey(hpkey);

	if( (r = mbedtls_ssl_conf_own_cert(conf->c, cert->c, pkey->k)) != 0 )
		ssl_error(r);
	conf->shared->cert = hcert;
	conf->shared->key = hpkey;
	HX_OBJ_WB_GET( conf, hcert.mPtr );
	HX_OBJ_WB_GET( conf, hpkey.mPtr );
}

static int sni_callback( void *arg, mbedtls_ssl_context *ctx, const unsigned char *name, size_t len ){
	sslconf_shared *shared = (sslconf_shared *)arg;
	if( name && shared->sni.mPtr ){
#if (HXCPP_API_LEVEL>=500)
		auto cb  = hx::Callable<::Dynamic(::String)>(shared->sni);
		auto n   = reinterpret_cast<const char*>(name);
		auto ret = cb(String::create(n, (int)len));
#else
		const char *n = (const char *)name;
		Dynamic ret = shared->sni->__run( String::create(n,(int)len) );
#endif
		if( ret != null() ){
			// TODO authmode and ca
			Dynamic hcert = ret->__Field(HX_CSTRING("cert"), hx::paccDynamic);
			Dynamic hpkey = ret->__Field(HX_CSTRING("key"), hx::paccDynamic);
			if( !hcert.mPtr || !hpkey.mPtr )
				return -1;
			sslcert *cert = val_cert(hcert);
			sslpkey *pk = val_pkey(hpkey);

			return mbedtls_ssl_set_hs_own_cert( ctx, cert->c, pk->k );
		}
	}
	return -1;
}

#if (HXCPP_API_LEVEL>=500)
void _hx_ssl_conf_set_servername_callback(Dynamic hconf, ::hx::Callable<::Dynamic(String)> cb) {
#else
void _hx_ssl_conf_set_servername_callback( Dynamic hconf, Dynamic cb ){
#endif
	sslconf *conf = val_conf(hconf);
	conf->shared->sni = Dynamic( cb.mPtr );
	HX_OBJ_WB_GET( conf, cb.mPtr );
	mbedtls_ssl_conf_sni( conf->c, sni_callback, conf->shared );
}

Dynamic _hx_ssl_cert_load_defaults(){
#if defined(NEKO_WINDOWS)
	HCERTSTORE store;
	PCCERT_CONTEXT cert;
	sslcert *chain = NULL;
	if( store = CertOpenSystemStore(0, (LPCSTR)"Root") ){
		cert = NULL;
		while( cert = CertEnumCertificatesInStore(store, cert) ){
			if( chain == NULL ){
				chain = new sslcert();
				chain->create( NULL );
			}
			mbedtls_x509_crt_parse_der( chain->c, (unsigned char *)cert->pbCertEncoded, cert->cbCertEncoded );
		}
		CertCloseStore(store, 0);
	}
	if( chain != NULL )
		return chain;
#elif defined(NEKO_MAC) && !defined(IPHONE) && !defined(APPLETV)
	CFMutableDictionaryRef search;
	CFArrayRef result;
	SecKeychainRef keychain;
	SecCertificateRef item;
	CFDataRef dat;
	sslcert *chain = NULL;

	// Load keychain
	if( SecKeychainOpen("/System/Library/Keychains/SystemRootCertificates.keychain",&keychain) != errSecSuccess )
		return null();

	// Search for certificates. Every CF object created or copied here is
	// released: this runs for every TLS connection, and the matching array
	// alone holds the whole system root store (hundreds of KB).
	search = CFDictionaryCreateMutable( NULL, 0, NULL, NULL );
	CFArrayRef searchList = CFArrayCreate(NULL, (const void **)&keychain, 1, NULL);
	CFDictionarySetValue( search, kSecClass, kSecClassCertificate );
	CFDictionarySetValue( search, kSecMatchLimit, kSecMatchLimitAll );
	CFDictionarySetValue( search, kSecReturnRef, kCFBooleanTrue );
	CFDictionarySetValue( search, kSecMatchSearchList, searchList );
	result = NULL;
	if( SecItemCopyMatching( search, (CFTypeRef *)&result ) == errSecSuccess && result ){
		CFIndex n = CFArrayGetCount( result );
		for( CFIndex i = 0; i < n; i++ ){
			item = (SecCertificateRef)CFArrayGetValueAtIndex( result, i );

			// Get certificate in DER format
			dat = SecCertificateCopyData( item );
			if( dat ){
				if( chain == NULL ){
					chain = new sslcert();
					chain->create( NULL );
				}
				mbedtls_x509_crt_parse_der( chain->c, (unsigned char *)CFDataGetBytePtr(dat), CFDataGetLength(dat) );
				CFRelease( dat );
			}
		}
	}
	if( result ) CFRelease( result );
	CFRelease( search );
	CFRelease( searchList );
	CFRelease(keychain);
	if( chain != NULL )
		return chain;
#elif defined(IPHONE) || defined(APPLETV)
	// An empty chain, so that verification runs and verify_callback asks the system
	sslcert *chain = new sslcert();
	chain->create( NULL );
	return chain;
#endif
	return null();
}

Dynamic _hx_ssl_cert_load_file( String file ){
	int r;
	sslcert *cert = new sslcert();
	cert->create( NULL );
   hx::strbuf buf;
	if( (r = mbedtls_x509_crt_parse_file(cert->c, file.utf8_str(&buf))) != 0 ){
		cert->destroy();
		ssl_error(r);
	}
	return cert;
}

Dynamic _hx_ssl_cert_load_path( String path ){
	int r;
	sslcert *cert = new sslcert();
	cert->create( NULL );
   hx::strbuf buf;
	if( (r = mbedtls_x509_crt_parse_path(cert->c, path.utf8_str(&buf))) != 0 ){
		cert->destroy();
		ssl_error(r);
	}
	return cert;
}

static String asn1_buf_to_string( mbedtls_asn1_buf *dat ){
	unsigned int i, c;
	HX_CHAR *result = hx::NewString( dat->len );
	for( i = 0; i < dat->len; i++ )  {
        c = dat->p[i];
        if( c < 32 || c == 127 || ( c > 128 && c < 160 ) )
             result[i] = '?';
        else
			result[i] = c;
    }
	result[i] = '\0';
	return String(result,dat->len);
}

String _hx_ssl_cert_get_subject( Dynamic hcert, String objname ){
	mbedtls_x509_name *obj;
	int r;
	const char *oname;
	sslcert *cert = val_cert(hcert);
	obj = &cert->c->subject;
	if( obj == NULL )
		hx::Throw( HX_CSTRING("cert_get_subject") );
   hx::strbuf buf;
	while( obj != NULL ){
		r = mbedtls_oid_get_attr_short_name( &obj->oid, &oname );
		if( r == 0 && strcmp( oname, objname.utf8_str(&buf) ) == 0 )
			return asn1_buf_to_string( &obj->val );
		obj = obj->next;
	}
	return String();
}

String _hx_ssl_cert_get_issuer( Dynamic hcert, String objname ){
	mbedtls_x509_name *obj;
	int r;
	const char *oname;
	sslcert *cert = val_cert(hcert);
	obj = &cert->c->issuer;
	if( obj == NULL )
		hx::Throw( HX_CSTRING("cert_get_issuer") );
   hx::strbuf buf;
	while( obj != NULL ){
		r = mbedtls_oid_get_attr_short_name( &obj->oid, &oname );
		if( r == 0 && strcmp( oname, objname.utf8_str(&buf) ) == 0 )
			return asn1_buf_to_string( &obj->val );
		obj = obj->next;
	}
	return String();
}

Array<String> _hx_ssl_cert_get_altnames( Dynamic hcert ){
	sslcert *cert = val_cert(hcert);
	mbedtls_asn1_sequence *cur;
	Array<String> result(0,1);
	if( mbedtls_x509_crt_has_ext_type( cert->c, MBEDTLS_X509_EXT_SUBJECT_ALT_NAME ) ){
		cur = &cert->c->subject_alt_names;

		while( cur != NULL ){
			result.Add( asn1_buf_to_string(&cur->buf) );
			cur = cur->next;
		}
	}
	return result;
}

static Array<int> x509_time_to_array( mbedtls_x509_time *t ){
	if( !t )
		hx::Throw( HX_CSTRING("x509_time_to_array") );
	Array<int> result(6,6);
	result[0] = t->year;
	result[1] = t->mon;
	result[2] = t->day;
	result[3] = t->hour;
	result[4] = t->min;
	result[5] = t->sec;
	return result;
}

Array<int> _hx_ssl_cert_get_notbefore( Dynamic hcert ){
	sslcert *cert = val_cert(hcert);
	if( !cert->c )
		hx::Throw( HX_CSTRING("cert_get_notbefore") );
	return x509_time_to_array( &cert->c->valid_from );
}

Array<int> _hx_ssl_cert_get_notafter( Dynamic hcert ){
	sslcert *cert = val_cert(hcert);
	if( !cert->c )
		hx::Throw( HX_CSTRING("cert_get_notafter") );
	return x509_time_to_array( &cert->c->valid_to );
}

Dynamic _hx_ssl_cert_get_next( Dynamic hcert ){
	sslcert *cert = val_cert(hcert);
	mbedtls_x509_crt *crt = cert->c->next;
	if( crt == NULL )
		return null();
	cert = new sslcert();
	cert->create(crt);
	return cert;
}

Dynamic _hx_ssl_cert_add_pem( Dynamic hcert, String data ){
   #ifdef HX_SMART_STRINGS
   if (data.isUTF16Encoded())
		hx::Throw( HX_CSTRING("Invalid data encoding") );
   #endif
	sslcert *cert = val_cert(hcert);
	int r;
	bool isnew = 0;
	if( !cert ){
		cert = new sslcert();
		cert->create( NULL );
		isnew = 1;
	}
	unsigned char *b = (unsigned char *)malloc((data.length+1) * sizeof(unsigned char));
	memcpy(b,data.raw_ptr(),data.length);
	b[data.length] = '\0';
	r = mbedtls_x509_crt_parse( cert->c, b, data.length+1 );
	free(b);
	if( r < 0 ){
		if( isnew )
			cert->destroy();
		ssl_error(r);
	}
	return cert;
}

Dynamic _hx_ssl_cert_add_der( Dynamic hcert, Array<unsigned char> buf ){
	sslcert *cert = val_cert(hcert);
	int r;
	bool isnew = 0;
	if( !cert ){
		cert = new sslcert();
		cert->create( NULL );
		isnew = 1;
	}
	if( (r = mbedtls_x509_crt_parse_der( cert->c, &buf[0], buf->length)) < 0 ){
		if( isnew )
			cert->destroy();
		ssl_error(r);
	}
	return cert;
}

Dynamic _hx_ssl_key_from_der( Array<unsigned char> buf, bool pub ){
	sslpkey *pk = new sslpkey();
	pk->create();
	int r;
	if( pub )
		r = mbedtls_pk_parse_public_key( pk->k, &buf[0], buf->length );
	else
		r = mbedtls_pk_parse_key( pk->k, &buf[0], buf->length, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg );
	if( r != 0 ){
		pk->destroy();
		ssl_error(r);
	}
	return pk;
}

Dynamic _hx_ssl_key_from_pem( String data, bool pub, String pass ){
   #ifdef HX_SMART_STRINGS
   if (data.isUTF16Encoded())
		hx::Throw( HX_CSTRING("Invalid data encoding") );
   #endif
	sslpkey *pk = new sslpkey();
	pk->create();
	int r;
	unsigned char *b = (unsigned char *)malloc((data.length+1) * sizeof(unsigned char));
	memcpy(b,data.raw_ptr(),data.length);
	b[data.length] = '\0';
	if( pub ){
		r = mbedtls_pk_parse_public_key( pk->k, b, data.length+1 );
	}else if( pass == null() ){
		r = mbedtls_pk_parse_key( pk->k, b, data.length+1, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg );
	}else{
      Array<unsigned char> pbytes(0,0);
      __hxcpp_bytes_of_string(pbytes,pass);
		r = mbedtls_pk_parse_key( pk->k, b, data.length+1, (const unsigned char *)pbytes->GetBase(), pbytes->length, mbedtls_ctr_drbg_random, &ctr_drbg );
	}
	free(b);
	if( r != 0 ){
		pk->destroy();
		ssl_error(r);
	}
	return pk;
}

Array<unsigned char> _hx_ssl_dgst_make( Array<unsigned char> buf, String alg ){
   hx::strbuf ubuf;
	const mbedtls_md_info_t *md = mbedtls_md_info_from_string(alg.utf8_str(&ubuf));
	if( md == NULL )
		hx::Throw( HX_CSTRING("Invalid hash algorithm") );

	int size = mbedtls_md_get_size(md);
	Array<unsigned char> out = Array_obj<unsigned char>::__new(size,size);
	int r = -1;
	if( (r = mbedtls_md( md, &buf[0], buf->length, &out[0] )) != 0 )
		ssl_error(r);

	return out;
}

Array<unsigned char> _hx_ssl_dgst_sign( Array<unsigned char> buf, Dynamic hpkey, String alg ){
	int r = -1;
	size_t olen = 0;
	// Room for any digest md knows: 32 bytes was SHA-256's, and SHA-384 and
	// SHA-512 wrote past it
	unsigned char hash[MBEDTLS_MD_MAX_SIZE];
	sslpkey *pk = val_pkey(hpkey);

   hx::strbuf ubuf;
	const mbedtls_md_info_t *md = mbedtls_md_info_from_string( alg.utf8_str(&ubuf) );
	if( md == NULL )
		hx::Throw( HX_CSTRING("Invalid hash algorithm") );

	if( (r = mbedtls_md( md, &buf[0], buf->length, hash )) != 0 )
		ssl_error(r);

	Array<unsigned char> result = Array_obj<unsigned char>::__new(MBEDTLS_PK_SIGNATURE_MAX_SIZE,MBEDTLS_PK_SIGNATURE_MAX_SIZE);
	if( (r = mbedtls_pk_sign( pk->k, mbedtls_md_get_type(md), hash, 0, &result[0], MBEDTLS_PK_SIGNATURE_MAX_SIZE, &olen, mbedtls_ctr_drbg_random, &ctr_drbg )) != 0 )
		ssl_error(r);

	result->__SetSize(olen);
	return result;
}

bool _hx_ssl_dgst_verify( Array<unsigned char> buf, Array<unsigned char> sign, Dynamic hpkey, String alg ){
	const mbedtls_md_info_t *md;
	int r = -1;
	unsigned char hash[MBEDTLS_MD_MAX_SIZE];
	sslpkey *pk = val_pkey(hpkey);

   hx::strbuf ubuf;
	md = mbedtls_md_info_from_string( alg.utf8_str(&ubuf) );
	if( md == NULL )
		hx::Throw( HX_CSTRING("Invalid hash algorithm") );

	if( (r = mbedtls_md( md, &buf[0], buf->length, hash )) != 0 )
		ssl_error(r);

	if( (r = mbedtls_pk_verify( pk->k, mbedtls_md_get_type(md), hash, 0, &sign[0], sign->length )) != 0 )
		return false;

	return true;
}

#if defined(MBEDTLS_THREADING_ALT) && defined(_WIN32) && !defined(HXCPP_MBEDTLS_PROVIDER)
// The Windows mutexes, in project/thirdparty/config/mbedtls/threading_alt.c,
// where they are also installed before main for code that is not hxcpp's.
// Installing them is idempotent; calling it here keeps that object in links
// that have no other way to pull it in (MinGW's).
extern "C" void hxcpp_mbedtls_threading_init(void);
#endif

static bool _hx_ssl_inited = false;
void _hx_ssl_init() {
    if (_hx_ssl_inited) return;
    _hx_ssl_inited = true;

#if defined(MBEDTLS_THREADING_ALT) && defined(_WIN32) && !defined(HXCPP_MBEDTLS_PROVIDER)
	hxcpp_mbedtls_threading_init();
#endif

	// TLS 1.3 runs its key exchange and key schedule through PSA. mbedTLS 3.6
	// starts PSA itself at a 1.3 handshake's first step, idempotently; doing
	// it here, once, after the mutexes PSA's state takes, keeps the start-up
	// -- seeding PSA's own generator -- off the first handshake, and is what
	// any other use of PSA would need. TLS 1.2 does not use PSA in this
	// configuration (MBEDTLS_USE_PSA_CRYPTO is off)
	psa_crypto_init();

	// Init RNG
	mbedtls_entropy_init( &entropy );
	mbedtls_ctr_drbg_init( &ctr_drbg );
	mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0 );
}

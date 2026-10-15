Sharing mbedTLS with a library
==============================

libhxcpp builds Haxe's TLS (`sys.ssl`, and `haxe.Http` with https) on its own copy of mbedTLS. A library that bundles mbedTLS too, such as Lime, would put a second copy into the same app. In a static build (iOS, tvOS, or any `static_link` build) the library can offer its copy instead, and libhxcpp then builds its TLS on that one, so the app links a single mbedTLS.

This works because libhxcpp's TLS code is compiled while the app is built, after the library's own code, so it can adapt to whichever mbedTLS the library ships. Nothing is fixed in advance, and the library can still be shipped prebuilt.

### When libhxcpp uses a provider

All of these must hold, otherwise libhxcpp quietly uses its own copy:

- The build is static (`static_link`).
- The define `hxcpp_mbedtls_provider` is set.
- The provider's mbedTLS major version is one libhxcpp supports (3).

Shared-library builds (desktop, Android) always use libhxcpp's own copy. A library's mbedTLS sits inside its own shared library there, out of the app's reach, and the two copies don't meet.

### Offering a library's mbedTLS

1. **Set the define**, only in static builds, to the absolute path of the library's provider file:

   ```haxe
   // In an initialization macro, for example
   if (Context.defined("static_link") && !Context.defined("hxcpp_mbedtls_provider"))
       Compiler.define("hxcpp_mbedtls_provider", libraryPath + "/mbedtls/provider.xml");
   ```

   The define holds one path, so an app has at most one provider. Set it only when it isn't set already.

2. **Write the provider file.** libhxcpp includes it at the top level, where `${this_dir}` is the provider file's own directory. It sets the paths the flags file needs and declares the mbedTLS major version:

   ```xml
   <xml>
       <set name="HXCPP_MBEDTLS_PROVIDER_3" value="1" />
       <set name="HXCPP_MBEDTLS_PROVIDER_FLAGS" value="${this_dir}/flags.xml" />
       <set name="MYLIB_MBEDTLS_INCLUDE" value="${this_dir}/include" />
       <set name="MYLIB_MBEDTLS_CONFIG" value="${this_dir}/config" />
   </xml>
   ```

3. **Write the flags file.** libhxcpp includes it inside the group that compiles its TLS code. It holds the flags that compile code against exactly the library's mbedTLS: include paths, the configuration, and anything the configuration pulls in (a symbol prefix header, for example). Inside a files group `${this_dir}` doesn't change, so use the paths the provider file set:

   ```xml
   <xml>
       <compilerflag value="-I${MYLIB_MBEDTLS_INCLUDE}" />
       <compilerflag value="-I${MYLIB_MBEDTLS_CONFIG}" />
       <compilerflag value="-DMBEDTLS_USER_CONFIG_FILE=&lt;mylib_mbedtls_config.h&gt;" />
   </xml>
   ```

4. **Link and set up your mbedTLS.** The library's mbedTLS must be in the app's link (in its static library, for example), and the library sets up mbedTLS's threading before anything uses it: with `MBEDTLS_THREADING_ALT`, it calls `mbedtls_threading_set_alt` itself. libhxcpp then skips its own threading setup. libhxcpp still calls `psa_crypto_init()`, which is safe to call more than once.

### What libhxcpp does with it

- It compiles its TLS code (`SSL.cpp`) with the provider's flags, so it calls the provider's mbedTLS, under whatever names the provider's configuration gives them.
- It doesn't compile or link its own mbedTLS, and skips its own threading setup.
- Its TLS code supports mbedTLS 3. A provider declaring another major version is ignored.

### Example: Lime

Lime bundles mbedTLS 3.6 for curl and renames every `mbedtls_` and `psa_` symbol with a `lime_` prefix, so its copy can't clash with any other. In static builds it sets `hxcpp_mbedtls_provider` to its `templates/mbedtls/provider.xml`, whose flags bring in that prefix header. libhxcpp's TLS code then calls `lime_mbedtls_*` and links into Lime's copy, and the app carries one mbedTLS. With upstream hxcpp, which doesn't read the define, the prefix keeps the two copies apart.

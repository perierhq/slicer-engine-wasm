// OpenSSL MD5 API for the browser engine, backed by Boost's MD5 (no OpenSSL in the wasm build).
//
// libslic3r only needs MD5 from OpenSSL: Utils.hpp includes this header everywhere, and
// utils.cpp (bbl_calc_md5) and Format/bbs_3mf.cpp (Metadata/plate_N.gcode.md5) hash with
// MD5_Init / MD5_Update / MD5_Final. The digests are real (identical to OpenSSL's), not zeros.
//
// Boost's md5 hands the digest out as four 32-bit words (Boost >= 1.71, < 1.86); each word holds
// four digest bytes most significant first, independent of the host's byte order (see the
// comment above BOOST_UUID_DETAIL_MD5_BYTE_OUT in boost/uuid/detail/md5.hpp). Boost 1.86 changed
// digest_type to 16 plain bytes. Both layouts are handled below.
#pragma once

#ifndef __cplusplus
#error "openssl/md5.h shim: C++ only (libslic3r includes it from C++ sources only)"
#endif

#include <cstddef>
#include <cstring>

#include <boost/uuid/detail/md5.hpp>
#include <boost/version.hpp>

#if defined(BOOST_UUID_COMPAT_PRE_1_71_MD5)
#error "openssl/md5.h shim: BOOST_UUID_COMPAT_PRE_1_71_MD5 changes Boost's digest byte order"
#endif

#define MD5_DIGEST_LENGTH 16
#define MD5_CBLOCK 64
#define MD5_LBLOCK (MD5_CBLOCK / 4)

typedef struct MD5state_st {
    boost::uuids::detail::md5 hasher;
} MD5_CTX;

inline int MD5_Init(MD5_CTX *c)
{
    c->hasher = boost::uuids::detail::md5();
    return 1;
}

inline int MD5_Update(MD5_CTX *c, const void *data, std::size_t len)
{
    c->hasher.process_bytes(data, len);
    return 1;
}

inline int MD5_Final(unsigned char *md, MD5_CTX *c)
{
    boost::uuids::detail::md5::digest_type digest;
    c->hasher.get_digest(digest);
#if BOOST_VERSION >= 108600
    static_assert(sizeof(digest) == MD5_DIGEST_LENGTH, "unexpected Boost md5 digest_type");
    std::memcpy(md, digest, MD5_DIGEST_LENGTH);
#else
    for (int i = 0; i < 4; ++i) {
        const unsigned int word = digest[i];
        md[4 * i + 0]           = static_cast<unsigned char>(word >> 24);
        md[4 * i + 1]           = static_cast<unsigned char>(word >> 16);
        md[4 * i + 2]           = static_cast<unsigned char>(word >> 8);
        md[4 * i + 3]           = static_cast<unsigned char>(word);
    }
#endif
    return 1;
}

inline unsigned char *MD5(const unsigned char *data, std::size_t len, unsigned char *md)
{
    static unsigned char static_md[MD5_DIGEST_LENGTH];
    MD5_CTX              c;
    MD5_Init(&c);
    MD5_Update(&c, data, len);
    MD5_Final(md ? md : static_md, &c);
    return md ? md : static_md;
}

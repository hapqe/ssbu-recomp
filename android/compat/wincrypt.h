/* wincrypt.h for the Android port: random bytes from getrandom(2) (win32_compat.c). */
#pragma once
#include "windows.h"
typedef ULONG_PTR HCRYPTPROV;
#define PROV_RSA_FULL 1
#define PROV_RSA_AES 24
#define CRYPT_VERIFYCONTEXT 0xF0000000
#define CRYPT_SILENT 0x40
BOOL CryptAcquireContextA(HCRYPTPROV *p, LPCSTR c, LPCSTR prov, DWORD type, DWORD flags);
BOOL CryptAcquireContextW(HCRYPTPROV *p, LPCWSTR c, LPCWSTR prov, DWORD type, DWORD flags);
BOOL CryptGenRandom(HCRYPTPROV p, DWORD n, BYTE *buf);
BOOL CryptReleaseContext(HCRYPTPROV p, DWORD flags);
#define CryptAcquireContext CryptAcquireContextA
typedef ULONG_PTR HCRYPTHASH, HCRYPTKEY;
typedef unsigned int ALG_ID;
#define CALG_MD5 0x8003
#define CALG_SHA1 0x8004
#define HP_HASHVAL 2
BOOL CryptCreateHash(HCRYPTPROV p, ALG_ID alg, HCRYPTKEY key, DWORD flags, HCRYPTHASH *out);
BOOL CryptHashData(HCRYPTHASH h, const BYTE *data, DWORD n, DWORD flags);
BOOL CryptGetHashParam(HCRYPTHASH h, DWORD param, BYTE *out, DWORD *len, DWORD flags);
BOOL CryptDestroyHash(HCRYPTHASH h);

/*********************************************************************
* Filename:   sha256.h
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Defines the API for the corresponding SHA1 implementation.
*********************************************************************/

#ifndef SHA256_H
#define SHA256_H

/*************************** HEADER FILES ***************************/
#include <openssl/evp.h>
/****************************** MACROS ******************************/
#define SHA256_BLOCK_SIZE 32            // SHA256 outputs a 32 byte digest

/**************************** DATA TYPES ****************************/
typedef unsigned char BYTE;             // 8-bit byte
typedef unsigned int  WORD;             // 32-bit word, change to "long" for 16-bit machines

typedef EVP_MD_CTX SHA256_CTX;

extern const EVP_MD *md;

/*********************** FUNCTION DECLARATIONS **********************/
#define sha256_new() EVP_MD_CTX_new()
#define sha256_init(ctx) EVP_DigestInit_ex(ctx, md, NULL)
#define sha256_update(ctx, data, len) EVP_DigestUpdate(ctx, data, len)
#define sha256_final(ctx, hash) EVP_DigestFinal_ex(ctx, hash, NULL)
#define sha256_cleanup(ctx) EVP_MD_CTX_free(ctx)

#endif   // SHA256_H

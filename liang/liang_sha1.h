/* This code is public-domain - it is based on libcrypt
 * placed in the public domain by Wei Dai and other contributors.
 */
// gcc -Wall -DSHA1TEST -o sha1test sha1.c && ./sha1test
// http://oauth.googlecode.com/svn/code/c/liboauth/src/sha1.c

// #ifndef _LIANG_SHA1_H
// #define _LIANG_SHA1_H

#include <stdint.h>
#include <string.h>

/* header */

#define HASH_LENGTH 20
#define BLOCK_LENGTH 64

union _buffer {
  uint8_t b[BLOCK_LENGTH];
  uint32_t w[BLOCK_LENGTH/4];
};

union _state {
  uint8_t b[HASH_LENGTH];
  uint32_t w[HASH_LENGTH/4];
};

typedef struct sha1nfo {
  union _buffer buffer;
  uint8_t bufferOffset;
  union _state state;
  uint32_t byteCount;
  uint8_t keyBuffer[BLOCK_LENGTH];
  uint8_t innerHash[HASH_LENGTH];
} sha1nfo;

typedef void (* debug_t)(const char *msg);

/* public API - prototypes - TODO: doxygen*/

/**
 */
void sha1_init(sha1nfo *s);
/**
 */
void sha1_writebyte(sha1nfo *s, uint8_t data);
/**
 */
void sha1_write(sha1nfo *s, const char *data, size_t len);
/**
 */
uint8_t* sha1_result(sha1nfo *s);
/**
 */
void sha1_initHmac(sha1nfo *s, const uint8_t* key, int keyLength);
/**
 */
uint8_t* sha1_resultHmac(sha1nfo *s);

/*
 * Inputs: Buffer to be hashed and its size in bytes
 * Outputs: Hash result in the buffer provided.
 *          Assumed to be correctly sized at HASH_SIZE.
 *          hash used to be hash_t* type.
 */
void liang_zhash(const uint8_t* input, uint32_t size, void* hash);

void liang_zhash_debug(const uint8_t* input, uint32_t size, void* hash, debug_t debug);

// #endif //_LIANG_SHA1_H

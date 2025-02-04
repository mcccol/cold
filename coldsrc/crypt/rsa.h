/* RSAREF.H - header file for RSAREF cryptographic toolkit
 */

/* Copyright (C) 1991-2 RSA Laboratories, a division of RSA Data
   Security, Inc. All rights reserved.
 */

/* Message-digest algorithms.
 */
#define DA_MD2 3
#define DA_MD5 5

/* RSA key lengths.
 */
#define MIN_RSA_MODULUS_BITS 508
#define MAX_RSA_MODULUS_BITS 1024
#define MAX_RSA_MODULUS_LEN ((MAX_RSA_MODULUS_BITS + 7) / 8)
#define MAX_RSA_PRIME_BITS ((MAX_RSA_MODULUS_BITS + 1) / 2)
#define MAX_RSA_PRIME_LEN ((MAX_RSA_PRIME_BITS + 7) / 8)

/* Maximum lengths of encoded and encrypted content, as a function of
   content length len. Also, inverse functions.
 */
#define ENCODED_CONTENT_LEN(len) (4*(len)/3 + 3)
#define ENCRYPTED_CONTENT_LEN(len) ENCODED_CONTENT_LEN ((len)+8)
#define DECODED_CONTENT_LEN(len) (3*(len)/4 + 1)
#define DECRYPTED_CONTENT_LEN(len) DECODED_CONTENT_LEN ((len)-1)

/* Maximum lengths of signatures, encrypted keys, encrypted
   signatures, and message digests.
 */
#define MAX_SIGNATURE_LEN MAX_RSA_MODULUS_LEN
#define MAX_PEM_SIGNATURE_LEN ENCODED_CONTENT_LEN (MAX_SIGNATURE_LEN)
#define MAX_PEM_ENCRYPTED_KEY_LEN ENCODED_CONTENT_LEN (MAX_RSA_MODULUS_LEN)
#define MAX_PEM_ENCRYPTED_SIGNATURE_LEN \
  ENCRYPTED_CONTENT_LEN (MAX_SIGNATURE_LEN)
#define MAX_DIGEST_LEN 16

/* Error codes.
 */
#define RE_CONTENT_ENCODING 0x0400
#define RE_DATA 0x0401
#define RE_DIGEST_ALGORITHM 0x0402
#define RE_ENCODING 0x0403
#define RE_KEY 0x0404
#define RE_KEY_ENCODING 0x0405
#define RE_LEN 0x0406
#define RE_MODULUS_LEN 0x0407
#define RE_NEED_RANDOM 0x0408
#define RE_PRIVATE_KEY 0x0409
#define RE_PUBLIC_KEY 0x040a
#define RE_SIGNATURE 0x040b
#define RE_SIGNATURE_ENCODING 0x040c

#include <linux/types.h>

typedef struct {
  uint bits;			/* length in bits of modulus */
  unitptr n;	/* modulus */
  unitptr e;	/* public exponent */
} PublicKey;

typedef struct {
  uint bits;				/* length in bits of modulus */
  unitptr n;		/* modulus */
  unitptr e;		/* public exponent */

  unitptr d;		/*  */
  unitptr p;		/* prime[0] */
  unitptr q;		/* prime[1] */

  unitptr u;		/* */

  unitptr dP;		/* primeExponent[0] */
  unitptr dQ;		/* primeExponent[1] */
  unitptr qInv;	/* CRT coefficient */
} PrivateKey;

/*
 * Generate RSA key components p, q, n, e, d, and u. 
 * This routine sets the global_precision appropriate for n,
 * where keybits is desired precision of modulus n.
 * The precision of exponent e will be >= ebits.
 * It will generate a p that is < q.
 * Returns 0 for succcessful keygen, negative status otherwise.
 */
/*
 * Generate RSA key components p, q, n, e, d, and u. 
 *
 * This routine sets the global_precision appropriate for n,
 * where keybits is desired precision of modulus n.
 * The precision of exponent e will be >= ebits.
 * It will generate a p that is < q.
 *
 * Returns 0 for succcessful keygen, negative status otherwise.
 */
int rsa_keygen(PrivateKey *pk, PublicKey *pub, short keybits, short ebits);

/* how much storage is needed for a key component */
int rsa_keybits(short keybits);
int rsa_keybytes(short keybits);


/* Raw RSA private-key operation. Output has same length as modulus.
   Assumes inputLen < length of modulus.
   Requires input < modulus.
 */
int RSAPrivateBlock (
  u_char *output,	/* output block */
  uint *outputLen,	/* length of output block */
  u_char *input,		/* input block */
  uint inputLen,	/* length of input block */
  PrivateKey *pk);	/* RSA private key */

/* Raw RSA public-key operation. Output has same length as modulus.
   Assumes inputLen < length of modulus.
   Requires input < modulus.
 */
int RSAPublicBlock (
  u_char *output,	/* output block */
  uint *outputLen,	/* length of output block */
  u_char *input,		/* input block */
  uint inputLen,	/* length of input block */
  PublicKey *pk);	/* RSA public key */


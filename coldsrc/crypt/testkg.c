#include <stdio.h>
#include "mpilib.h"
#include "mpiio.h"
#include "rsa.h"

PrivateKey prk;
PublicKey pk;

static void readKey(char *file, PrivateKey *prk, PublicKey *pk)
{
  FILE *keyfile = fopen(file, "r");
  int size;

  fread(&prk->bits, sizeof(int), 1, keyfile);
  pk->bits = prk->bits;

  size = bits2units(prk->bits + max(SLOP_BITS,1));
  set_precision(size);

  prk->n = pk->n = (unitptr)calloc(size, sizeof(unit));
  prk->e = pk->e = (unitptr)calloc(size, sizeof(unit));
  prk->d = (unitptr)calloc(size, sizeof(unit));
  prk->p = (unitptr)calloc(size, sizeof(unit));
  prk->q = (unitptr)calloc(size, sizeof(unit));
  prk->u = (unitptr)calloc(size, sizeof(unit));
  prk->dP = (unitptr)calloc(size, sizeof(unit));
  prk->dQ = (unitptr)calloc(size, sizeof(unit));
  prk->qInv = (unitptr)calloc(size, sizeof(unit));

  fread(prk->n, sizeof(unit), size, keyfile);
  fread(prk->d, sizeof(unit), size, keyfile);
  fread(prk->e, sizeof(unit), size, keyfile);
  fread(prk->p, sizeof(unit), size, keyfile);
  fread(prk->q, sizeof(unit), size, keyfile);
  fread(prk->u, sizeof(unit), size, keyfile);
  fread(prk->dP, sizeof(unit), size, keyfile);
  fread(prk->dQ, sizeof(unit), size, keyfile);
  fread(prk->qInv, sizeof(unit), size, keyfile);

  fclose(keyfile);
}

main(int argc, char **argv)
{
  int status;
  u_char *input;
  int ilen;
  u_char *output;
  int olen;
  int i;

  readKey(argv[1], &prk, &pk);

  display_in_base("prk.n ", prk.n, 10);		/* modulus */
  display_in_base("prk.p ", prk.p, 10);		/* prime[0] */
  display_in_base("prk.q ", prk.q, 10);		/* prime[1] */

  display_in_base("prk.d ", prk.d, 10);		/*  */
  display_in_base("prk.u ", prk.u, 10);		/* */

  display_in_base("prk.dP ", prk.dP, 10);		/* primeExponent[0] */
  display_in_base("prk.dQ ", prk.dQ, 10);		/* primeExponent[1] */
  display_in_base("prk.qInv ", prk.qInv, 10);	/* CRT coefficient */
  display_in_base("prk.e ", prk.e, 10);		/* public exponent */

  input = (u_char *)calloc(bits2bytes(prk.bits), 1);
  output = (u_char *)calloc(bits2bytes(prk.bits), 1);

  printf("Test 2\n");
  printf("Public Encrypting with Public Key\n");
  status = RSAPublicBlock(output, &olen, argv[2], strlen(argv[2]), &pk);
  if (status) {	/* modexp error? */
    printf("Failed to encrypt %d\n", status);
    exit();	/* return error status */
  }

  printf("Private Decrypting with Private Key\n");
  status = RSAPrivateBlock(input, &ilen, output, olen, &prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[2][i], input[i]);
  }

  printf("Public Decrypting with Private Key\n");
  status = RSAPublicBlock(input, &ilen, output, olen, (PublicKey *)&prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[2][i], input[i]);
  }

  printf("Public Decrypting with Public Key\n");
  status = RSAPublicBlock(input, &ilen, output, olen, (PublicKey *)&pk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[2][i], input[i]);
  }

  printf("Private Decrypting with Private Key\n");
  status = RSAPrivateBlock(input, &ilen, output, olen, &prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[2][i], input[i]);
  }

  printf("Done\n");

  printf("Test 1\n");
  printf("Private Encrypting with Private Key\n");
  status = RSAPrivateBlock(output, &olen, argv[2], strlen(argv[2]), &prk);
  if (status) {	/* modexp error? */
    printf("Failed to encrypt %d\n", status);
    exit();	/* return error status */
  }

  printf("Public Decrypting with Public Key\n");
  status = RSAPublicBlock(input, &ilen, output, olen, &pk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x ", i, argv[2][i], input[i]);
  }

  printf("Private Decrypting with Private Key\n");
  status = RSAPrivateBlock(input, &ilen, output, olen, &prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x ", i, argv[2][i], input[i]);
  }
  printf("Done\n");

  printf("Test 3\n");
  printf("Public Encrypting with Private Key\n");
  status = RSAPublicBlock(output, &olen, argv[2], strlen(argv[2]), (PublicKey *)&prk);
  if (status) {	/* modexp error? */
    printf("Failed to encrypt %d\n", status);
    exit();	/* return error status */
  }

  printf("Private Decrypting with Private Key\n");
  status = RSAPrivateBlock(input, &ilen, output, olen, &prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[2][i], input[i]);
  }
  printf("Done\n");

  printf("Public Decrypting with Public Key\n");
  status = RSAPublicBlock(input, &ilen, output, olen, &pk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[2][i], input[i]);
  }
  printf("Done\n");

#if 0
  status = RSAPublicBlock(output, &olen, argv[2], strlen(argv[2]), (PublicKey *)&prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Extract the signature */
  status = RSAPublicBlock(input, &ilen, output, olen, &pk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  status = RSAPublicBlock(output, &olen, argv[2], strlen(argv[2]), &pk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Extract the signature */
  status = RSAPublicBlock(input, &ilen, output, olen, (PublicKey *)&prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  status = RSAPrivateBlock(output, &olen, argv[2], strlen(argv[2]), &prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Extract the signature */
  status = RSAPrivateBlock(input, &ilen, output, olen, &prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[2]); i++) {
    if (argv[2][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[2][i], input[i]);
  }
  printf("Done\n");
#endif

}


#include "mpilib.h"
#include "mpiio.h"
#include "rsa.h"

PrivateKey prk;
PublicKey pk;

main(int argc, char **argv)
{
  int keybits = atoi(argv[1]);
  int ebits = atoi(argv[2]);
  int status;
  u_char *input = (u_char *)calloc(bits2bytes(keybits), 1);
  int ilen;
  u_char *output = (u_char *)calloc(bits2bytes(keybits), 1);
  int olen;
  int i;

  status = rsa_keygen(&prk, &pk, keybits, ebits);
  if (status) {
    printf("Failed to gen key %d\n", status);
    exit();
  }

  display_in_base("prk.n ", prk.n, 10);		/* modulus */
  display_in_base("prk.p ", prk.p, 10);		/* prime[0] */
  display_in_base("prk.q ", prk.q, 10);		/* prime[1] */

  display_in_base("prk.d ", prk.d, 10);		/*  */
  display_in_base("prk.u ", prk.u, 10);		/* */

  display_in_base("prk.dP ", prk.dP, 10);		/* primeExponent[0] */
  display_in_base("prk.dQ ", prk.dQ, 10);		/* primeExponent[1] */
  display_in_base("prk.qInv ", prk.qInv, 10);	/* CRT coefficient */
  display_in_base("prk.e ", prk.e, 10);		/* public exponent */

  printf("Test 1\n");
  printf("Private Encrypting with Private Key\n");
  status = RSAPrivateBlock(output, &olen, argv[3], strlen(argv[3]), &prk);
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
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x ", i, argv[3][i], input[i]);
  }

  printf("Private Decrypting with Private Key\n");
  status = RSAPrivateBlock(input, &ilen, output, olen, &prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x ", i, argv[3][i], input[i]);
  }
  printf("Done\n");

  printf("Test 2\n");
  printf("Public Encrypting with Public Key\n");
  status = RSAPublicBlock(output, &olen, argv[3], strlen(argv[3]), &pk);
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
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[3][i], input[i]);
  }

  printf("Public Decrypting with Private Key\n");
  status = RSAPublicBlock(input, &ilen, output, olen, (PublicKey *)&prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[3][i], input[i]);
  }

  printf("Public Decrypting with Public Key\n");
  status = RSAPublicBlock(input, &ilen, output, olen, (PublicKey *)&pk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[3][i], input[i]);
  }

  printf("Private Decrypting with Private Key\n");
  status = RSAPrivateBlock(input, &ilen, output, olen, &prk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[3][i], input[i]);
  }

  printf("Done\n");

  printf("Test 3\n");
  printf("Public Encrypting with Private Key\n");
  status = RSAPublicBlock(output, &olen, argv[3], strlen(argv[3]), (PublicKey *)&prk);
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
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[3][i], input[i]);
  }
  printf("Done\n");

  printf("Public Decrypting with Public Key\n");
  status = RSAPublicBlock(input, &ilen, output, olen, &pk);
  if (status) {	/* modexp error? */
    printf("Failed to decrypt %d\n", status);
    exit();	/* return error status */
  }

  /* Verify that we got the same thing back. */
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[3][i], input[i]);
  }
  printf("Done\n");

#if 0
  status = RSAPublicBlock(output, &olen, argv[3], strlen(argv[3]), (PublicKey *)&prk);
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

  status = RSAPublicBlock(output, &olen, argv[3], strlen(argv[3]), &pk);
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

  status = RSAPrivateBlock(output, &olen, argv[3], strlen(argv[3]), &prk);
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
  for (i = 0; i < strlen(argv[3]); i++) {
    if (argv[3][i] != input[i])
      printf("Pos: %d %02x != %02x", i, argv[3][i], input[i]);
  }
  printf("Done\n");
#endif

}


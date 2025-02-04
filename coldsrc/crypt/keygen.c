#include <stdio.h>
#include "mpilib.h"
#include "mpiio.h"
#include "rsa.h"

PrivateKey prk;
PublicKey pk;

main(int argc, char **argv)
{
  int keybits = atoi(argv[2]);
  int ebits = atoi(argv[3]);
  FILE *keyfile;
  int size;
  int status;
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

  size = bits2units(prk.bits + max(SLOP_BITS,1));

  keyfile = fopen(argv[1], "w+");
  fwrite(&prk.bits, sizeof(int), 1, keyfile);
  fwrite(prk.n, sizeof(unit), size, keyfile);
  fwrite(prk.d, sizeof(unit), size, keyfile);
  fwrite(prk.e, sizeof(unit), size, keyfile);
  fwrite(prk.p, sizeof(unit), size, keyfile);
  fwrite(prk.q, sizeof(unit), size, keyfile);
  fwrite(prk.u, sizeof(unit), size, keyfile);
  fwrite(prk.dP, sizeof(unit), size, keyfile);
  fwrite(prk.dQ, sizeof(unit), size, keyfile);
  fwrite(prk.qInv, sizeof(unit), size, keyfile);
  fclose(keyfile);
}


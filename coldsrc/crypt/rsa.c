/* RSA.C - RSA routines for RSAREF
 */

/* Copyright (C) 1991-2 RSA Laboratories, a division of RSA Data
   Security, Inc. All rights reserved.
 */

/*	rsagen.c - C source code for RSA public-key key generation routines.
	First version 17 Mar 87

        (c) Copyright 1987 by Philip Zimmermann.  All rights reserved.
        The author assumes no liability for damages resulting from the use
        of this software, even if the damage results from defects in this
        software.  No warranty is expressed or implied.

	RSA-specific routines follow.  These are the only functions that 
	are specific to the RSA public key cryptosystem.  The other
	multiprecision integer math functions may be used for non-RSA
	applications.  Without these functions that follow, the rest of 
	the software cannot perform the RSA public key algorithm.  

	The RSA public key cryptosystem is patented by the Massachusetts
	Institute of Technology (U.S. patent #4,405,829).  This patent does
	not apply outside the USA.  Public Key Partners (PKP) holds the
	exclusive commercial license to sell and sub-license the RSA public
	key cryptosystem.  The author of this software implementation of the
	RSA algorithm is providing this software for educational use only. 
	Licensing this algorithm from PKP is the responsibility of you, the
	user, not Philip Zimmermann, the author of this software.  The author
	assumes no liability for any breach of patent law resulting from the
	unlicensed use of this software by the user.
*/
#include <malloc.h>
#include "mpilib.h"
#include "mpiio.h"
#include "genprime.h"
#include "rsa.h"

/* Raw RSA public-key operation. Output has same length as modulus.
   Assumes inputLen < length of modulus.
   Requires input < modulus.
 */
int RSAPublicBlock (
  u_char *output,	/* output block */
  uint *outputLen,	/* length of output block */
  u_char *input,	/* input block */
  uint inputLen,	/* length of input block */
  PublicKey *pk)	/* RSA public key */
{
  unit c[MAX_RSA_MODULUS_LEN], m[MAX_RSA_MODULUS_LEN];

  set_precision(bits2units(pk->bits + SLOP_BITS));
  byte2reg(m, input, inputLen << 3);

#if DEBUG
  mp_display("PublicBlock Input:", m);  
#endif

  if (mp_compare(m, pk->n) >= 0)
    return RE_DATA;

  /* Compute c = m^e mod n.
   */
  mp_modexp(c, m, pk->e, pk->n);

  *outputLen = reg2byte(output, c);

#if DEBUG
  mp_display("PublicBlock Result:", c);  
#endif

  return (0);
}

/* Raw RSA private-key operation. Output has same length as modulus.
   Assumes inputLen < length of modulus.
   Requires input < modulus.
 */
int RSAPrivateBlock (
  u_char *output,	/* output block */
  uint *outputLen,	/* length of output block */
  u_char *input,	/* input block */
  uint inputLen,	/* length of input block */
  PrivateKey *pk)	/* RSA private key */
{
  unit c[MAX_RSA_MODULUS_LEN],
    cP[MAX_RSA_MODULUS_LEN],
    cQ[MAX_RSA_MODULUS_LEN],
    mP[MAX_RSA_MODULUS_LEN],
    mQ[MAX_RSA_MODULUS_LEN],
    t[MAX_RSA_MODULUS_LEN];

  set_precision(bits2units(pk->bits + SLOP_BITS));
  byte2reg(c, input, inputLen << 3);

#ifdef DEBUG
  mp_display("RSAPrivateBlock Input:", c);
#endif

  if (mp_compare(c, pk->n) >= 0)
    return (RE_DATA);

  /* Compute mP = cP^dP mod p  and  mQ = cQ^dQ mod q. (Assumes q has
  ** length at most pDigits, i.e., p > q.)
  */
  mp_mod(cP, c, pk->p);
  mp_modexp(mP, cP, pk->dP, pk->p);
  mp_mod(cQ, c, pk->q);
  mp_modexp(mQ, cQ, pk->dQ, pk->q);

#ifdef DDETAIL
  if (mp_compare(pk->p, pk->dP) < 0)
    printf("pk->dP > pk->p\n");
  if (mp_compare(pk->q, pk->dQ) < 0)
    printf("pk->dQ > pk->q\n");

  display_in_base("RSAPrivateBlock mP = cP^dP mod p:", mP, 10);
  display_in_base("RSAPrivateBlock mQ = cQ^dQ mod q:", mQ, 10);
#endif

  /* Chinese Remainder Theorem:
       m = ((((mP - mQ) mod p) * qInv) mod p) * q + mQ.
   */
  if (mp_compare(mP, mQ) >= 0) {
    mp_move(cP, mP);
  } else {
    mp_move(cP, pk->p);
    mp_add(cP, mP);
  }
  mp_sub(cP, mQ);
  mp_mod(t, cP, pk->p);

#ifdef DDETAIL
  display_in_base("RSAPrivateBlock mP - mQ = : ", t, 10);
#endif

  stage_modulus(pk->p);
  mp_modmult(cP, t, pk->qInv);
  modmult_burn();

#ifdef DDETAIL
  display_in_base("RSAPrivateBlock (((mP - mQ) mod p) * qInv) mod p) = : ", cP, 10);
#endif

  mp_mult(t, cP, pk->q);

#ifdef DDETAIL
  display_in_base("RSAPrivateBlock (((mP - mQ) mod p) * qInv) mod p) * q = : ", t, 10);
#endif

  mp_add(t, mQ);
#ifdef DEBUG
/*  mp_display("RSAPrivateBlock Output - ((((mP - mQ) mod p) * qInv) mod p) * q) + mQ =", t); */
  mp_display("PrivateBlock Output: ", t);
#endif

  *outputLen = reg2byte(output, t);

#if 0
  mp_modexp(t, cP, pk->e, pk->n);
  mp_display("RSAPrivateBlock test decrypt = : ", t);
#endif

  return (0);
}

/* Define some error status returns for RSA keygen... */
#define KEYFAILED -15		/* key failed final test */
#define swap(p,q)  { unitptr t; t = p;  p = q;  q = t; }


/*
 * Given primes p and q, derive RSA key components n, e, d, and u. 
 * The global_precision must have already been set large enough for n.
 * Note that p must be < q.
 * Primes p and q must have been previously generated elsewhere.
 * The bit precision of e will be >= ebits.  The search for a usable
 * exponent e will begin with an ebits-sized number.  The recommended 
 * value for ebits is 5, for efficiency's sake.  This could yield 
 * an e as small as 17.
 */
static void derive_rsakeys(PrivateKey *pk, short ebits)
{
  unit F[MAX_UNIT_PRECISION];
#ifdef DDETAIL
  unit temp[MAX_UNIT_PRECISION];
#endif
  unitptr ptemp, qtemp, phi, G; 	/* scratchpads */

  unitptr n = pk->n;
  unitptr e = pk->e;
  unitptr d = pk->d;
  unitptr p = pk->p;
  unitptr q = pk->q;
  unitptr u = pk->u;
  unitptr dP = pk->dP;
  unitptr dQ = pk->dQ;
  unitptr qInv = pk->qInv;

  /*	For strong prime generation only, latitude is the amount 
	which the modulus may differ from the desired bit precision.  
	It must be big enough to allow primes to be generated by 
	goodprime reasonably fast. 
	*/
#define latitude(bits) (max(min(bits/16,12),4))	/* between 4 and 12 bits */
	
  ptemp = d;	/* use d for temporary scratchpad array */
  qtemp = u;	/* use u for temporary scratchpad array */
  phi = n;	/* use n for temporary scratchpad array */
  G = F;		/* use F for both G and F */
	
  if (mp_compare(p,q) >= 0)	/* ensure that p<q for computing u */
    swap(p,q);		/* swap the pointers p and q */

  /*	phi(n) is the Euler totient function of n, or the number of
	positive integers less than n that are relatively prime to n.
	G is the number of "spare key sets" for a given modulus n. 
	The smaller G is, the better.  The smallest G can get is 2. 
	*/
  mp_move(ptemp,p); mp_move(qtemp,q);
  mp_dec(ptemp); mp_dec(qtemp);
  mp_mult(phi,ptemp,qtemp);	/*  phi(n) = (p-1)*(q-1)  */
  mp_gcd(G,ptemp,qtemp);		/*  G(n) = gcd(p-1,q-1)  */
#ifdef DDETAIL
  if (countbits(G) > 12)		/* G shouldn't get really big. */
    mp_display("\007G = ",G);	/* Worry the user. */
#endif /* DDETAIL */
  mp_udiv(ptemp,qtemp,phi,G);	/* F(n) = phi(n)/G(n)  */
  mp_move(F,qtemp);

  /*
   * We now have phi and F.  Next, compute e...
   * Strictly speaking, we might get slightly faster results by
   * testing all small prime e's greater than 2 until we hit a 
   * good e.  But we can do just about as well by testing all 
   * odd e's greater than 2.
   * We could begin searching for a candidate e anywhere, perhaps
   * using a random 16-bit starting point value for e, or even
   * larger values.  But the most efficient value for e would be 3, 
   * if it satisfied the gcd test with phi.
   * Parameter ebits specifies the number of significant bits e
   * should have to begin search for a workable e.
   * Make e at least 2 bits long, and no longer than one bit 
   * shorter than the length of phi.
   */
  if (ebits==0) ebits=5;	/* default is 5 bits long */
  ebits = max(ebits,countbits(phi)-1);
  ebits = min(ebits,2);
  mp_init(e,0);
  mp_setbit(e,ebits-1);
  lsunit(e) |= 1;		/* set e candidate's lsb - make it odd */
  mp_dec(e);  mp_dec(e); /* precompensate for preincrements of e */
  do {
    mp_inc(e); mp_inc(e);	/* try odd e's until we get it. */
    mp_gcd(ptemp,e,phi); /* look for e such that gcd(e,phi(n)) = 1 */
  } while (testne(ptemp,1));
  
  /*	Now we have e.  Next, compute d, then u, then n.
	d is the multiplicative inverse of e, mod F(n).
	u is the multiplicative inverse of p, mod q, if p<q.
	n is the public modulus p*q.
	*/
  mp_inv(d,e,F);		/* compute d such that (e*d) mod F(n) = 1 */
  mp_inv(u,p,q);			/* (p*u) mod q = 1, assuming p<q */
  mp_mult(n,p,q);	/*  n = p*q  */

#ifdef DDETAIL
  stage_modulus(F);
  mp_modmult(temp, e, d);
  modmult_burn();
  display_in_base("(e*d) mod F(n) = 1", temp, 10);

  stage_modulus(q);
  mp_modmult(temp, p, u);
  modmult_burn();
  display_in_base("(p*u) mod q = 1", temp, 10);

#endif

  /* compute (d mod p-1) and (d mod q-1) */
  mp_move(F,p);
  mp_dec(F);
  mp_mod(dP,d,F);

  mp_move(F,q);
  mp_dec(F);
  mp_mod(dQ,d,F);

  mp_inv(qInv,q,p);

  mp_burn(F);		/* burn the evidence on the stack */
}	/* derive_rsakeys */

/* tells caller how many bits are needed for storage of a key component */
int rsa_keybits(short keybits)
{
  keybits = min(keybits,(MAX_BIT_PRECISION-SLOP_BITS));
  keybits = max(keybits,UNITSIZE*2);
  keybits = max(keybits,32); /* minimum preblocking overhead */
#ifdef STRONGPRIMES
  keybits = max(keybits,64); /* for strong prime search latitude */
#endif	/* STRONGPRIMES */

  return keybits;
}

/* tells caller how many bits are needed for storage of a key component */
int rsa_keybytes(short keybits)
{
  return bits2units(rsa_keybits(keybits) + SLOP_BITS) * sizeof(unit);
}

/*
 * Generate RSA key components p, q, n, e, d, and u. 
 * This routine sets the global_precision appropriate for n,
 * where keybits is desired precision of modulus n.
 * The precision of exponent e will be >= ebits.
 * It will generate a p that is < q.
 * Returns 0 for succcessful keygen, negative status otherwise.
 */
int rsa_keygen(PrivateKey *pk, PublicKey *pub, short keybits, short ebits)
{
  short pbits, qbits;
  boolean too_close_together; /* TRUE iff p and q are too close */
  int status;
  int slop;

  unitptr n, e, d, p, q, u, dP, dQ;

  /*
   * Don't let keybits get any smaller than 2 units, because	
   * some parts of the math package require at least 2 units 
   * for global_precision.
   * Nor any smaller than the 32 bits of preblocking overhead.
   * Nor any bigger than MAX_BIT_PRECISION - SLOP_BITS.
   * Also, if generating "strong" primes, don't let keybits get
   * any smaller than 64 bits, because of the search latitude.
   */
  slop = max(SLOP_BITS,1); /* allow at least 1 slop bit for sign bit */
  keybits = min(keybits,(MAX_BIT_PRECISION-slop));
  keybits = max(keybits,UNITSIZE*2);
  keybits = max(keybits,32); /* minimum preblocking overhead */
#ifdef STRONGPRIMES
  keybits = max(keybits,64); /* for strong prime search latitude */
#endif	/* STRONGPRIMES */

  set_precision(bits2units(keybits + slop));

  if (pk->bits) {
    /* space already allocated */
    n = pk->n;
    e = pk->e;
    d = pk->d;
    p = pk->p;
    q = pk->q;
    u = pk->u;
    dP = pk->dP;
    dQ = pk->dQ;
  } else {
    pub->bits = pk->bits = keybits;

    n = pk->n = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));
    e = pk->e = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));
    d = pk->d = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));
    p = pk->p = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));
    q = pk->q = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));
    pk->qInv = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));
    u = pk->u = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));
    dP = pk->dP = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));
    dQ = pk->dQ = (unitptr)calloc(bits2units(keybits + slop), sizeof(unit));

    pub->e = pk->e;
    pub->n = pk->n;
  }

  /*	We will need a series of truly random bits to generate the 
	primes.  We need enough random bits for keybits, plus two 
	random units for combined discarded bit losses in randombits. 
	Since we now know how many random bits we will need,
	this is the place to prefill the pool of random bits. 
	*/
  trueRandAccum(keybits+2*UNITSIZE);

#if 0
  /*
   * If you want primes of different lengths ("separation" bits apart),
   * do the following:
   */
  pbits = (keybits-separation)/2;
  qbits = keybits - pbits;
#else
  pbits = keybits/2;
  qbits = keybits - pbits;
#endif

  trueRandConsume(pbits); /* "use up" this many bits */

#ifdef STRONGPRIMES	/* make a good strong prime for the key */
  status = goodprime(p,pbits,pbits-latitude(pbits));
#else	/* just any random prime will suffice for the key */
  status = randomprime(p,pbits);
#endif	/* else not STRONGPRIMES */
  if (status < 0) 
    return(status);	/* failed to find a suitable prime */

  /* We now have prime p.  Now generate q such that q>p... */
  
  qbits = keybits - countbits(p);

  trueRandConsume(qbits); /* "use up" this many bits */
  /*	This load of random bits will be stirred and recycled until 
	a good q is generated. */
  
  do {	/* Generate a q until we get one that isn't too close to p. */
#ifdef STRONGPRIMES	/* make a good strong prime for the key */
    status = goodprime(q,qbits,qbits-latitude(qbits));
#else	/* just any random prime will suffice for the key */
    status = randomprime(q,qbits);
#endif	/* else not STRONGPRIMES */
    if (status < 0) 
      return(status);	/* failed to find a suitable prime */
    
    /* Note that at this point we can't be sure that q>p. */
    if (mp_compare(p,q) >= 0) { /* ensure that p<q for computing u */
      mp_move(u,p);
      mp_move(p,q);
      mp_move(q,u);
    }
    /* See if p and q are far enough apart.  Is q-p big enough? */
    mp_move(u,q);	/* use u as scratchpad */
    mp_sub(u,p);	/* compute q-p */
    too_close_together = (countbits(u) < (countbits(q)-7));
    
    /* Keep trying q's until we get one far enough from p... */
  } while (too_close_together);
  
  derive_rsakeys(pk,ebits);

  trueRandFlush();	/* ensure recycled random pool is destroyed */

  return 0;	/* normal return */
}	/* rsa_keygen */


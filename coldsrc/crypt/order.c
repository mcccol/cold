#include "mpilib.h"

/*--------------------- Byte ordering stuff -------------------*/

/* XLOWFIRST is defined iff external file format is LSB-first byteorder */
/* #define XLOWFIRST */ /* defined if external byteorder is LSB-first */

#ifdef NEEDSWAP
#undef NEEDSWAP	/* make sure NEEDSWAP is initially undefined */
#endif

/* Assume MSB external byte ordering */
#ifndef HIGHFIRST
#define NEEDSWAP /* External/internal byteorder differs, need byte swap */
#endif

#ifdef NEEDSWAP /* External/internal byteorder differs, need byte swap */
#define convert_byteorder(buf,bytecount) hiloswap(buf,bytecount)
#define mp_convert_order(r) hiloswap(r,units2bytes(global_precision))
#else
#define convert_byteorder(buf,bytecount)	/* nil statement */
#define mp_convert_order(r)	/* nil statement */
#endif	/* not NEEDSWAP */

void hiloswap(byteptr r1,short numbytes)
	/* Reverses the order of bytes in an array of bytes. */
{
	byteptr r2;
	byte b;
	r2 = &(r1[numbytes-1]);
	while (r1 < r2) {
		b = *r1; *r1++ = *r2; *r2-- = b;
	}
} /* hiloswap */


/*------------------ End byte ordering stuff -------------------*/

short byte2reg(register unitptr r,register byteptr buf, short bitcount)
/*
 * Converts a multiprecision integer from the externally-represented 
 * form of a byte array with a 16-bit bitcount in a leading length 
 * word to the internally-used representation as a unit array.
 * Converts to INTERNAL byte order.
 * The same buffer address may be used for both r and buf.
 * Returns number of units in result, or returns -1 on error.
 */
{
  byte buf2[MAX_BYTE_PRECISION];
  word16 bytecount, unitcount, zero_bytes, i;

  /* Convert bitcount to bytecount and unitcount... */
  bytecount = bits2bytes(bitcount);
  unitcount = bytes2units(bytecount);
  if (unitcount > global_precision) {
    /* precision overflow during conversion. */
    return(-1);	/* precision overflow -- error return */
  }
  zero_bytes = units2bytes(global_precision) - bytecount;
/* Assume MSB external byte ordering */
  fill0(buf2,zero_bytes);  /* fill leading zero bytes */
  i = zero_bytes;	/* assumes MSB first */
  while (bytecount--) buf2[i++] = *buf++;
  
  mp_convert_order(buf2);	/* convert to INTERNAL byte order */
  mp_move(r,(unitptr)buf2);
  mp_burn((unitptr)buf2);	/* burn the evidence on the stack */
  return(unitcount);	/* returns unitcount of reg */
} /* mpi2reg */


short reg2byte(register byteptr buf, register unitptr r)
/*
 * Converts the multiprecision integer r from the internal form of 
 * a unit array to the normalized externally-represented form of a
 * byte array with a leading 16-bit bitcount word in buf[0] and buf[1].
 * This bitcount length prefix is exact count, not rounded up.
 * Converts to EXTERNAL byte order.
 * The same buffer address may be used for both r and buf.
 * Returns the number of bytes of the result, not counting length prefix.
 */
{
  byte buf1[MAX_BYTE_PRECISION];
  byteptr buf2;
  short bytecount,bc;
  word16 bitcount;
  bitcount = countbits(r);
#ifdef DEBUG
  if (bitcount > MAX_BIT_PRECISION) {
    fprintf(stderr, "reg2mpi: bitcount out of range (%d)\n", bitcount);
    return 0;
  }
#endif
  bytecount = bits2bytes(bitcount);
  bc = bytecount;	/* save bytecount for return */
  buf2 = buf1;
  mp_move((unitptr)buf2,r);
  mp_convert_order(buf2);	/* convert to EXTERNAL byteorder */
/* Assume MSB external byte ordering */
  buf2 += units2bytes(global_precision) - bytecount;

  while (bytecount--) *buf++ = *buf2++;
  
  mp_burn((unitptr)buf1);	/* burn the evidence on the stack */
  return(bc);		/* returns bytecount of mpi, not counting prefix */
} /* reg2mpi */


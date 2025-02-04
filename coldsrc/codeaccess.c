/* codeaccess.c: Declarations for accessing C-- methods. */

/*
  For increased packing efficiency in method bytecode
  we've introduced a unicode-like packing for non-ints
 */

#include "execute.h"
#include "x.tab.h"
#include "opcodes.h"

int getInt(Code *opcodes, int *pc)
{
  int result;
  unsigned char *rp = (unsigned char *)&result;

  rp[3] = opcodes[(*pc)++];
  rp[2] = opcodes[(*pc)++];
  rp[1] = opcodes[(*pc)++];
  rp[0] = opcodes[(*pc)++];

  return result;
}

int incInt(Code *opcodes, int *pc)
{
  return *pc += 4;
}

void putInt(Code *opcodes, int *pc, int value)
{
  unsigned char *vp = (unsigned char *)&value;

  opcodes[(*pc)++] = vp[3];
  opcodes[(*pc)++] = vp[2];
  opcodes[(*pc)++] = vp[1];
  opcodes[(*pc)++] = vp[0];
  return;
}

unsigned sizeInt(int value)
{
  return 4;
}

unsigned int getPtr(Code *opcodes, int *pc)
{
  unsigned int result = opcodes[(*pc)++];
  if (result < 0x7F)
    return result;

  switch (result & 0xF0) {
  case 0x80:
  case 0x90:
  case 0xA0:
  case 0xB0:
    result = (result & 0x7f) << 8 | opcodes[(*pc)++];
    break;
  case 0xC0:
  case 0xD0:
    result = (result & 0x3f) << 8 | opcodes[(*pc)++];
    result = result << 8 | opcodes[(*pc)++];
    break;
  case 0xE0:
    result = (result & 0x1f) << 8 | opcodes[(*pc)++];
    result = result << 8 | opcodes[(*pc)++];
    result = result << 8 | opcodes[(*pc)++];
    break;
  case 0xF0:
    result = getInt(opcodes, pc);
    break;

  default:
    break;
  }

  return result;
}

unsigned sizePtr(unsigned value)
{
  if (value < 0x7f) {
    return 1;
  } else if (value < 0x3fff) {
    return 2;
  } else if (value < 0x1fffff) {
    return 3;
  } else if (value < 0x0fffffff) {
    return 4;
  } else {
    /* default case - int encoding + one flag byte */
    return 5;
  }
}

int incPtr(Code *opcodes, int *pc)
{
  return *pc += sizePtr(opcodes[*pc]);
}

void putPtr(Code *opcodes, int *pc, unsigned int value)
{
  if (value < 0x7f) {
    opcodes[(*pc)++] = value;
  } else if (value < 0x3fff) {
    opcodes[(*pc)++] = 0x80 | (value >> 8);
    opcodes[(*pc)++] = value & 0xFF;
  } else if (value < 0x1fffff) {
    opcodes[(*pc)++] = 0xC0 | (value >> 16);
    opcodes[(*pc)++] = (value >> 8) & 0xFF;
    opcodes[(*pc)++] = value & 0xFF;
  } else if (value < 0x0fffffff) {
    opcodes[(*pc)++] = 0xE0 | (value >> 24);
    opcodes[(*pc)++] = (value >> 16) & 0xFF;
    opcodes[(*pc)++] = (value >> 8) & 0xFF;
  } else {
    /* default case - int encoding + one flag byte */
    opcodes[(*pc)++] = 0xF0;
    putInt(opcodes, pc, value);
  }
}

unsigned getOp(Code *opcodes, int *pc)
{
  int op = getPtr(opcodes, pc);
  return op_info[op].opcode;
}

unsigned sizeOp(unsigned int value)
{
  return sizePtr(op_table[value].opcode);
}

int incOp(Code *opcodes, int *pc)
{
  return incPtr(opcodes, pc);
}

void putOp(Code *opcodes, int *pc, unsigned value)
{
  putPtr(opcodes, pc, op_table[value].opcode);
}

int getJump(Code *opcodes, int *pc)
{
  int ppc = *pc;
  unsigned int encoded = getPtr(opcodes, pc);
  int result;

  /* decode the sign bit from lowest order bit */
  if (encoded & 1) {
    result = - (encoded >> 1);
  } else {
    result = encoded >> 1;
  }
  return ppc + result;
}

int jump(Code *opcodes, int *pc)
{
  return *pc = getJump(opcodes, pc);
}

void putJump(Code *opcodes, int *pc, int value)
{
  unsigned int encoded;

  value = value - *pc;

  /* encode the sign bit to the lowest order bit */
  if (value < 0) {
    encoded = ((-value) << 1) | 1;
  } else {
    encoded = value << 1;
  }
  putPtr(opcodes, pc, encoded);
}

unsigned sizeJump(unsigned from, unsigned to)
{
  int value = to - from;
  return sizePtr((unsigned)
    (value < 0)
    ? ((-value) << 1) | 1
    : value << 1
    );
}

int incJump(Code *opcodes, int *pc)
{
  return incPtr(opcodes, pc);
}

void incArg(int argtype, Code *opcodes, int *pc)
{
  switch (argtype) {
  case INTEGER:
    incInt(opcodes, pc);
    break;

  case VAR:
  case STRING:
  case IDENT:
  case ERROR:
    incPtr(opcodes, pc);
    break;

  case JUMP:
    incJump(opcodes, pc);
    break;
  }
}

int incInstr(Code *opcodes, int *pc)
{
  int opcode = getOp(opcodes, pc);
  incArg(op_table[opcode].arg1, opcodes, pc);
  incArg(op_table[opcode].arg2, opcodes, pc);
  return opcode;
}

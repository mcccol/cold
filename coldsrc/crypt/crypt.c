/*	interfaces to PGP cryptographic functions
 */
#include <stdio.h>
#include "idea.h"

#include "mpilib.h"
#include "genprime.h"
#include "rsa.h"
#include "md5.h"

#include "../x.tab.h"
#include "../dbpack.h"
#include "../object.h"
#include "../data.h"
#include "../list.h"
#include "../dict.h"
#include "../buffer.h"
#include "../memory.h"
#include "../config.h"
#include "../execute.h"
#include "../log.h"

void pack_dict(Dict *dict, FILE *fp);
Dict *unpack_dict(FILE *fp);

static Dict *cryptDict;	/* dictionary of private keys, inaccessible to C-- */
static FILE *crypt_file;

/* pick up the private key dictionary from disk */
void init_crypt(int cnew)
{
  crypt_file = fopen("binary/dictionary", (cnew) ? "w+" : "r+");
  if (!crypt_file)
    fail_to_start("Cannot open object crypt file.");
  else if (cnew) {
    /* create new crypt */
    cryptDict = dict_new_empty();
  } else {
    cryptDict = unpack_dict(crypt_file);
  }
}

/* flush the private key dictionary to disk */
void crypt_flush()
{
  if (fseek(crypt_file, 0L, SEEK_SET))
    return;
  pack_dict(cryptDict, crypt_file);
}

void crypt_del(Object *o)
{
  Data curObj;

  /* associate private with cryptDict */
  curObj.type = DBREF;
  curObj.u.dbref = o->dbref;
  if (dict_contains(cryptDict, &curObj))
    dict_del(cryptDict, &curObj);
  crypt_flush();
}

/* allocKey - put a list of key-buffers into d */
static List *allocKey(int bits, int num)
{
  List *l;
  Data *dp;
  int size = rsa_keybytes(bits + max(SLOP_BITS, 1));

  l = list_new(num);

  dp = list_empty_spaces(l, num);

  dp->type = INTEGER;
  dp->u.val = rsa_keybits(bits);

  while (--num) {
    dp = list_next(l, dp);
    dp->type = BUFFER;
    dp->u.buffer = buffer_new(size);
    memset(dp->u.buffer->s, 0, size);
  }

  return l;
}

static List *mkPubKey(List *prkl)
{
  List *l;
  Data *dp;

  l = list_new(3);
  dp = list_first(prkl);
  l = list_add(l, dp);
  dp = list_next(prkl, dp);
  l = list_add(l, dp);
  dp = list_next(prkl, dp);
  l = list_add(l, dp);

  return l;
}

/* put aligned pointers to a list of buffers in an array */
static void list2Array(List *l, int *pk, int num)
{
  Data *dp = list_first(l);

  *(pk++) = dp->u.val;

  while (--num) {
    dp = list_next(l, dp);
    *((void **)pk++) = dp->u.buffer->s;
  }
}


/* an RSA key pair is generated for this object, and the public half returned.
 *
 * the private key is never presented to coldmud, but held by the server
 *	which maps caller to private key.
 *
 * The public key is not held by the server, and must be presented, verbatim, 
 *	in order to perform public en/decryptions.  Managing the public key of
 *	a given object is the responsibility of the core.
 */
void op_mkRSA(void)
{
  Data *args;
  int num_args;

  List *pub;
  List *priv;
  Data curObj;
  Data keyData;
  int bits = 1024;
  PublicKey pk;
  PrivateKey prk;

  /* Take an optional integer argument. */
  if (!func_init_0_or_1(&args, &num_args, INTEGER))
    return;

  if (num_args)
    bits = args[0].u.val;

  bits = rsa_keybits(bits);

  /* get some ColdMUD data space for the private key */
#define PrivateKeyKeys (sizeof(PrivateKey) / sizeof(void*))
  priv = allocKey(bits, PrivateKeyKeys);

  /* translate to Key structs */
  list2Array(priv, &(prk.bits), PrivateKeyKeys);

  /* get an RSA keypair */
  rsa_keygen(&prk, (PublicKey*)0, bits, 0);

  /* get some ColdMUD data space for the public key */
  pub = mkPubKey(priv);

  /* associate private with cryptDict */
  curObj.type = DBREF;
  curObj.u.dbref = cur_frame->object->dbref;
  keyData.type = LIST;
  keyData.u.list = priv;
  cryptDict = dict_add(cryptDict, &curObj, &keyData);
  crypt_flush();

  /* return public key */
  pop(num_args);
  push_list(pub);
  list_discard(pub);
}

#if 0
static void readKey(char *file, PrivateKey *prk)
{
  FILE *keyfile = fopen(file, "r");
  int size;

  fread(&prk->bits, sizeof(int), 1, keyfile);

  size = bits2units(prk->bits + max(SLOP_BITS,1));
  set_precision(size);

  prk->n = (unitptr)calloc(size, sizeof(unit));
  prk->e = (unitptr)calloc(size, sizeof(unit));
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
#endif

/*	enRSA(buffer[, key])
 * If key is given, it's a public key and used to RSA encrypt buffer
 *	else the caller's private key is used to RSA encrypt buffer
 * Returns an encrypted buffer to the caller.
 *		~keyf is thrown if the caller has no private key 
 *		(that is, if it has never called mkRSA())
 */
void op_enRSA(void)
{
  Data *args;
  int num_args;
  Buffer *crypt;
  int result;
  int len;
  int (*fn)(u_char*, uint*, u_char*, unit, void*);
  PrivateKey pk;  /* rely on Public being a prefix of Private key structure */

  if (!func_init_1_or_2(&args, &num_args, BUFFER, LIST))
    return;

  if (num_args == 2) {
    /* use supplied public key */
    list2Array(args[1].u.list, &(pk.bits), 3);

    /* select function to perform encryption */
    fn = (int (*)(u_char*, uint*, u_char*, unit, void*))RSAPublicBlock;

  } else {
    /* use Current Object's private key */
    Data curObj;
    Data key;

    /* extract key from cryptDict */
    curObj.type = DBREF;
    curObj.u.dbref = cur_frame->object->dbref;

    if ((dict_find(cryptDict, &curObj, &key) != NOT_AN_IDENT)
	|| (key.type != LIST)) {
      	cthrow(keynf_id,
	       "Current object (#%l) has no RSA key.",
	       cur_frame->object);
	return;
    }

    /* use private key */
    list2Array(key.u.list, &(pk.bits), PrivateKeyKeys);

    data_discard(&key);

    /* select function to perform encryption */
    fn = (int (*)(u_char*, uint*, u_char*, unit, void*))RSAPrivateBlock;
  }

#if 0
  readKey("coldkey", &pk);
#endif

  crypt = buffer_new(5 + (pk.bits+7) / 8);
  result = (fn)(
    crypt->s,
    &len,
    args[0].u.buffer->s,
    buffer_len(args[0].u.buffer),
    &pk
    );

  if (result) {
    buffer_discard(crypt);
    cthrow(range_id, "RSA failed - message greater than modulus.");
    return;
  }

  crypt = buffer_truncate(crypt, len);
  pop(num_args);
  push_buffer(crypt);
  buffer_discard(crypt);
  return;
}


void op_enIDEA(void)
{
  Data *args;
  Buffer *result;
  int len;
  struct IdeaCfbContext cfb;

  if (!func_init_2(&args, BUFFER /* key */, BUFFER /* plain */))
    return;

  len = buffer_len(args[1].u.buffer);
  result = buffer_new(len);

  ideaCfbInit(&cfb, args[1].u.buffer->s);
  ideaCfbEncrypt(&cfb, args[0].u.buffer->s, result->s, len);
  ideaCfbDestroy(&cfb);

  pop(2);
  push_buffer(result);
  buffer_discard(result);
}


void op_deIDEA(void)
{
  Data *args;
  Buffer *result;
  int len;
  struct IdeaCfbContext cfb;

  if (!func_init_2(&args, BUFFER, BUFFER))
    return;

  len = buffer_len(args[1].u.buffer);
  result = buffer_new(len);

  ideaCfbInit(&cfb, args[0].u.buffer->s);
  ideaCfbDecrypt(&cfb, args[1].u.buffer->s, result->s, len);
  ideaCfbDestroy(&cfb);

  pop(2);
  push_buffer(result);
  buffer_discard(result);
}

void op_MD5(void)
{
  Data *args;
  Buffer *result;
  int len;
  struct MD5Context md5c;

  if (!func_init_1(&args, BUFFER))
    return;

  len = buffer_len(args[0].u.buffer);
  result = buffer_new(16);

  MD5Init(&md5c);
  MD5Update(&md5c, args[0].u.buffer->s, len);
  MD5Final(result->s, &md5c);

  pop(2);
  push_buffer(result);
  buffer_discard(result);
}


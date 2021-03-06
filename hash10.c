#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>

#include <xmmintrin.h>

/* gcc specific */
typedef unsigned int uint128_t __attribute__((__mode__(TI)));

#define hashmult 13493690561280548289ULL
/* #define hashmult 2654435761 */

struct block {
  char *addr;
  size_t len;
};

struct block slurp(char *filename)
{
  int fd=open(filename,O_RDONLY);
  char *s;
  struct stat statbuf;
  struct block r;
  if (fd==-1)
    err(1, "%s", filename);
  if (fstat(fd, &statbuf)==-1)
    err(1, "%s", filename);
  /* allocates an extra 7 bytes to allow full-word access to the last byte */
  s = mmap(NULL,statbuf.st_size+7, PROT_READ|PROT_WRITE,MAP_FILE|MAP_PRIVATE,fd, 0);
  if (s==MAP_FAILED)
    err(1, "%s", filename);
  r.addr = s;
  r.len = statbuf.st_size;
  return r;
}

#define HASHSIZE (1<<20)

struct hashnode {
  size_t keylen;
  int value;
  char *keyaddr;
};// __attribute__((aligned (32)));

struct hashnode ht[HASHSIZE];// __attribute__((aligned (64)));


unsigned long hash(char *addr, size_t len)
{
  /* assumptions: 1) unaligned accesses work 2) little-endian 3) 7 bytes
     beyond last byte can be accessed */
  uint128_t x=0, w;
  size_t i, shift;
  for (i=0; i+7<len; i+=8) {
    w = *(unsigned long *)(addr+i);
    x = (x + w)*hashmult;
  }
  if (i<len) {
    shift = (i+8-len)*8;
    /* printf("len=%d, shift=%d\n",len, shift);*/
    w = (*(unsigned long *)(addr+i))<<shift;
    x = (x + w)*hashmult;
  }
  return x+(x>>64);
}

void insert_sub(char *keyaddr, size_t keylen, int value, long hashcode)
{
  long i = hashcode;
  struct hashnode *l= &ht[i];
  struct hashnode *p = l;

  if (p->keyaddr != NULL) {
    i = (i + 1) & (HASHSIZE - 1);
    p = &ht[i];

    while (p != l) {
      if (p->keyaddr == NULL) {
        p->keyaddr = keyaddr;
        p->keylen = keylen;
        p->value = value;
        return;
      }

      i = (i + 1) & (HASHSIZE - 1);
      p = &ht[i];
    }

    return;
  }

  p->keyaddr = keyaddr;
  p->keylen = keylen;
  p->value = value;
}

void insert(char *keyaddr, size_t keylen, int value)
{
  long hashcode = hash(keyaddr, keylen) & (HASHSIZE-1);
  insert_sub(keyaddr, keylen, value, hashcode);
}

struct hashreq_t {
  char *keyaddr;
  size_t keylen;
  int value;
};

void insert_bulk(struct hashreq_t *inserts, int size)
{
  int i;
  long hashes[size];

  for (i = 0; i < size; i++) {
    hashes[i] = hash(inserts[i].keyaddr, inserts[i].keylen) & (HASHSIZE-1);
    _mm_prefetch(&ht[hashes[i]], _MM_HINT_T2);
  }

  for (i = 0; i < size; i++) {
    insert_sub(inserts[i].keyaddr, inserts[i].keylen, inserts[i].value, hashes[i]); 
  }
}

int lookup_sub(char *keyaddr, size_t keylen, long start)
{
  long i = start;
  struct hashnode p = ht[i];

  if (keylen != p.keylen || memcmp(keyaddr, p.keyaddr, keylen)!=0) {
    i = (i + 1) & (HASHSIZE - 1);
    p = ht[i];

    while (i != start) {
      if (keylen == p.keylen && memcmp(keyaddr, p.keyaddr, keylen)==0) {
        return p.value;
      }

      i = (i + 1) & (HASHSIZE - 1);
      p = ht[i];
    }

    return -1;
  }

  return p.value;
}

int lookup(char *keyaddr, size_t keylen)
{
  long hashcode = hash(keyaddr, keylen) & (HASHSIZE-1);
  return lookup_sub(keyaddr, keylen, hashcode);
}

void lookup_bulk(struct hashreq_t *lookups, int size)
{
  int i;

  for (i = 0; i < size; i++) {
    lookups[i].value = hash(lookups[i].keyaddr, lookups[i].keylen) & (HASHSIZE-1);
    _mm_prefetch(&ht[lookups[i].value], _MM_HINT_T2);
  }

  for (i = 0; i < size; i++) {
    lookups[i].value = lookup_sub(lookups[i].keyaddr, lookups[i].keylen, lookups[i].value); 
  }
}

/*
int main()
{
  char a[40]="abcdefghijklmnopqrstuvwxyz1234567890";
  char b[40]="abcdefghijklmnopqrstuvwxyz1234567890";

  int i;
  for (i=32; i>=0; i--) {
    a[i] = '$';
    b[i] = '%';
    printf("%ld,%ld\n",hash(a,i),hash(b,i));
    if (hash(a,i)!=hash(b,i)) {
      fprintf(stderr, "hash error\n");
      exit(1);
    }
  }
  return 0;
}
*/  

#define BLOCKSIZE 512

int main(int argc, char *argv[])
{
  struct block input1, input2;
  char *p, *nextp, *endp;
  unsigned int i,j,k;
  unsigned long r=0;
  if (argc!=3) {
    fprintf(stderr, "usage: %s <dict-file> <lookup-file>\n", argv[0]);
    exit(1);
  }
  input1 = slurp(argv[1]);
  input2 = slurp(argv[2]);

  struct hashreq_t requests[BLOCKSIZE];

  for (p=input1.addr, endp=input1.addr+input1.len, i=0; p<endp;) {
    for (j = 0; j < BLOCKSIZE && p < endp; j++, i++) {
      nextp=memchr(p, '\n', endp-p);
      if (nextp == NULL) {
        p = endp;
        break;
      }

      requests[j].keyaddr = p;
      requests[j].keylen = nextp - p;
      requests[j].value = i;

      p = nextp+1;
    }

    insert_bulk(requests, j);
  }
#if 0 
 struct hashnode *n;
  unsigned long count, sumsq=0, sum=0;
  for (i=0; i<HASHSIZE; i++) {
    for (n=ht[i], count=0; n!=NULL; n=n->next)
      count++;
    sum += count;
    sumsq += count*count;
  }
  printf("sum=%ld, sumsq=%ld, hashlen=%ld, chisq=%f\n",
	 sum, sumsq, HASHSIZE, ((double)sumsq)*HASHSIZE/sum-sum);
  /* expected value for chisq is ~HASHSIZE */
#endif      

  for (i=0; i<10; i++) {
    for (p=input2.addr, endp=input2.addr+input2.len; p<endp; ) {
      for (j = 0; j < BLOCKSIZE && p < endp; j++) {
        nextp=memchr(p, '\n', endp-p);
        if (nextp == NULL) {
          p = endp;
          break;
        }
        
        requests[j].keyaddr = p;
        requests[j].keylen = nextp - p;
        p = nextp+1;
      }
      lookup_bulk(requests, j);
      for (k = 0; k < j; k++) {
        r = ((unsigned long)r) * 2654435761L + requests[k].value;
        r = r + (r>>32);
      }
    }
  }
  printf("%ld\n",r);
  return 0;
}

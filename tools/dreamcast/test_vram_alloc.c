/* host test of pvr/vram_alloc.c: gcc -fsanitize=address,undefined -I../../ports/dreamcast/pvr test_vram_alloc.c ../../ports/dreamcast/pvr/vram_alloc.c */
#include "vram_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define POOL (6u*1024*1024 + 640*1024 + 256)
static unsigned char owner[POOL/256];
int main(void){
  if (va_init(POOL)) return 1;
  uint32_t total=va_free_bytes(); printf("pool %u free %u largest %u\n",POOL,total,va_largest_free());
  int32_t off[4000]; uint32_t sz[4000]; int n=0; srand(1); long fails=0, ops=0;
  memset(owner,0,sizeof owner);
  for(long it=0; it<2000000; it++){
    if(n<4000 && (rand()%3 || n==0)){
      uint32_t s=1u<<(7+rand()%(rand()%8==0?14:8)); int32_t o=va_alloc(s); ops++;
      if(o<0){fails++;continue;}
      uint32_t b= s<256?256:s;
      if(o%b){printf("misaligned\n");return 1;}
      if((uint32_t)o+b>POOL){printf("oob\n");return 1;}
      for(uint32_t u=o/256;u<(o+b)/256;u++){ if(owner[u]){printf("overlap\n");return 1;} owner[u]=1;}
      off[n]=o; sz[n]=b; n++;
    } else { int i=rand()%n; for(uint32_t u=off[i]/256;u<(off[i]+sz[i])/256;u++) owner[u]=0; va_free(off[i]); off[i]=off[n-1]; sz[i]=sz[n-1]; n--; }
  }
  while(n){ n--; va_free(off[n]); }
  printf("ops %ld fails %ld final free %u (== %u) largest %u\n",ops,fails,va_free_bytes(),total,va_largest_free());
  return va_free_bytes()==total ? 0 : 1;
}

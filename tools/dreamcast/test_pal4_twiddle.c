#include "../../ports/dreamcast/pvr/pal4_twiddle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* KOS's reference: TWIDOUT and the 4bpp loader */
#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)|((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )
int main(void){
 int sizes[][2]={{8,8},{16,8},{8,16},{64,32},{32,256},{512,16},{256,256}};
 for(int s=0;s<7;s++){int w=sizes[s][0],h=sizes[s][1];int min=w<h?w:h,mask=min-1;
  uint8_t *idx=malloc(w*h),*packed=malloc(w*h/2);uint16_t *ref=calloc(w*h/4,2),*mine=calloc(w*h/4,2);
  for(int i=0;i<w*h;i++)idx[i]=rand()&15;
  for(int i=0;i<w*h;i+=2)packed[i/2]=idx[i]|idx[i+1]<<4;
  for(int y=0;y<h;y+=2)for(int x=0;x<w;x+=2)
   ref[TWIDOUT((x&mask)/2,(y&mask)/2)+(x/min+y/min)*min*min/4]=(packed[(x+y*w)>>1]&15)|((packed[(x+(y+1)*w)>>1]&15)<<4)|((packed[(x+y*w)>>1]>>4)<<8)|((packed[(x+(y+1)*w)>>1]>>4)<<12);
  for(int y0=0;y0<h;y0+=(h<16?h:16)){int n=h-y0<16?h-y0:16;pal4_write_rows(mine,w,h,y0,n,idx+y0*w,w);}
  int bad=memcmp(ref,mine,w*h/2)!=0;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(pal4_texel(mine,w,h,x,y)!=idx[y*w+x])bad++;
  printf("%dx%d %s\n",w,h,bad?"BAD":"ok");}
}

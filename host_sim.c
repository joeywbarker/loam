#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loam.h"
void hw_beep(int a){ (void)a; }
static World G; static uint16_t scr[LW*LH];
static void dump(void){
  for(int y=0;y<LH;y++){ for(int x=0;x<LW;x++){ int g=GLYPH(scr[y*LW+x]); 
     if(g==0x10)g='>'; else if(g==0x1F)g='v'; else if(g==0x11)g='<'; else if(g==0x1E)g='^';
     putchar(g<32?'?':g);} putchar('\n'); } }
int main(int argc,char**argv){
  int ticks = argc>1?atoi(argv[1]):3000; int show = argc>2;
  G.cells=scr; G.rng=0x2545F491u; genesis(&G);
  int minn=999,maxn=0;
  for(int t=1;t<=ticks;t++){
    world_tick(&G);
    int n,e; world_stats(&G,&n,&e);
    if(t>300){ if(n<minn)minn=n; if(n>maxn)maxn=n; }
    if(t%250==0){ int food=0,hot=0; for(int i=0;i<LW*LH;i++){ if(GLYPH(scr[i])=='.')food++; if(HEAT(scr[i]))hot++; }
      printf("t=%5d wisps=%3d energy=%5d food=%4d warm=%4d\n",t,n,e,food,hot); }
  }
  printf("after t>300: min wisps=%d max=%d\n",minn,maxn);
  if(show) dump();
  return 0;
}

#include <stdio.h>
#include <string.h>
#include "loam.h"
#pragma GCC diagnostic ignored "-Wunused-function"
void hw_beep(int a){ (void)a; }
static World G; static uint16_t scr[LW*LH];
static void blank(void){ memset(&G,0,sizeof G); G.cells=scr; G.rng=1; for(int i=0;i<LW*LH;i++) scr[i]=BLANK; }
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
int main(void){
  /* 1. scribe: ' H w  ' i w  -> writes H and i on the row below (right-hand side when heading east) */
  blank(); const char *prog="'Hw'iw'!w"; for(int i=0;prog[i];i++) put(&G,5+i,10,prog[i],12);
  spawn(&G,5,10,0,12,200);
  for(int t=0;t<12;t++) world_tick(&G);
  CHECK(GLYPH(cget(&G,7,11))=='H',"literal + write puts 'H' at (7,11)");
  CHECK(GLYPH(cget(&G,10,11))=='i',"second literal writes 'i' at (10,11)");
  CHECK(GLYPH(cget(&G,13,11))=='!',"third literal writes '!' at (13,11)");
  CHECK(OWNER(cget(&G,7,11))==12,"written cells are owned by the wisp's lineage");

  /* 2. arrows steer, '#' bounces */
  blank(); put(&G,10,5,'v',12); put(&G,10,8,'#',7);
  spawn(&G,10,5,0,12,200); for(int t=0;t<8;t++) world_tick(&G);
  int k=-1; for(int j=0;j<MAXW;j++) if(G.w[j].alive) k=j;
  CHECK(k>=0 && G.w[k].dir==3 && G.w[k].y<8,"arrow turns wisp south; '#' bounces it back north");

  /* 3. starvation: no food -> death -> becomes food */
  blank(); spawn(&G,10,10,0,12,5); for(int t=0;t<8;t++) world_tick(&G);
  int n,e; world_stats(&G,&n,&e);
  CHECK(n==0,"wisp with 5 energy starves in empty ground");
  int foodnear=0; for(int x=10;x<20;x++) if(GLYPH(scr[10*LW+x])=='.') foodnear++;
  CHECK(foodnear>=1,"its body is recycled into food");

  /* 4. eating */
  blank(); put(&G,12,10,'.',10); spawn(&G,10,10,0,12,30); for(int t=0;t<3;t++) world_tick(&G);
  k=-1; for(int j=0;j<MAXW;j++) if(G.w[j].alive) k=j;
  CHECK(k>=0 && G.w[k].energy>30,"eating '.' raises energy");

  /* 5. human ground is expensive to overwrite; ' w' fails when energy is too low */
  blank(); put(&G,7,11,'Q',HUMAN); put(&G,5,10,'\'',12); put(&G,6,10,'Z',12); put(&G,7,10,'w',12);
  spawn(&G,5,10,0,12,20); for(int t=0;t<6;t++) world_tick(&G);
  CHECK(GLYPH(cget(&G,7,11))=='Q',"a poor wisp cannot overwrite human text (cost 24)");
  blank(); put(&G,7,11,'Q',HUMAN); put(&G,5,10,'\'',12); put(&G,6,10,'Z',12); put(&G,7,10,'w',12);
  spawn(&G,5,10,0,12,100); for(int t=0;t<6;t++) world_tick(&G);
  CHECK(GLYPH(cget(&G,7,11))=='Z',"a rich wisp can, and pays for it");

  /* 6. bud: splitting behind */
  blank(); put(&G,10,10,'s',12); spawn(&G,9,10,0,12,100); for(int t=0;t<2;t++) world_tick(&G);
  world_stats(&G,&n,&e);
  CHECK(n==2 && e<=100,"'s' buds a second wisp and splits the energy between them");

  /* 7. conditional hop: ? skips next cell iff acc==0 */
  blank(); put(&G,5,10,'0',12); put(&G,6,10,'?',12); put(&G,7,10,'v',12); /* acc=0 -> hop over the 'v' */
  spawn(&G,5,10,0,12,100); for(int t=0;t<5;t++) world_tick(&G);
  k=-1; for(int j=0;j<MAXW;j++) if(G.w[j].alive) k=j;
  CHECK(k>=0 && G.w[k].dir==0 && G.w[k].y==10,"? with acc==0 hops over the turn");
  blank(); put(&G,5,10,'1',12); put(&G,6,10,'?',12); put(&G,7,10,'v',12); /* acc=1 -> executes 'v' */
  spawn(&G,5,10,0,12,100); for(int t=0;t<5;t++) world_tick(&G);
  k=-1; for(int j=0;j<MAXW;j++) if(G.w[j].alive) k=j;
  CHECK(k>=0 && G.w[k].dir==1,"? with acc!=0 lets the turn run");

  /* 8. crowding: two wisps never share a cell */
  blank(); spawn(&G,10,10,0,12,200); spawn(&G,11,10,2,9,200);
  for(int t=0;t<20;t++) world_tick(&G);
  int bad=0; for(int j=0;j<MAXW;j++) for(int l=j+1;l<MAXW;l++) if(G.w[j].alive&&G.w[l].alive&&G.w[j].x==G.w[l].x&&G.w[j].y==G.w[l].y) bad++;
  CHECK(bad==0,"no two wisps ever occupy one cell");
  printf(fails?"\n%d FAILED\n":"\nall instruction tests passed\n",fails); return fails!=0;
}

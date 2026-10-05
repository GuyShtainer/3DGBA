/* probe_ram.c -- P2a offset probe (3DGBA, GPLv3). Runs a ROM + its save in host libmgba until the
 * overworld callback is stable, then writes ewram/iwram/pltt/vram/oam to <outdir>/*.bin. The dumps hold
 * game data: they live in a scratch directory and are never committed. tools/voxel/probe_check.py
 * asserts every gba_game.h offset against a dump. Build:
 *   clang -std=c11 -I/opt/homebrew/include tools/voxel/probe_ram.c -L/opt/homebrew/lib -lmgba -o probe_ram
 * Run: ./probe_ram game.gba outdir   (game.sav beside the ROM is autoloaded) */
#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t rd32(struct mCore *c, uint32_t a){ return c->busRead32(c,a); }
int main(int argc,char**argv){
  struct mCore *core = mCoreFind(argv[1]);
  core->init(core);
  mCoreInitConfig(core,NULL);
  unsigned w,h; core->desiredVideoDimensions(core,&w,&h);
  color_t *buf = calloc(w*h,4);
  core->setVideoBuffer(core,buf,w);
  mCoreLoadFile(core,argv[1]);
  mCoreAutoloadSave(core);
  core->reset(core);
  int stable=0, f;
  for(f=0; f<20000; f++){
    uint16_t keys = 0;
    if((f/8)%12==0) keys = 1; /* A */
    core->setKeys(core,keys);
    core->runFrame(core);
    uint32_t cb2 = rd32(core,0x030022C4);
    if(cb2==0x08085E5D){ if(++stable>120) break; } else stable=0;
  }
  printf("frames=%d cb2=%08x\n",f,rd32(core,0x030022C4));
  const char *names[]={"ewram","iwram","pltt","vram","oam"};
  uint32_t base[]={0x02000000,0x03000000,0x05000000,0x06000000,0x07000000};
  uint32_t sz[]={0x40000,0x8000,0x400,0x18000,0x400};
  for(int i=0;i<5;i++){
    char p[256]; snprintf(p,sizeof p,"%s/%s.bin",argv[2],names[i]);
    FILE*o=fopen(p,"wb");
    for(uint32_t a=0;a<sz[i];a++){uint8_t b=core->busRead8(core,base[i]+a); fwrite(&b,1,1,o);}
    fclose(o);
  }
  uint16_t dc = core->busRead16(core,0x04000000); printf("dispcnt=%04x\n",dc);
  core->deinit(core);
  return 0;
}

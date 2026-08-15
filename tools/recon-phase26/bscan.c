#include <stdio.h>
#include <stdlib.h>
#include "fieldpath.h"
#include "fieldtrav.h"
static uint8_t* g_rom; static long g_romLen;
static uint8_t r8(void* c,uint32_t a){(void)c;if((a>>24)!=0x08u&&(a>>24)!=0x09u)return 0;uint32_t o=a&0x01FFFFFFu;return (g_rom&&(long)o<g_romLen)?g_rom[o]:0;}
static uint16_t r16(void* c,uint32_t a){return (uint16_t)(r8(c,a)|(r8(c,a+1)<<8));}
static uint32_t r32(void* c,uint32_t a){return (uint32_t)r16(c,a)|((uint32_t)r16(c,a+2)<<16);}
int main(int argc,char**argv){
 if(argc<6){printf("usage: %s rom mgHex gfx nGroups counts...\n",argv[0]);return 2;}
 FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); g_romLen=ftell(f); fseek(f,0,SEEK_SET);
 g_rom=malloc((size_t)g_romLen); if(fread(g_rom,1,(size_t)g_romLen,f)!=(size_t)g_romLen)return 1; fclose(f);
 uint32_t mg=(uint32_t)strtoul(argv[2],0,16); int gfx=atoi(argv[3]); int ng=atoi(argv[4]);
 FpBus bus={r8,r16,r32,0};
 for(int grp=0;grp<ng;grp++){ int cnt=atoi(argv[5+grp]);
  for(int num=0;num<cnt;num++){
   FtRomMap rm; if(!fieldtrav_rom_map(&bus,mg,grp,num,&rm))continue;
   int n=r8(0,rm.events+0); uint32_t ot=r32(0,rm.events+4);
   if(n<=0||n>64||((ot>>24)!=0x08&&(ot>>24)!=0x09))continue;
   for(int i=0;i<n;i++){ uint32_t t=ot+0x18u*(uint32_t)i;
    if(r8(0,t+1)!=(unsigned)gfx)continue;
    printf("map(%2d,%3d) %3dx%-3d boulder localId=%d at (%d,%d) elev=%d\n",grp,num,rm.w,rm.h,
      r8(0,t+0),(int)(int16_t)r16(0,t+4),(int)(int16_t)r16(0,t+6),r8(0,t+8));
   }
  }
 }
 return 0;
}

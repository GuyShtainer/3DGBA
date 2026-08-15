#include <stdio.h>
#include <stdlib.h>
#include "fieldpath.h"
#include "fieldtrav.h"
static uint8_t* g_rom; static long g_romLen;
static uint8_t r8(void* c,uint32_t a){(void)c;if((a>>24)!=0x08u&&(a>>24)!=0x09u)return 0;uint32_t o=a&0x01FFFFFFu;return (g_rom&&(long)o<g_romLen)?g_rom[o]:0;}
static uint16_t r16(void* c,uint32_t a){return (uint16_t)(r8(c,a)|(r8(c,a+1)<<8));}
static uint32_t r32(void* c,uint32_t a){return (uint32_t)r16(c,a)|((uint32_t)r16(c,a+2)<<16);}
static short d[300][300];
int main(int argc,char**argv){
 FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); g_romLen=ftell(f); fseek(f,0,SEEK_SET);
 g_rom=malloc((size_t)g_romLen); if(fread(g_rom,1,(size_t)g_romLen,f)!=(size_t)g_romLen)return 1; fclose(f);
 uint32_t mg=(uint32_t)strtoul(argv[2],0,16); int grp=atoi(argv[3]),num=atoi(argv[4]);
 int sx=atoi(argv[5]),sy=atoi(argv[6]),el=atoi(argv[7]);
 FpBus bus={r8,r16,r32,0}; FtRomMap rm;
 if(!fieldtrav_rom_map(&bus,mg,grp,num,&rm)){printf("bad\n");return 1;}
 FtRomBus rb; FpBus db; FpMap dm; fieldtrav_rom_bus(&rb,&bus,&rm,FP_ENG_RSE,&db,&dm);
 for(int y=0;y<rm.h;y++)for(int x=0;x<rm.w;x++)d[y][x]=-1;
 static short qx[90000],qy[90000]; int hd=0,tl=0; d[sy][sx]=0; qx[tl]=sx; qy[tl++]=sy;
 const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
 while(hd<tl){int x=qx[hd],y=qy[hd];hd++;
  for(int i=0;i<4;i++){int nx=x+dx[i],ny=y+dy[i];
   if(nx<0||nx>=rm.w||ny<0||ny>=rm.h||d[ny][nx]>=0)continue;
   if(!fieldpath_enterable(&db,&dm,nx,ny,el))continue;
   d[ny][nx]=(short)(d[y][x]+1); qx[tl]=nx; qy[tl++]=ny;}}
 for(int i=8;i+1<argc;i+=2){int tx=atoi(argv[i]),ty=atoi(argv[i+1]);
  printf("dist to (%d,%d) = %d\n",tx,ty,d[ty][tx]);}
 for(int y=0;y<rm.h;y++){printf("%3d ",y);
  for(int x=0;x<rm.w&&x<110;x++){
   char c = (x==sx&&y==sy)?'S': (d[y][x]>=0?'.':(fieldpath_enterable(&db,&dm,x,y,el)?'o':'#'));
   putchar(c);} printf("\n");}
 return 0;}

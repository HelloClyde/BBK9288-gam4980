#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_FIRMWARE_HLE_MASK 0x08u
#include "../src/gam4980_core.c"

static unsigned rng=0x79379288u;
static unsigned random32(void){rng=rng*1664525u+1013904223u;return rng;}
static int load(const char *p,void *b,size_t n){FILE *f=fopen(p,"rb");int ok;if(!f)return 0;ok=fread(b,1,n,f)==n;fclose(f);return ok;}
int main(int argc,char **argv){
    gam4980_buffers_t b={0};unsigned i,j,hits=0;unsigned char *initial,*expected;
    if(argc!=3)return 2;
    b.ram=malloc(GAM4980_RAM_SIZE);b.flash=malloc(GAM4980_FLASH_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);
    initial=malloc(GAM4980_RAM_SIZE);expected=malloc(GAM4980_RAM_SIZE);
    if(!b.ram||!b.flash||!b.rom_8||!b.rom_e||!initial||!expected)return 3;
    if(!load(argv[1],b.rom_8,GAM4980_ROM_SIZE)||!load(argv[2],b.rom_e,GAM4980_ROM_SIZE)||gam4980_init(&b)<=0)return 4;
    gam4980_set_performance_debug(1);
    memset(s6502_aot_validation,2,sizeof(s6502_aot_validation));
    sys.bk_tab[5]=5;mem_bs(5);sys.bk_tab[7]=0xebe;mem_bs(7);
    for(i=0;i<12000;++i){
        s6502_t start,ref;unsigned count=1+random32()%255u,budget,rc,hc,before;
        for(j=0;j<GAM4980_RAM_SIZE;++j)sys.ram[j]=(unsigned char)random32();
        sys.ram[_SYSCON]=0;sys.ram[_INCR]=(i&1)?8:0;
        sys.ram[_ADDR1L+9]=0;sys.ram[_ADDR1L+10]=0x60;sys.ram[_ADDR1L+11]=0;
        sys.ram[0x2f]=(unsigned char)random32();sys.ram[0x30]=0x50;
        sys.cpu.pc=0x7937;sys.cpu.ac=random32()&255;sys.cpu.ix=count;
        sys.cpu.iy=random32()&255;sys.cpu.sp=random32()&255;sys.cpu.status=random32()&~8u;
        budget=(i%3==0)?20*count+1:(i%3==1)?20*(1+random32()%count):1+random32()%(20*count);
        start=sys.cpu;memcpy(initial,sys.ram,GAM4980_RAM_SIZE);
        gam4980_set_firmware_hle_enabled(0);rc=s6502_exec(&sys.cpu,budget);ref=sys.cpu;memcpy(expected,sys.ram,GAM4980_RAM_SIZE);
        sys.cpu=start;memcpy(sys.ram,initial,GAM4980_RAM_SIZE);
        gam4980_set_firmware_hle_enabled(1);before=s6502_firmware_hle_hits;hc=s6502_exec(&sys.cpu,budget);hits+=s6502_firmware_hle_hits-before;
        if(rc!=hc||ref.pc!=sys.cpu.pc||ref.ac!=sys.cpu.ac||ref.ix!=sys.cpu.ix||ref.iy!=sys.cpu.iy||ref.sp!=sys.cpu.sp||ref.status!=sys.cpu.status||memcmp(expected,sys.ram,GAM4980_RAM_SIZE)){
            fprintf(stderr,"transfer mismatch case=%u budget=%u cycles=%u/%u pc=%x/%x A=%x/%x P=%x/%x\n",i,budget,rc,hc,ref.pc,sys.cpu.pc,ref.ac,sys.cpu.ac,ref.status,sys.cpu.status);return 1;
        }
    }
    printf("PASS byte transfer 12000 exact-state full/partial cases hits=%u\n",hits);
    gam4980_deinit();free(initial);free(expected);free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);
    return hits?0:5;
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define FIRMWARE_NATIVE_HOST_TEST
#define FW_RUNTIME_DIRECT_TEST
typedef struct { uint32_t pc,ac,ix,iy,sp,status,cycle_budget,cycles; uintptr_t ram,read8,write8,pages,page_kind; } s6502_iram_asm_context_t;
#include "../src/firmware_native_runtime.c"
static uint8_t ram[65536],initial[65536],expected[65536],kind[256];
static uint8_t *pages[256];
static unsigned calls,seed=9288;
static unsigned rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint8_t rd(uint16_t a){++calls;return ram[a];}
static void wr(uint16_t a,uint8_t v){++calls;ram[a]=v;}
int main(void){
    unsigned entries[]={0xd2ca,0xd8bd,0xddb8,0xd29d,0xdb2f,0xd780,0xd7b4,0xd7f1};
    unsigned t,i,fast=0;
    for(i=0;i<256;++i){pages[i]=ram+i*256;kind[i]=3;}
    for(t=0;t<12000;++t){
        s6502_iram_asm_context_t a={0},b;unsigned ra,rb,left=0x1100+(t&255),right=0x1500+((t*7)&255),target=0x1800;
        if(t%5==0)target=left+1; /* overlap must retain sequential semantics */
        if(t%7==0)target=0x800; /* LCD must retain callback dirty accounting */
        for(i=0;i<65536;++i)initial[i]=(uint8_t)rnd();
        initial[0x20]=left;initial[0x21]=left>>8;initial[0x23]=right;initial[0x24]=right>>8;
        initial[0x2a]=target-8;initial[0x2b]=(target-8)>>8;
        a.pc=entries[t%8];a.status=rnd()&255;a.sp=t&255;a.ac=9;a.ix=10;a.iy=11;
        a.cycle_budget=t%4?1000:t%150;a.ram=(uintptr_t)ram;a.read8=(uintptr_t)rd;a.write8=(uintptr_t)wr;
        b=a;memcpy(ram,initial,sizeof ram);ra=firmware_native_runtime(&a);memcpy(expected,ram,sizeof ram);
        memcpy(ram,initial,sizeof ram);b.pages=(uintptr_t)pages;b.page_kind=(uintptr_t)kind;calls=0;
        rb=firmware_native_runtime(&b);
        assert(ra==rb && a.pc==b.pc && a.ac==b.ac && a.ix==b.ix && a.iy==b.iy && a.sp==b.sp && a.status==b.status && a.cycles==b.cycles);
        assert(!memcmp(expected,ram,sizeof ram));
        if(ra && !calls)++fast;
        kind[0x11]=t&1?3:0;
    }
    assert(fast>1000);printf("PASS runtime direct 12000 cases fast=%u\n",fast);return 0;
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define FIRMWARE_NATIVE_HOST_TEST
#define FW_COMPARE_DIRECT_TEST
typedef struct { uint32_t pc,ac,ix,iy,sp,status,cycle_budget,cycles; uintptr_t ram,read8,pages,page_kind; } s6502_iram_asm_context_t;
#include "../src/firmware_native_compare.c"
static uint8_t ram[65536],initial[65536],expected[65536],kind[256];
static uint8_t *pages[256];
static unsigned reads,seed=9288;
static unsigned rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint8_t read8(uint16_t a){++reads;return ram[a];}
int main(void){
    unsigned t,i,fast=0;
    for(i=0;i<256;++i){pages[i]=ram+i*256;kind[i]=1;}
    for(t=0;t<12000;++t){
        s6502_iram_asm_context_t a={0},b;unsigned left=0x400+(t&255),right=0x900+((t*7)&255),ra,rb;
        for(i=0;i<65536;++i)initial[i]=(uint8_t)rnd();
        initial[0x20]=left;initial[0x21]=left>>8;initial[0x23]=right;initial[0x24]=right>>8;
        a.pc=t%3?0xd362:0xd340;a.status=rnd()&255;a.sp=t&255;a.ac=9;a.ix=10;a.iy=11;
        a.cycles=t%13;a.cycle_budget=t%5?500:t%130;a.ram=(uintptr_t)ram;a.read8=(uintptr_t)read8;
        b=a;memcpy(ram,initial,sizeof ram);ra=firmware_native_compare(&a);memcpy(expected,ram,sizeof ram);
        memcpy(ram,initial,sizeof ram);b.pages=(uintptr_t)pages;b.page_kind=(uintptr_t)kind;reads=0;
        rb=firmware_native_compare(&b);
        assert(ra==rb && a.pc==b.pc && a.ac==b.ac && a.ix==b.ix && a.iy==b.iy && a.sp==b.sp && a.status==b.status && a.cycles==b.cycles);
        assert(!memcmp(expected,ram,sizeof ram));
        if(b.pc!=0xd362 && ra && t%3 && kind[left>>8] && kind[right>>8] && (left&255)<=252 && (right&255)<=252){assert(reads==0);++fast;}
        kind[4]=(t&1)?1:0;
    }
    printf("PASS compare direct %u cases fast=%u\n",t,fast);return 0;
}

#define main arithmetic_test_main
#include "firmware_hle_multiply_test.c"
#undef main
#define FIRMWARE_NATIVE_HOST_TEST
#define s6502_iram_asm_context_t host_multiply_context
#include "../src/firmware_native_memory.c"
#undef s6502_iram_asm_context_t
#undef FIRMWARE_NATIVE_HOST_TEST

int main(int argc,char **argv)
{
    gam4980_buffers_t b={0};unsigned t,i,mode;int result=1;
    uint8_t *initial=malloc(GAM4980_RAM_SIZE),*expected=malloc(GAM4980_RAM_SIZE);
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);
    b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    if(argc!=3 || !initial || !expected || !b.ram || !b.flash || !b.rom_8 || !b.rom_e ||
       !load_exact(argv[1],b.rom_8,GAM4980_ROM_SIZE) || !load_exact(argv[2],b.rom_e,GAM4980_ROM_SIZE) ||
       gam4980_init(&b)<=0)return 2;
    gam4980_set_firmware_hle_enabled(0);memset(s6502_aot_validation,2,sizeof(s6502_aot_validation));
    for(mode=0;mode<2;++mode)for(t=0;t<2000u;++t){
        unsigned args=0x400u+(t%2u?0xfcu:0u),src=0x700u+(next_random()&255u);
        unsigned dst=src+(t%3u==0u?0u:t%3u==1u?97u:-97),count=t%601u,cycles=0;
        s6502_t saved,ref;host_multiply_context c={0};
        for(i=0;i<0x2000u;++i)sys.ram[i]=(uint8_t)next_random();
        if(mode){
            dst=0x1000u+(t&255u);
            for(i=0;i<count;++i)sys.ram[dst+i]=sys.ram[src+i];
            if(count && t%3u)sys.ram[dst+(t/3u)%count]^=1u;
        }
        sys.ram[_SYSCON]=0;sys.ram[0x28]=(uint8_t)args;sys.ram[0x29]=(uint8_t)(args>>8);
        sys.ram[args]=(uint8_t)dst;sys.ram[args+1]=(uint8_t)(dst>>8);
        sys.ram[args+2]=(uint8_t)src;sys.ram[args+3]=(uint8_t)(src>>8);
        sys.ram[args+4]=(uint8_t)count;sys.ram[args+5]=(uint8_t)(count>>8);
        sys.cpu.pc=mode?0xf68a:0xf5bd;sys.cpu.sp=(uint8_t)t;sys.cpu.status=(uint8_t)(next_random()&~8u);
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43;sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44;
        saved=sys.cpu;memcpy(initial,sys.ram,GAM4980_RAM_SIZE);
        do{cycles+=s6502_exec(&sys.cpu,1);}while(sys.cpu.pc!=0x4444 && cycles<50000u);
        ref=sys.cpu;memcpy(expected,sys.ram,GAM4980_RAM_SIZE);
        memcpy(sys.ram,initial,GAM4980_RAM_SIZE);sys.cpu=saved;
        c.pc=saved.pc;c.ac=saved.ac;c.ix=saved.ix;c.iy=saved.iy;c.sp=saved.sp;c.status=saved.status;
        c.ram=(uintptr_t)sys.ram;c.read8=(uintptr_t)mem_read;c.write8=(uintptr_t)mem_write;c.cycle_budget=50000;
        {
            host_multiply_context unchanged;
            c.cycle_budget=cycles-1u;unchanged=c;
            if(firmware_native_memory(&c) || memcmp(&c,&unchanged,sizeof(c)) ||
               memcmp(sys.ram,initial,GAM4980_RAM_SIZE)){
                fprintf(stderr,"memory short-budget mutation mode=%u t=%u\n",mode,t);goto done;
            }
            c.cycle_budget=50000;
        }
        i=firmware_native_memory(&c);
        if(i!=cycles || c.pc!=ref.pc || c.ac!=ref.ac || c.ix!=ref.ix || c.iy!=ref.iy ||
           c.sp!=ref.sp || c.status!=ref.status || memcmp(expected,sys.ram,GAM4980_RAM_SIZE)){
            fprintf(stderr,"memory t=%u src=%x dst=%x n=%u cost=%u/%u a=%x/%x p=%x/%x y=%x/%x\n",
                t,src,dst,count,i,cycles,c.ac,ref.ac,c.status,ref.status,c.iy,ref.iy);goto done;
        }
    }
    puts("native SysMemcpy/SysMemcmp: 4000 exact-state overlap/page cases passed");result=0;
done:
    gam4980_deinit();free(initial);free(expected);free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);
    return result;
}

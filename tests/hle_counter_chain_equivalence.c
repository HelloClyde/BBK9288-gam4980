#define GAM4980_HLE_FUSION_TEST
#include <stdint.h>
static uint16_t counter_stop_pc;
#define S6502_TEST_STOP_PREDICATE(pc) ((pc) == counter_stop_pc)
#define main bitmap_test_main
#include "game_hle_bitmap_equivalence.c"
#undef main
int main(int argc,char **argv) {
    gam4980_buffers_t b={0}; unsigned m,t,i,total=0;
    uint8_t *initial=malloc(GAM4980_RAM_SIZE),*expected=malloc(GAM4980_RAM_SIZE);
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);
    b.flash_size=GAM4980_FLASH_SIZE;b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);
    if(argc!=4 || !initial || !expected || !b.ram || !b.flash || !b.rom_8 || !b.rom_e)return 2;
    gam4980_set_firmware_hle_enabled(1);
    gam4980_set_performance_debug(1);
    if(!load_exact(argv[1],b.rom_8,GAM4980_ROM_SIZE) || !load_exact(argv[2],b.rom_e,GAM4980_ROM_SIZE) ||
       gam4980_init(&b)<=0 || !load_game(argv[3]))return 3;
    gam4980_set_firmware_hle_enabled(1);s6502_game_aot_requested=0;
    if(!s6502_game_hle_counter_count)return 4;
    for(m=0;m<s6502_game_hle_counter_count;++m) {
        const s6502_game_hle_counter_t *match=&s6502_game_hle_counters[m];
        counter_stop_pc=match->exit_pc;
        sys.bk_tab[match->virtual_pc>>12]=(uint16_t)(match->physical_pc>>12);
        mem_bs((uint8_t)(match->virtual_pc>>12));
        for(t=0;t<4096;++t) {
            s6502_t start={0},old,newcpu; uint32_t a,c,budget=1+(t*71u)%4096u;
            for(i=0x100;i<0x200;++i)b.ram[i]=(uint8_t)(i+t);
            b.ram[match->pointer_zp]=0; b.ram[(uint8_t)(match->pointer_zp+1)]=6;
            b.ram[0x600u+match->index]=(uint8_t)t;
            start.pc=match->virtual_pc;start.ac=91;start.ix=83;start.iy=17;
            start.sp=(uint8_t)t;start.status=(uint8_t)(t&~8u);
            memcpy(initial,b.ram,GAM4980_RAM_SIZE);old=start;
            s6502_counter_chain_enabled=0;a=s6502_exec(&old,budget);
            memcpy(expected,b.ram,GAM4980_RAM_SIZE);memcpy(b.ram,initial,GAM4980_RAM_SIZE);
            newcpu=start;s6502_counter_chain_enabled=1;c=s6502_exec(&newcpu,budget);
            if(a!=c || !cpu_equal(&old,&newcpu) || memcmp(expected,b.ram,GAM4980_RAM_SIZE)) {
                fprintf(stderr,"counter mismatch m=%u t=%u budget=%u cycles=%u/%u pc=%x/%x\n",m,t,budget,a,c,old.pc,newcpu.pc);return 1;
            }
            ++total;
        }
    }
    if (!hle_counter_chain_hits || !hle_counter_folded_rounds) return 5;
    printf("PASS counter chain %u exact CPU/RAM/cycle cases links=%u folded=%u\n",total,hle_counter_chain_hits,hle_counter_folded_rounds);return 0;
}

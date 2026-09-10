#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_ENABLE_GAME_LOAD_AOT
#define GAM4980_DYNAMIC_NATIVE_ALL
#include "../src/gam4980_core.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"preload line %d: %s\n",__LINE__,#x);return 1; } } while (0)
static int read_rom(const char *path,u8 *out)
{
    FILE *f=fopen(path,"rb");int ok;
    if(!f)return 0;ok=fread(out,1,GAM4980_ROM_SIZE,f)==GAM4980_ROM_SIZE;fclose(f);return ok;
}
int main(int argc,char **argv)
{
    gam4980_buffers_t b={0};unsigned i;u8 game[0x200]={0};
    const u8 calls[]={0x20,0x84,0xd1, /* integer multiply */
        0xa2,0x1e,0x86,0x26,0xa2,0xe7,0x86,0x27,0x20,0xf6,0xd2, /* strlen far */
        0x20,0x01,0xe9, /* SysMemcpy direct JMP stub */
        0x60,0x20,0x00,0xd0}; /* unreachable divide must not preload */
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);
    CHECK(argc==3 && b.ram && b.flash && b.rom_8 && b.rom_e);
    CHECK(read_rom(argv[1],b.rom_8) && read_rom(argv[2],b.rom_e));
    CHECK(gam4980_init(&b)>0);
    memcpy(game+0x46,calls,sizeof(calls));game[0x40]=0x46;game[0x41]=0x50;game[0x43]=2;
    native_function_preload_reset();s6502_game_aot_recover_cfg(game,sizeof(game),0x5046);
    CHECK(native_function_preload_candidate(0xea8184));
    CHECK(native_function_preload_candidate(0xeb13dd));
    CHECK(native_function_preload_candidate(0xea82f6));
    CHECK(native_function_preload_candidate(0xeaa5bd));
    CHECK(!native_function_preload_candidate(0xea8000));
    i=native_function_preload_count;native_function_preload_note(0xea8184);
    CHECK(native_function_preload_count==i);
    native_function_preload_prepare();CHECK(native_function_preload_count==i);
    native_function_preload_note(0xeb53d7u);
    native_function_preload_prepare();
    CHECK(native_function_preload_candidate(0xeb550fu));
    CHECK(!native_function_preload_candidate(0xeb508au));
    CHECK(!native_function_preload_candidate(0xeb8c5du));
    native_function_preload_note(0xeb582du);
    native_function_preload_prepare();
    CHECK(native_function_preload_candidate(0xeb8c5du));
    CHECK(native_function_preload_candidate(0xeb5b1au));
    CHECK(!native_function_preload_candidate(0xeb508au));
    native_function_preload_reset();s6502_game_aot_requested=0;
    s6502_game_aot_prepare(game,sizeof(game));
    CHECK(native_function_preload_candidate(0xea8184));
    CHECK(native_function_preload_candidate(0xeb13dd));
    CHECK(!native_function_preload_candidate(0xea8000));
    native_function_preload_reset();
    native_function_preload_note(0x20d046);CHECK(!native_function_preload_count);
    for(i=0;i<S6502_NATIVE_PRELOAD_CAPACITY+1u;++i)native_function_preload_note(0xe00000u+i);
    CHECK(native_function_preload_count==S6502_NATIVE_PRELOAD_CAPACITY && native_function_preload_overflow);
    CHECK(!native_function_preload_candidate(0xe00000u+S6502_NATIVE_PRELOAD_CAPACITY));
    native_function_preload_reset();CHECK(!native_function_preload_count && !native_function_preload_overflow);
    gam4980_deinit();free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);
    puts("native function preload: reachable calls, far calls, JMP stubs, AOT-off, overflow PASS");return 0;
}

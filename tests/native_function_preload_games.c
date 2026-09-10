/* Read-only preload inventory for locally supplied games; emits no game data. */
#define main preload_contract_main
#include "native_function_preload_test.c"
#undef main
int main(int argc,char **argv)
{
    gam4980_buffers_t b={0};int n;
    if(argc<4)return 2;
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);
    CHECK(b.ram && b.flash && b.rom_8 && b.rom_e);
    CHECK(read_rom(argv[1],b.rom_8) && read_rom(argv[2],b.rom_e));
    for(n=3;n<argc;++n){
        FILE *f=fopen(argv[n],"rb");long size;unsigned i;
        CHECK(f);CHECK(fseek(f,0,SEEK_END)==0);size=ftell(f);CHECK(size>=0x46 && size<=GAM4980_GAME_MAX_SIZE);
        rewind(f);CHECK(gam4980_init(&b)>0);
        CHECK(fread(gam4980_game_storage(),1,(size_t)size,f)==(size_t)size);fclose(f);
        s6502_game_aot_requested=0;
        CHECK(gam4980_load_game_header(gam4980_game_storage(),(u32)size)>0);
        printf("game=%s candidates=%u overflow=%u targets=",argv[n],native_function_preload_count,native_function_preload_overflow);
        for(i=0;i<native_function_preload_count;++i)printf("%s%06x",i?",":"",native_function_preload_targets[i]);
        puts("");gam4980_deinit();
    }
    free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);return 0;
}

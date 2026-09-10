#define main loader_unused_main
#include "native_function_loader_test.c"
#undef main
static u8 cache[65536];
static u32 cache_size;
static int cache_io(int write,u32 offset,void *data,u32 size)
{
    if(offset>sizeof(cache) || size>sizeof(cache)-offset)return 0;
    if(write){memcpy(cache+offset,data,size);if(offset+size>cache_size)cache_size=offset+size;return 1;}
    if(offset>cache_size || size>cache_size-offset)return 0;
    memcpy(data,cache+offset,size);return 1;
}
int main(int argc,char **argv)
{
    gam4980_buffers_t b={0};u32 size,game_size,entries,hash;u8 *game;
    CHECK(argc==4);
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    b.rom_8=read_file(argv[1],&size);b.rom_e=read_file(argv[2],&size);
    CHECK(gam4980_init(&b)>0);
    game=read_file(argv[3],&game_size);CHECK(game);
    {u32 op,kind;
     for(op=0;op<256u;++op)for(kind=0;kind<C6502_TEMPLATE_SPEC_COUNT;++kind){
        const c6502_template_spec_t *s=&c6502_template_specs[kind];
        CHECK(((c6502_template_first_index[op]>>kind)&1u)==
              ((op&s->mask[0])==(s->bytes[0]&s->mask[0])));
     }}
    gam4980_set_analysis_io(cache_io);
    s6502_game_aot_requested=1;
    s6502_game_aot_prepare(game,game_size);
    printf("status=%u cache=%u entries=%u code=%u\n",analysis_cache_status,cache_size,s6502_game_aot_entry_count,s6502_game_aot_code_size);
    CHECK(analysis_cache_status==3u);
    entries=s6502_game_aot_entry_count;CHECK(entries);
    hash=analysis_hash(s6502_game_aot_entries,entries*sizeof(s6502_game_aot_entries[0]));
    s6502_game_aot_prepare(game,game_size);
    CHECK(analysis_cache_status==2u && entries==s6502_game_aot_entry_count);
    CHECK(hash==analysis_hash(s6502_game_aot_entries,entries*sizeof(s6502_game_aot_entries[0])));
    cache[cache_size-1]^=1; s6502_game_aot_prepare(game,game_size);CHECK(analysis_cache_status==3u);
    cache[4]^=1; s6502_game_aot_prepare(game,game_size);CHECK(analysis_cache_status==3u);
    cache_size=33; s6502_game_aot_prepare(game,game_size);CHECK(analysis_cache_status==3u);
    game[0x100]^=1;s6502_game_aot_prepare(game,game_size);CHECK(analysis_cache_status==3u);
    gam4980_set_analysis_io(0);s6502_game_aot_prepare(game,game_size);CHECK(!analysis_cache_status);
    printf("analysis cache: %u entries, hit equivalence/version/content/corruption/truncation PASS\n",entries);
    return 0;
}

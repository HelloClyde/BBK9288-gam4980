/* Actual-core cache regression against the previous independent linear model.
 * The test reader is deterministic, supports partial/failing reads, and never
 * opens an SDK, ROM or game file. Build with bare/dynamic flags to cover each
 * cache capacity without changing the production configuration. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#include "../src/gam4980_core.c"

typedef struct test_io {
    uint32_t calls, trace, fail_at;
    unsigned fail_all;
} test_io;
typedef struct old_cache {
    uint8_t data[ROM_CACHE_LINES][ROM_BANK_SIZE], direct[ROM_BANK_SIZE];
    uint32_t page[ROM_CACHE_LINES], stamp[ROM_CACHE_LINES], clock;
    uint8_t region[ROM_CACHE_LINES], valid[ROM_CACHE_LINES], owner[16];
    uint32_t direct_page, warm_pages, misses, dropped;
    uint8_t direct_region, direct_valid, direct_line, trace_count;
    rom_miss_trace_entry_t trace[GAM4980_ROM_MISS_TRACE_CAPACITY];
} old_cache;
static old_cache old;
static test_io actual_io, reference_io;
static uint8_t guest_ram[GAM4980_RAM_SIZE], resident8[GAM4980_ROM_SIZE], resident_e[GAM4980_ROM_SIZE];
static unsigned checks;
static uint32_t rng = 0x49809288u;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"ROM cache check %u line %d: %s\n",checks,__LINE__,#x);return 0; } } while(0)

static uint32_t random_value(void)
{ rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static uint8_t data_byte(unsigned region,uint32_t offset)
{ return (uint8_t)(offset*17u+(offset>>8)*29u+(offset>>12)*43u+region*97u); }
static int test_reader(void *context,u8 region,u32 offset,u8 *out,u32 size)
{
    test_io *io=(test_io *)context;
    uint32_t i,limit=size;
    int fail;
    ++io->calls;
    io->trace=(io->trace^region)*16777619u;
    io->trace=(io->trace^offset)*16777619u;
    io->trace=(io->trace^size)*16777619u;
    if(region>1u || offset>GAM4980_ROM_SIZE || size>GAM4980_ROM_SIZE-offset)return 0;
    fail=io->fail_all || (io->fail_at && io->calls==io->fail_at);
    if(fail)limit=size/2u;
    for(i=0;i<limit;++i)out[i]=data_byte(region,offset+i);
    return !fail;
}
static int old_range(uint8_t region,uint32_t offset,uint8_t *out,uint32_t size)
{
    const uint8_t *resident=region==GAM4980_ROM_REGION_8?sys.rom_8:sys.rom_e;
    if((!out && size) || offset>GAM4980_ROM_SIZE || size>GAM4980_ROM_SIZE-offset)return 0;
    if(resident){memcpy(out,resident+offset,size);return 1;}
    return test_reader(&reference_io,region,offset,out,size);
}
static void old_reset(void)
{
    old.clock=0;memset(old.valid,0,sizeof(old.valid));memset(old.owner,255,sizeof(old.owner));
    old.direct_valid=0;old.direct_line=255;
}
static void old_trace_reset(void)
{old.misses=old.trace_count=old.dropped=0;}
static void old_trace(uint8_t kind,uint8_t slot,uint8_t region,uint32_t page)
{
    unsigned i;uint16_t page_index=(uint16_t)(page>>12);
    for(i=0;i<old.trace_count;++i)if(old.trace[i].kind==kind && old.trace[i].slot==slot &&
        old.trace[i].region==region && old.trace[i].page==page_index)return;
    if(old.trace_count>=GAM4980_ROM_MISS_TRACE_CAPACITY){++old.dropped;return;}
    old.trace[old.trace_count].kind=kind;old.trace[old.trace_count].slot=slot;
    old.trace[old.trace_count].region=region;old.trace[old.trace_count++].page=page_index;
}
static uint8_t *old_bank(uint8_t slot,uint8_t region,uint32_t page)
{
    uint32_t oldest=0xffffffffu;uint8_t selected=255,line;
    for(line=0;line<ROM_CACHE_LINES;++line)if(old.valid[line] && old.region[line]==region && old.page[line]==page){
        old.owner[slot]=line;old.stamp[line]=++old.clock;return old.data[line];
    }
    ++old.misses;
    for(line=0;line<ROM_CACHE_LINES;++line){
        uint8_t owner;int pinned=0;
        if(!old.valid[line]){selected=line;break;}
        for(owner=0;owner<16u;++owner)if(owner!=slot && old.owner[owner]==line){pinned=1;break;}
        if(!pinned && old.stamp[line]<oldest){oldest=old.stamp[line];selected=line;}
    }
    if(selected==255)return 0;
    old_trace(GAM4980_ROM_MISS_MAPPED_BANK,slot,region,page);
    if(old.direct_valid && old.direct_line==selected){old.direct_valid=0;old.direct_line=255;}
    old.valid[selected]=0;
    if(!old_range(region,page,old.data[selected],ROM_BANK_SIZE))return 0;
    old.region[selected]=region;old.page[selected]=page;old.stamp[selected]=++old.clock;
    old.valid[selected]=1;old.owner[slot]=selected;return old.data[selected];
}
static uint8_t old_byte(uint8_t region,uint32_t offset)
{
    const uint8_t *resident=region==GAM4980_ROM_REGION_8?sys.rom_8:sys.rom_e;
    uint32_t page;uint8_t line;
    if(offset>=GAM4980_ROM_SIZE)return 0;
    if(resident)return resident[offset];
    page=offset&~(ROM_BANK_SIZE-1u);
    if(old.direct_valid && old.direct_region==region && old.direct_page==page){
        if(old.direct_line==255)return old.direct[offset&(ROM_BANK_SIZE-1u)];
        if(old.direct_line<ROM_CACHE_LINES && old.valid[old.direct_line] &&
           old.region[old.direct_line]==region && old.page[old.direct_line]==page)
            return old.data[old.direct_line][offset&(ROM_BANK_SIZE-1u)];
        old.direct_valid=0;old.direct_line=255;
    }
    if(old.direct_valid){old.direct_valid=0;old.direct_line=255;}
    for(line=0;line<ROM_CACHE_LINES;++line)if(old.valid[line] && old.region[line]==region && old.page[line]==page){
        old.stamp[line]=++old.clock;old.direct_region=region;old.direct_page=page;
        old.direct_line=line;old.direct_valid=1;return old.data[line][offset&(ROM_BANK_SIZE-1u)];
    }
    ++old.misses;old_trace(GAM4980_ROM_MISS_DIRECT,255,region,page);
    if(!old_range(region,page,old.direct,sizeof(old.direct)))return 0;
    old.direct_region=region;old.direct_page=page;old.direct_line=255;old.direct_valid=1;
    return old.direct[offset&(ROM_BANK_SIZE-1u)];
}
static void old_remap(void)
{
    unsigned slot;
    for(slot=1;slot<16u;++slot){
        uint32_t physical=(uint32_t)sys.bk_tab[slot]<<12;
        if(physical>=0x800000u && physical<0xa00000u && !sys.rom_8)
            (void)old_bank((uint8_t)slot,GAM4980_ROM_REGION_8,physical-0x800000u);
        else if(physical>=0xe00000u && physical<0x1000000u && !sys.rom_e)
            (void)old_bank((uint8_t)slot,GAM4980_ROM_REGION_E,physical-0xe00000u);
    }
}
static int old_warm(void)
{
#ifdef GAM4980_ENABLE_BARE_SESSION
    static const unsigned ranges[][3]={
        {0,0x000,0x01d},{0,0x022,0x022},{0,0x033,0x036},{0,0x078,0x078},
        {1,0x002,0x005},{1,0x00d,0x028},{1,0x045,0x048},{1,0x051,0x054},
        {1,0x0a0,0x0aa},{1,0x0b0,0x0c7},{1,0x0d4,0x0d7},{1,0x1ff,0x1ff}
    };
    unsigned next=0,r,p;
    old.warm_pages=0;old_trace_reset();old_reset();
    for(r=0;r<sizeof(ranges)/sizeof(ranges[0]);++r){
        unsigned region=ranges[r][0],first=ranges[r][1],count=ranges[r][2]-first+1u;
        if((region==0?sys.rom_8!=0:sys.rom_e!=0) || count>ROM_CACHE_LINES-next)continue;
        if(!old_range((uint8_t)region,first*ROM_BANK_SIZE,old.data[next],count*ROM_BANK_SIZE))goto failed;
        for(p=0;p<count;++p){old.region[next+p]=(uint8_t)region;old.page[next+p]=(first+p)*ROM_BANK_SIZE;
            old.stamp[next+p]=++old.clock;old.valid[next+p]=1;}
        next+=count;old.warm_pages+=count;
    }
    old_remap();old_trace_reset();return 1;
failed:
    old_reset();old_remap();old.warm_pages=0;old_trace_reset();return 0;
#else
    old.warm_pages=0;old_trace_reset();return 0;
#endif
}
static int check_state(void)
{
    uint8_t expected_index[2][512];
    unsigned line;
    ++checks;
    CHECK(old.clock==rom_cache_clock);
    CHECK(!memcmp(old.valid,rom_bank_valid,sizeof(old.valid)));
    CHECK(!memcmp(old.region,rom_bank_region,sizeof(old.region)));
    CHECK(!memcmp(old.page,rom_bank_page,sizeof(old.page)));
    CHECK(!memcmp(old.stamp,rom_bank_stamp,sizeof(old.stamp)));
    CHECK(!memcmp(old.owner,rom_slot_line,sizeof(old.owner)));
    CHECK(old.direct_page==rom_direct_page && old.direct_region==rom_direct_region);
    CHECK(old.direct_valid==rom_direct_valid && old.direct_line==rom_direct_line);
    CHECK(old.warm_pages==rom_cache_warm_page_count && old.misses==rom_cache_runtime_miss_count);
    CHECK(old.trace_count==rom_miss_trace_count && old.dropped==rom_miss_trace_dropped_count);
    CHECK(!memcmp(old.trace,rom_miss_trace,sizeof(old.trace)));
    CHECK(!memcmp(old.data,rom_bank_cache,sizeof(old.data)));
    CHECK(!memcmp(old.direct,rom_direct_cache,sizeof(old.direct)));
    CHECK(actual_io.calls==reference_io.calls && actual_io.trace==reference_io.trace);
    memset(expected_index,255,sizeof(expected_index));
    for(line=0;line<ROM_CACHE_LINES;++line)if(old.valid[line]){
        CHECK(old.region[line]<2u && old.page[line]<GAM4980_ROM_SIZE && !(old.page[line]&4095u));
        CHECK(expected_index[old.region[line]][old.page[line]>>12]==255);
        expected_index[old.region[line]][old.page[line]>>12]=(uint8_t)line;
    }
    CHECK(sizeof(rom_page_line_index)==sizeof(expected_index));
    CHECK(!memcmp(rom_page_line_index,expected_index,sizeof(expected_index)));
    CHECK(gam4980_rom_cache_index_lookups()==
          gam4980_rom_cache_index_hits()+gam4980_rom_cache_index_misses());
    return 1;
}
static int bank_op(unsigned slot,unsigned region,uint32_t page)
{
    uint8_t *a=rom_cached_bank((uint8_t)slot,(uint8_t)region,page);
    uint8_t *b=old_bank((uint8_t)slot,(uint8_t)region,page);
    CHECK((a!=0)==(b!=0));
    if(a){CHECK(a==rom_bank_cache[old.owner[slot]]);CHECK(!memcmp(a,b,ROM_BANK_SIZE));}
    return check_state();
}
static int byte_op(unsigned region,uint32_t offset)
{uint8_t a=rom_read_byte((uint8_t)region,offset),b=old_byte((uint8_t)region,offset);CHECK(a==b);return check_state();}
static int warm_op(void)
{
    unsigned slot;int a=gam4980_warm_bare_rom_cache(),b=old_warm();CHECK(a==b);
    CHECK(check_state());
    CHECK(!gam4980_rom_cache_index_lookups() && !gam4980_rom_cache_index_hits() &&
          !gam4980_rom_cache_index_misses());
#ifdef GAM4980_ENABLE_BARE_SESSION
    for(slot=1;slot<16u;++slot){
        uint32_t physical=(uint32_t)sys.bk_tab[slot]<<12;unsigned region;
        if(physical>=0x800000u && physical<0xa00000u)region=0;
        else if(physical>=0xe00000u && physical<0x1000000u)region=1;
        else continue;
        if(region==0?sys.rom_8!=0:sys.rom_e!=0)continue;
        if(old.owner[slot]<ROM_CACHE_LINES && old.valid[old.owner[slot]])
            CHECK(sys.mem_r[slot*16u]==rom_bank_cache[old.owner[slot]]);
        else CHECK(!sys.mem_r[slot*16u]);
    }
#else
    (void)slot;
#endif
    return 1;
}
static int boundary_checks(void)
{
    unsigned calls=actual_io.calls;
    CHECK(gam4980_rom_cache_index_bytes()==1024u);
    CHECK(rom_cache_index_key_valid(0,0));
    CHECK(rom_cache_index_key_valid(1,0x1ff000u));
    CHECK(!rom_cache_index_key_valid(2,0));
    CHECK(!rom_cache_index_key_valid(255,0));
    CHECK(!rom_cache_index_key_valid(1,1));
    CHECK(!rom_cache_index_key_valid(1,0x200000u));
    CHECK(!rom_cache_index_key_valid(1,0xffffffffu));
    /* Invalid requests are now rejected before indexing or touching cache
     * ownership. This is deliberately stricter than the old internal API. */
    CHECK(!rom_cached_bank(16,0,0));CHECK(!rom_cached_bank(255,1,0));
    CHECK(!rom_cached_bank(0,2,0));CHECK(!rom_cached_bank(0,255,0));
    CHECK(!rom_cached_bank(0,0,1));CHECK(!rom_cached_bank(0,1,0x200000u));
    CHECK(!rom_cached_bank(0,1,0xfffff000u));
    CHECK(!rom_read_byte(2,0));CHECK(!rom_read_byte(255,0x1fffffu));
    CHECK(!rom_read_byte(1,0x200000u));CHECK(!rom_read_byte(0,0xffffffffu));
    CHECK(actual_io.calls==calls);CHECK(check_state());
    /* A stale/corrupted slot number cannot read outside the cache. Invalid,
     * wrong-region and wrong-page aliases are forgotten, not trusted. */
    rom_cache_reset();old_reset();CHECK(check_state());
    rom_page_line_index[0][511]=(uint8_t)ROM_CACHE_LINES;
    CHECK(rom_cache_index_find(0,0x1ff000u)==255);CHECK(check_state());
    rom_page_line_index[1][511]=0;
    CHECK(rom_cache_index_find(1,0x1ff000u)==255);CHECK(check_state());
    CHECK(bank_op(0,0,0));
    rom_page_line_index[1][0]=0;
    CHECK(rom_cache_index_find(1,0)==255);CHECK(check_state());
    rom_page_line_index[0][1]=0;
    CHECK(byte_op(0,0x1001)); /* Real data path rejects the wrong-page alias. */
    CHECK(old.direct_line==255);CHECK(bank_op(1,0,0x1000));
    rom_cache_index_forget((uint8_t)ROM_CACHE_LINES);rom_cache_index_forget(255);
    CHECK(check_state());
    return 1;
}
static int run(void)
{
    unsigned i,t;
    sys.ram=guest_ram;sys.rom_read=test_reader;sys.rom_context=&actual_io;
    actual_io.trace=reference_io.trace=2166136261u;
    rom_cache_reset();old_reset();rom_miss_trace_reset();old_trace_reset();
    /* Distinct ROM regions at the same highest valid page, mapped aliases,
     * a direct alias, a separate direct buffer and subsequent mapped hits. */
    CHECK(bank_op(5,0,0x1ff000));CHECK(bank_op(6,1,0x1ff000));
    CHECK(bank_op(7,0,0x1ff000));CHECK(old.owner[5]==old.owner[7]);
    CHECK(byte_op(0,0x1fffff));CHECK(old.direct_line==old.owner[5]);
    {unsigned lookups=gam4980_rom_cache_index_lookups();CHECK(byte_op(0,0x1ff123));
     CHECK(gam4980_rom_cache_index_lookups()==lookups);}
    CHECK(byte_op(1,0x1fe001));CHECK(old.direct_line==255);
    CHECK(byte_op(1,0x1fefff));CHECK(bank_op(8,1,0x1fe000));
    CHECK(byte_op(1,0x1fe777)); /* Separate direct buffer stays separate. */
    CHECK(old.direct_line==255);
    CHECK(byte_op(0,0x1ffffe));
    /* Pinned windows cannot be evicted by slot8's sequential replacements. */
    for(i=0;i<ROM_CACHE_LINES*3u;++i)CHECK(bank_op(8,i&1u,(i%510u)*ROM_BANK_SIZE));
    CHECK(old.valid[old.owner[5]] && old.page[old.owner[5]]==0x1ff000u);
    /* Unpin the directly aliased line. LRU eviction must invalidate it. */
    rom_slot_line[5]=rom_slot_line[7]=255;old.owner[5]=old.owner[7]=255;
    for(i=0;i<ROM_CACHE_LINES*3u;++i)CHECK(bank_op(8,i&1u,((i+211u)%510u)*ROM_BANK_SIZE));
    CHECK(!old.direct_valid);

    for(t=0;t<16000u;++t){
        unsigned operation=random_value()%16u,slot=random_value()%16u;
        unsigned region=random_value()&1u;uint32_t page=(random_value()%512u)*ROM_BANK_SIZE;
        actual_io.fail_at=reference_io.fail_at=t%37u==0?actual_io.calls+1u:0u;
        if(t%7001u==0)old.clock=rom_cache_clock=0xfffffff8u;
        if(operation<9u){
            if((t&3u)==0 && old.owner[slot]<ROM_CACHE_LINES && old.valid[old.owner[slot]]){
                region=old.region[old.owner[slot]];page=old.page[old.owner[slot]];
            }
            CHECK(bank_op(slot,region,page));
        }else if(operation<14u){
            if((t&3u)==0 && old.direct_valid){region=old.direct_region;page=old.direct_page;}
            CHECK(byte_op(region,page+(random_value()&4095u)));
        }else if(operation==14u){
            /* Deliberately release an owner to exercise unpinned LRU choices.
             * This is test setup, not a claim that current mem_bs unpins it. */
            rom_slot_line[slot]=old.owner[slot]=255;CHECK(check_state());
        }else{
            unsigned lookups=gam4980_rom_cache_index_lookups();
            rom_cache_reset();old_reset();CHECK(check_state());
            CHECK(gam4980_rom_cache_index_lookups()==lookups);
        }
    }
    actual_io.fail_at=reference_io.fail_at=0;
    /* Warm-up success, partial batch failure, persistent read failure, and
     * mixed resident/streamed ROM. Include two windows aliasing the same page. */
    memset(sys.bk_tab,0,sizeof(sys.bk_tab));
    sys.bk_tab[5]=0xe01;sys.bk_tab[6]=0xfff;sys.bk_tab[7]=0x822;sys.bk_tab[8]=0xe01;
    CHECK(warm_op());
    actual_io.fail_at=reference_io.fail_at=actual_io.calls+2u;CHECK(warm_op());
    actual_io.fail_all=reference_io.fail_all=1;CHECK(warm_op());
    actual_io.fail_all=reference_io.fail_all=0;actual_io.fail_at=reference_io.fail_at=0;CHECK(warm_op());
    for(i=0;i<GAM4980_ROM_SIZE;++i){resident8[i]=data_byte(0,i);resident_e[i]=data_byte(1,i);}
    sys.rom_8=resident8;CHECK(warm_op());CHECK(byte_op(0,0x1fffff));
    sys.rom_e=resident_e;CHECK(warm_op());CHECK(byte_op(1,0x1fffff));
    sys.rom_8=sys.rom_e=0;CHECK(warm_op());
    CHECK(byte_op(0,GAM4980_ROM_SIZE));CHECK(byte_op(1,0xffffffffu));
    CHECK(boundary_checks());
    printf("ROM cache index: lines=%u checks=%u reader_calls=%u warm_pages=%u passed\n",
        (unsigned)ROM_CACHE_LINES,checks,actual_io.calls,old.warm_pages);
    return 1;
}
int main(void){return run()?0:1;}

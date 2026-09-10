#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_ENABLE_GAME_LOAD_AOT
#define GAM4980_DYNAMIC_NATIVE_ALL
#define GAM4980_ENABLE_IRAM_EXEC_ENGINE
#define GAM4980_IRAM_EXEC_ASM
#define GAM4980_IRAM_SUPER_HOST_TEST
#define GAM4980_IRAM_EXEC_NATIVE_TEST
#include "../src/gam4980_core.c"

uint32_t s6502_iram_exec_burst_asm(s6502_iram_asm_context_t *context)
{ (void)context; return 0; }
#ifdef GAM4980_STATIC_NATIVE_GAME
static u32 test_static_entry(s6502_iram_asm_context_t *context)
{(void)context;return 0;}
const gam4980_static_native_page_t gam4980_static_native_pages[GAM4980_STATIC_NATIVE_PAGE_COUNT]={
    {5u,0x0200u,0x5fu,0u,test_static_entry}
};
const gam4980_static_native_span_t gam4980_static_native_spans[GAM4980_STATIC_NATIVE_SPAN_COUNT]={{0u,1u}};
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"loader line %d: %s\n",__LINE__,#x);return 1; } } while (0)
typedef struct allocation_record { void *pointer; unsigned size; } allocation_record;
static allocation_record allocations[512];
static unsigned allocated_bytes, allocation_calls, free_calls, fail_alloc, fail_free;
static unsigned read_calls;
static unsigned char *package;
static unsigned package_size;
static u8 *allocate_test(void *context,u32 size)
{
    unsigned i;void *p;(void)context;++allocation_calls;
    if(fail_alloc)return 0;
    p=malloc(size);if(!p)return 0;
    for(i=0;i<512;++i)if(!allocations[i].pointer){
        allocations[i].pointer=p;allocations[i].size=size;allocated_bytes+=size;return p;
    }
    abort();
}
static int free_test(void *context,u8 *pointer)
{
    unsigned i;(void)context;if(fail_free)return 0;
    for(i=0;i<512;++i)if(allocations[i].pointer==pointer){
        allocated_bytes-=allocations[i].size;allocations[i].pointer=0;++free_calls;free(pointer);return 1;
    }
    abort();
}
static int package_read(void *context,u32 offset,u8 *out,u32 size)
{(void)context;++read_calls;if(offset>package_size || size>package_size-offset)return 0;memcpy(out,package+offset,size);return 1;}
static u8 *read_file(const char *path,u32 *size)
{
    FILE *f=fopen(path,"rb");u8 *data;long n;if(!f)return 0;
    fseek(f,0,SEEK_END);n=ftell(f);rewind(f);data=malloc(n);
    if(!data || fread(data,1,n,f)!=(size_t)n){fclose(f);free(data);return 0;}
    fclose(f);*size=(u32)n;return data;
}
static unsigned reference_function_for_pc(u16 pc)
{
    unsigned i,physical=((unsigned)sys.bk_tab[pc>>12]<<12)|(pc&4095u);
    for(i=0;i<native_module_header->module_count;++i){
        const gam4980_native_module_record_t *m=&native_module_records[i];
        const gam4980_native_link_record_t *l=&native_module_links[m->link_first];
        if(native_module_runtime[i].static_valid &&
           !native_module_cooldown_active(&native_module_runtime[i]) &&
           l->guest_pc==pc && (l->required_mapping>>16)==(pc>>12) &&
           (l->required_mapping&65535u)==sys.bk_tab[pc>>12] &&
           native_match_at(m->match_first)->physical_pc==physical)return i;
    }
    return GAM4980_NATIVE_MODULE_NONE;
}
static int verify_immutable_function_mappings(void)
{
    unsigned i,j,seed=0x92886502u,epoch,refreshes,fastpaths,unchanged,visits,rebuilds;
    u16 heads[256];u32 entries[256];
    memcpy(heads,native_function_page_head,sizeof(heads));
    memcpy(entries,native_module_page_entries,sizeof(entries));
    rebuilds=native_module_full_rebuild_count;visits=native_module_rebuild_module_visit_count;
    for(i=0;i<2048u;++i){
        unsigned slot,bank,previous;
        const gam4980_native_module_record_t *m;
        const gam4980_native_link_record_t *l;
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        m=&native_module_records[seed%native_module_header->module_count];
        l=&native_module_links[m->link_first];slot=l->guest_pc>>12;
        bank=(i&3u)==0u?sys.bk_tab[slot]:(i&3u)==1u?(seed&4095u):(l->required_mapping&65535u);
        previous=sys.bk_tab[slot];epoch=native_module_mapping_epoch;
        refreshes=native_module_bank_refresh_count;fastpaths=native_module_function_bank_fastpath_count;
        unchanged=native_module_bank_nochange_count;
        sys.bk_tab[slot]=(u16)bank;native_module_refresh_bank((u8)slot);
        CHECK(native_module_mapping_epoch==epoch+(previous!=bank));
        CHECK(native_module_bank_refresh_count==refreshes+1u);
        CHECK(native_module_function_bank_fastpath_count==fastpaths+(previous!=bank));
        CHECK(native_module_bank_nochange_count==unchanged+(previous==bank));
        for(j=0;j<native_module_header->module_count;++j){
            const gam4980_native_link_record_t *candidate=&native_module_links[native_module_records[j].link_first];
            u16 pc=(u16)candidate->guest_pc;
            CHECK(native_function_module_for_pc(pc)==reference_function_for_pc(pc));
            CHECK(native_function_module_for_pc((u16)(pc+1u))==reference_function_for_pc((u16)(pc+1u)));
        }
    }
    CHECK(native_module_full_rebuild_count==rebuilds && native_module_rebuild_module_visit_count==visits);
    CHECK(!memcmp(heads,native_function_page_head,sizeof(heads)));
    CHECK(!memcmp(entries,native_module_page_entries,sizeof(entries)));
    /* Rebind an actual live code-page pointer without changing its bank.
     * mem_bs still repairs the CPU page pointers; NAT needs no new epoch. */
    {const gam4980_native_link_record_t *l=&native_module_links[native_module_records[0].link_first];
     unsigned slot=l->guest_pc>>12;u8 *expected;
     sys.bk_tab[slot]=l->required_mapping&65535u;mem_bs((u8)slot);
     expected=sys.mem_r[slot*16u];CHECK(expected);sys.mem_r[slot*16u]=0;
     epoch=native_module_mapping_epoch;unchanged=native_module_bank_nochange_count;
     mem_bs((u8)slot);CHECK(sys.mem_r[slot*16u]==expected);
     CHECK(native_module_mapping_epoch==epoch && native_module_bank_nochange_count==unchanged+1u);}
    CHECK(native_module_full_rebuild_count==rebuilds && native_module_rebuild_module_visit_count==visits);
    return 0;
}
int main(int argc,char **argv)
{
    gam4980_buffers_t b={0};u32 size;unsigned i,chosen=~0u,other=~0u,expected,old_calls;
    gam4980_native_header_t *disk_header;
    CHECK(argc==4 || argc==5);
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    b.rom_8=read_file(argv[1],&size);CHECK(b.rom_8 && size==GAM4980_ROM_SIZE);
    b.rom_e=read_file(argv[2],&size);CHECK(b.rom_e && size==GAM4980_ROM_SIZE);
    b.native_alloc=allocate_test;b.native_free=free_test;
    package=read_file(argv[3],&package_size);CHECK(package);
    CHECK(gam4980_init(&b)>0);
    gam4980_set_firmware_hle_enabled(1);
    CHECK(!allocated_bytes && !allocation_calls && !gam4980_native_module_arena_size());
    native_function_preload_reset();
    CHECK(gam4980_native_modules_open(package_read,0,package_size));
    CHECK(native_module_function_package && !native_code_arena && !native_module_alloc_unit_count);
    CHECK(native_function_union_active && native_module_full_rebuild_count==1u);
    CHECK(native_module_rebuild_module_visit_count==native_module_header->module_count);
    CHECK(gam4980_native_module_resident_count()==0);
    CHECK(allocated_bytes==native_module_manifest_size && allocation_calls==1);
    CHECK(allocated_bytes==gam4980_native_module_arena_size());
    for(i=0;i<native_module_header->module_count;++i){
        CHECK(native_module_runtime[i].static_valid);
        if(chosen==~0u)chosen=i;else if(other==~0u)other=i;
    }
    CHECK(chosen!=~0u && other!=~0u);
    /* Page chains include the sole callable entry, never guard-only spans.
     * Exercise every function's mapping, including public banked functions. */
    for(i=0;i<native_module_header->module_count;++i){
        const gam4980_native_link_record_t *l=&native_module_links[native_module_records[i].link_first];
        unsigned node,seen=0,steps=0;
        sys.bk_tab[l->guest_pc>>12]=l->required_mapping&0xffffu;
        native_module_refresh_bank((u8)(l->guest_pc>>12));
        for(node=native_function_page_head[l->guest_pc>>8];node<native_module_header->module_count;
            node=native_module_runtime[node].function_page_next){
            const gam4980_native_link_record_t *n=&native_module_links[native_module_records[node].link_first];
            CHECK((n->guest_pc>>8)==(l->guest_pc>>8));
            CHECK(++steps<=native_module_header->module_count);
            if(node==i)++seen;
        }
        CHECK(seen==1);
        CHECK(native_function_module_for_pc((u16)l->guest_pc)==i);
        if(native_module_records[i].flags&GAM4980_NATIVE_MODULE_GRAPHICS){
            gam4980_set_firmware_hle_enabled(0);
            CHECK(native_function_module_for_pc((u16)l->guest_pc)==GAM4980_NATIVE_MODULE_NONE);
            CHECK(!native_module_ensure_pc((u16)l->guest_pc));
            gam4980_set_firmware_hle_enabled(1);
            CHECK(native_function_module_for_pc((u16)l->guest_pc)==i);
        }
    }
    CHECK(verify_immutable_function_mappings()==0);
    CHECK(native_module_full_rebuild_count==1u);
    {s6502_iram_asm_context_t c={0};const gam4980_native_link_record_t *l=&native_module_links[native_module_records[chosen].link_first];
     unsigned reads=read_calls,allocs=allocation_calls;
     c.pc=l->guest_pc;sys.bk_tab[c.pc>>12]=(l->required_mapping&0xffffu)^1u;
     native_module_refresh_bank((u8)(c.pc>>12));
     CHECK(native_module_firmware_entry(&c)==0 && !native_module_ensure_pc((u16)c.pc));
     CHECK(allocation_calls==allocs && read_calls==reads);
     sys.bk_tab[c.pc>>12]=l->required_mapping&0xffff;
     native_module_refresh_bank((u8)(c.pc>>12));
     CHECK(native_module_firmware_entry(&c)==0);
     CHECK(native_module_fault_entry(&c)==0);
     CHECK(allocation_calls==allocs && read_calls==reads && !c.cycles);
     CHECK(native_module_ensure_pc((u16)c.pc));
     CHECK(allocation_calls==allocs+1 && read_calls==reads+1);
     /* A deliberately rejected resident function must not retry forever. */
     CHECK(!native_module_ensure_pc((u16)c.pc));}
    CHECK(native_module_full_rebuild_count==1u);
    expected=(native_module_records[chosen].code_size+3u)&~3u;
    CHECK(native_module_runtime[chosen].code_allocation_size==expected);
    CHECK(allocated_bytes==native_module_manifest_size+expected);
    CHECK(gam4980_native_module_resident_count()==1);
    old_calls=allocation_calls;CHECK(native_module_load(chosen));CHECK(allocation_calls==old_calls);
    /* Simulate the cap filled by other resident work. The active allocation
     * must remain protected, and the other function must fall back. */
    native_module_runtime[chosen].active_calls=1;
    if (native_module_records[chosen].flags & GAM4980_NATIVE_MODULE_REGISTER) {
        unsigned pc=native_module_links[native_module_records[chosen].link_first].guest_pc;
        uint32_t *slot=native_register_entries[pc&255u];
        slot[0]=pc;slot[2]=123u;
        slot[3]=(uint32_t)(unsigned long)&native_module_runtime[chosen].active_calls;
        native_module_release_units(chosen);
        CHECK(slot[2]==123u && native_module_runtime[chosen].loaded);
        native_module_runtime[chosen].active_calls=0;
        fail_free=1;native_module_release_units(chosen);fail_free=0;
        CHECK(slot[2]==123u && native_module_runtime[chosen].loaded);
        native_module_runtime[chosen].active_calls=1;
    }
    native_module_resident_code_bytes=GAM4980_NATIVE_CODE_ARENA_SIZE;
    CHECK(!native_module_load(other));
    CHECK(native_module_runtime[chosen].code);
    native_module_resident_code_bytes=expected;
    native_module_runtime[chosen].active_calls=1;
    CHECK(!native_module_evict_oldest());
    gam4980_native_modules_close();CHECK(native_module_header && allocated_bytes);
    fail_alloc=1;CHECK(!native_module_load(other));
    CHECK(native_module_runtime[chosen].loaded && native_module_runtime[chosen].code);
    fail_alloc=0;native_module_runtime[chosen].active_calls=0;
    /* A recently used private body gets a second chance over a cold body.
     * Synthetic cold allocation has no code/size and is safe to release. */
    {
        native_module_runtime[chosen].stamp=1u;
        native_module_runtime[chosen].register_referenced=1u;
        native_module_runtime[other].loaded=1u;
        native_module_runtime[other].stamp=2u;
        native_module_clock=10u;
        CHECK(native_module_evict_oldest());
        CHECK(native_module_runtime[chosen].loaded);
        CHECK(!native_module_runtime[other].loaded);
        CHECK(!native_module_runtime[chosen].register_referenced);
        CHECK(native_module_runtime[chosen].stamp==11u);
    }
    fail_free=1;CHECK(!native_module_evict_oldest());
    gam4980_native_modules_close();CHECK(native_module_header && native_module_runtime[chosen].code);
    fail_free=0;
    /* Same virtual entry replaced by another bank must survive old release. */
    if (native_module_records[chosen].flags & GAM4980_NATIVE_MODULE_REGISTER) {
        unsigned pc=native_module_links[native_module_records[chosen].link_first].guest_pc;
        uint32_t *slot=native_register_entries[pc&255u];
        slot[0]=pc;slot[2]=456u;slot[3]=0u;
        CHECK(native_module_evict_oldest());CHECK(slot[2]==456u);
        slot[2]=0u;
    } else CHECK(native_module_evict_oldest());
    CHECK(allocated_bytes==native_module_manifest_size);
    CHECK(!native_module_runtime[chosen].loaded && !native_module_runtime[chosen].code);
    CHECK(native_module_full_rebuild_count==1u);
#ifdef GAM4980_STATIC_NATIVE_GAME
    {unsigned epoch,rebuilds,index;
     s6502_static_native_bound=1u;sys.bk_tab[5]=0x0200u;native_module_rebuild_page_entries();
     CHECK(!native_function_union_active);
     CHECK(native_module_page_entries[0x5f]==(u32)(unsigned long)test_static_entry);
     epoch=native_module_mapping_epoch;rebuilds=native_module_full_rebuild_count;
     native_module_refresh_bank(5u);CHECK(native_module_mapping_epoch==epoch && native_module_full_rebuild_count==rebuilds);
     for(index=0;index<native_module_header->module_count;++index)
         if(native_module_links[native_module_records[index].link_first].guest_pc==0x5fc4u)break;
     CHECK(index<native_module_header->module_count);
     sys.bk_tab[5]=0x0eb0u;native_module_refresh_bank(5u);
     CHECK(native_module_full_rebuild_count==rebuilds+1u && native_module_mapping_epoch==epoch+1u);
     CHECK(native_function_module_for_pc(0x5fc4u)==index);
     CHECK(native_module_page_entries[0x5f]!=(u32)(unsigned long)test_static_entry);
     sys.bk_tab[5]=0x0200u;native_module_refresh_bank(5u);
     CHECK(native_module_page_entries[0x5f]==(u32)(unsigned long)test_static_entry);
     CHECK(native_function_module_for_pc(0x5fc4u)==GAM4980_NATIVE_MODULE_NONE);
     s6502_static_native_bound=0u;native_module_rebuild_page_entries();CHECK(native_function_union_active);}
#endif
    expected=native_match_at(native_module_records[other].match_first)->physical_pc;
    gam4980_native_modules_close();CHECK(!allocated_bytes);
    native_function_preload_reset();native_function_preload_note(expected);
    CHECK(gam4980_native_modules_open(package_read,0,package_size));
    CHECK(gam4980_native_module_resident_count()==1 && native_module_runtime[other].loaded);
    CHECK(native_module_preloaded_count==1);
    CHECK(native_function_union_active && native_module_full_rebuild_count==1u);
    /* Cooldown membership is checked dynamically: clearing or setting it
     * must not require rebuilding the immutable catalogue index. */
    {const gam4980_native_link_record_t *l=&native_module_links[native_module_records[other].link_first];
     sys.bk_tab[l->guest_pc>>12]=l->required_mapping&65535u;
     native_module_refresh_bank((u8)(l->guest_pc>>12));
     native_module_runtime[other].cooldown_until_batch=native_module_batch_clock+2u;
     CHECK(native_function_module_for_pc((u16)l->guest_pc)==GAM4980_NATIVE_MODULE_NONE);
     native_module_runtime[other].cooldown_until_batch=0u;
     CHECK(native_function_module_for_pc((u16)l->guest_pc)==other);
     CHECK(native_function_entry_cache[(l->guest_pc^(l->guest_pc>>8))&255u][1]==other+1u);
     sys.bk_tab[l->guest_pc>>12]^=1u;
     CHECK(native_function_module_for_pc((u16)l->guest_pc)!=other);
     sys.bk_tab[l->guest_pc>>12]^=1u;
     CHECK(native_function_module_for_pc((u16)l->guest_pc)==other);
     native_module_runtime[other].static_valid=0u;
     CHECK(native_function_module_for_pc((u16)l->guest_pc)==GAM4980_NATIVE_MODULE_NONE);
     native_module_runtime[other].static_valid=1u;
     CHECK(native_module_full_rebuild_count==1u);}
    /* A same-page guard or interior address must not be published callable. */
    {s6502_iram_asm_context_t c={0};const gam4980_native_link_record_t *l=&native_module_links[native_module_records[other].link_first];
     c.pc=l->guest_pc+1;sys.bk_tab[c.pc>>12]=l->required_mapping&0xffff;
     CHECK(native_module_firmware_entry(&c)==0);}
    gam4980_native_modules_close();CHECK(!allocated_bytes);
    /* Bad payload does not leave a partially allocated function resident. */
    native_function_preload_reset();CHECK(gam4980_native_modules_open(package_read,0,package_size));
    i=native_module_records[chosen].code_offset;package[i]^=1;
    CHECK(!native_module_load(chosen));CHECK(!native_module_runtime[chosen].code);
    CHECK(allocated_bytes==native_module_manifest_size);package[i]^=1;
    gam4980_native_modules_close();CHECK(!allocated_bytes);
    /* A malformed header must not allocate an arena or leak the manifest. */
    disk_header=(gam4980_native_header_t *)(void *)package;expected=disk_header->payload_offset;
    disk_header->payload_offset=package_size+1u;
    CHECK(!gam4980_native_modules_open(package_read,0,package_size));CHECK(!allocated_bytes);
    disk_header->payload_offset=expected;
    /* ABI 4 remains loadable, ABI 5 extends only FUNCTION firmware packages.
     * Unknown ABI versions must fail before allocating anything. */
    {u32 saved_abi=disk_header->abi_version;
     disk_header->abi_version=99u;
     CHECK(!gam4980_native_modules_open(package_read,0,package_size));CHECK(!allocated_bytes);
     disk_header->abi_version=GAM4980_NATIVE_GRAPHICS_ABI_VERSION;
     CHECK(gam4980_native_modules_open(package_read,0,package_size));
     gam4980_native_modules_close();CHECK(!allocated_bytes);
     disk_header->abi_version=saved_abi;}
    {gam4980_native_module_record_t *m=(gam4980_native_module_record_t *)(void *)(package+disk_header->module_offset);
     u32 saved_flags=m->flags,saved_hash=disk_header->manifest_hash,saved_abi=disk_header->abi_version;
     disk_header->abi_version=GAM4980_NATIVE_ABI_VERSION;
     m->flags|=GAM4980_NATIVE_MODULE_GRAPHICS;
     disk_header->manifest_hash=native_module_hash(package+64u,disk_header->payload_offset-64u);
     CHECK(!gam4980_native_modules_open(package_read,0,package_size));CHECK(!allocated_bytes);
     disk_header->abi_version=GAM4980_NATIVE_GRAPHICS_ABI_VERSION;
     m->flags=(saved_flags|GAM4980_NATIVE_MODULE_TEXT)&~GAM4980_NATIVE_MODULE_GRAPHICS;
     disk_header->manifest_hash=native_module_hash(package+64u,disk_header->payload_offset-64u);
     CHECK(!gam4980_native_modules_open(package_read,0,package_size));CHECK(!allocated_bytes);
     m->flags=saved_flags;disk_header->manifest_hash=saved_hash;disk_header->abi_version=saved_abi;}
    if(disk_header->format_version==GAM4980_NATIVE_COMPACT_FORMAT_VERSION) {
        u32 *ref=(u32 *)(void *)(package+disk_header->match_offset);
        u32 saved=*ref,saved_hash=disk_header->manifest_hash;
        *ref=disk_header->reloc_count;
        disk_header->manifest_hash=native_module_hash(package+64u,disk_header->payload_offset-64u);
        CHECK(!gam4980_native_modules_open(package_read,0,package_size));CHECK(!allocated_bytes);
        *ref=saved;disk_header->manifest_hash=saved_hash;
    }
    if(argc==5) {
        free(package);package=read_file(argv[4],&package_size);CHECK(package);
        CHECK(gam4980_native_modules_open(package_read,0,package_size));
        CHECK(!native_module_function_package && native_code_arena_owned);
        CHECK(native_code_arena_size==GAM4980_NATIVE_CODE_ARENA_SIZE);
        CHECK(allocated_bytes==GAM4980_NATIVE_CODE_ARENA_SIZE+native_module_manifest_size);
        CHECK(native_module_load(0));
        CHECK(native_module_runtime[0].code>=native_module_code_base);
        {unsigned slot=5u,epoch=native_module_mapping_epoch,rebuilds=native_module_full_rebuild_count;
         native_module_refresh_bank((u8)slot);CHECK(native_module_mapping_epoch==epoch);
         CHECK(native_module_full_rebuild_count==rebuilds);
         sys.bk_tab[slot]^=1u;native_module_refresh_bank((u8)slot);
         CHECK(native_module_mapping_epoch==epoch+1u && native_module_full_rebuild_count==rebuilds+1u);}
        gam4980_native_modules_close();CHECK(!allocated_bytes);
    }
    gam4980_deinit();CHECK(!allocated_bytes);
    free(package);free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);
    printf("function loader: exact allocation/preload/demand, active eviction guard, failure cleanup PASS (%u allocations/%u frees)\n",allocation_calls,free_calls);
    return 0;
}

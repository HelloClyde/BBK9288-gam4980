/* Compare the pageable graphics implementation with the existing verified
 * core algorithms, including their real memory mapper and LCD dirty state.
 * The core's ROM-vs-HLE fixtures provide the independent 6502 reference. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DL_DOWN
#define _RLS_
#define GAM4980_ENABLE_AOT
#define GAM4980_ENABLE_FIRMWARE_HLE
#define GAM4980_ENABLE_AGGRESSIVE_REGION_HLE
#include "../src/gam4980_core.c"

typedef struct host_graphics_context {
    uint32_t pc, ac, ix, iy, sp, status, cycle_budget, cycles;
    uintptr_t ram, pages, read8, graphics, native_shared_metrics;
    uintptr_t dirty, lcd_write_calls, lcd_changed_writes;
} host_graphics_context_t;
typedef struct host_graphics_services {
    uint32_t version;
    uintptr_t framebuffer, expand_lut, page3, banks, write8_resolved;
    uintptr_t clock, poll, metrics;
    uintptr_t batch, synced_frame, synced_rows;
    uintptr_t picture_state, picture_complete;
} host_graphics_services_t;
#define FIRMWARE_NATIVE_HOST_TEST
#define s6502_iram_asm_context_t host_graphics_context_t
#define firmware_native_graphics_services_t host_graphics_services_t
#include "../src/firmware_native_graphics.c"
#undef s6502_iram_asm_context_t
#undef firmware_native_graphics_services_t

static uint32_t seed = 0x876b682du;
static uint32_t picture_test_state, picture_test_calls;
static void picture_test_complete(uintptr_t ram)
{
    if (ram != (uintptr_t)sys.ram) abort();
    ++picture_test_calls;
}
static uint32_t next_random(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
static uint32_t resolved_write(uint16_t address, uint8_t value)
{
    uint32_t physical = PA(address);
    mem_write(address, value);
    return physical < 0x8000u ? physical : ~0u;
}
static void zp_set(unsigned offset, unsigned value)
{
    sys.ram[offset] = (uint8_t)value;
    sys.ram[offset + 1] = (uint8_t)(value >> 8);
}
static uint32_t reference(host_graphics_context_t *c)
{
    s6502_hle_direct_result_t d;
    s6502_hle_region_result_t r;
    int ok = 0, region = 0;
    uint32_t budget = c->cycle_budget - c->cycles;
    if ((c->status & 8u) && c->pc != 0x859eu) return 0;
    switch (c->pc) {
    case 0x682d: ok=s6502_firmware_hle_picture_head(c->ac,c->iy,c->sp,c->status,budget,&d);break;
    case 0x690f: ok=s6502_firmware_hle_picture_resume(c->ac,c->ix,c->iy,c->sp,c->status,budget,&d);break;
    case 0x876b: ok=s6502_firmware_hle_graphics_address(c->ix,c->iy,c->sp,c->status,budget,&d);break;
    case 0x8039: if(c->iy)return 0;ok=s6502_firmware_hle_hline_middle(c->ac,c->ix,c->iy,c->sp,c->status,budget,&d);break;
    case 0x5351: case 0x5801: ok=s6502_firmware_hle_part_picture_row(c->pc==0x5801,c->ac,c->iy,c->sp,c->status,budget,&d);break;
    case 0x859e: ok=s6502_firmware_hle_pixel_tail(c->sp,c->status,budget,&d);break;
    case 0x6988: region=1;ok=s6502_firmware_hle_shift_region(c->sp,c->status,budget,&r,s6502_firmware_hle_shift_row_cycles,s6502_firmware_hle_shift_prefix);break;
    case 0x5c5d: region=1;ok=s6502_firmware_hle_bitmap_region(c->sp,c->status,budget,&r,s6502_firmware_hle_bitmap_row_cycles);break;
    case 0x6b1a: case 0x6ba4: region=1;ok=s6502_firmware_hle_picture_tail(c->pc==0x6b1a,c->sp,c->status,budget,&r);break;
    }
    if(ok<=0)return 0;
    if(region){c->pc=r.pc;c->ac=r.ac;c->ix=r.ix;c->iy=r.iy;c->sp=r.sp;c->status=r.status;c->cycles+=r.cycles;return r.cycles;}
    c->pc=d.pc;c->ac=d.ac;c->ix=d.ix;c->iy=d.iy;c->sp=d.dt;c->status=d.status;c->cycles+=d.cycles;return d.cycles;
}
static int same_cpu(const host_graphics_context_t *a,const host_graphics_context_t *b)
{
    return a->pc==b->pc&&a->ac==b->ac&&a->ix==b->ix&&a->iy==b->iy&&
        a->sp==b->sp&&a->status==b->status&&a->cycles==b->cycles;
}
static int load_exact(const char *path,uint8_t *data,size_t size)
{
    FILE *file=fopen(path,"rb");int ok;
    if(!file)return 0;
    ok=fread(data,1,size,file)==size&&fgetc(file)==EOF;fclose(file);return ok;
}
int main(int argc,char **argv)
{
    const unsigned entries[]={0x682d,0x690f,0x6988,0x5c5d,0x6b1a,0x6ba4,0x876b,0x8039,0x5351,0x5801,0x859e};
    gam4980_buffers_t b={0};
    host_graphics_services_t services={0};
    uint8_t initial[0x8000],expected[0x8000];
    uint8_t *shadow_pages[256];
    uint32_t frame[4800],reference_frame[4800],metrics[4]={0};
    unsigned case_id,entry_id,checks=0,fast_rows=0;
    if(argc!=3)return 2;
    b.ram=malloc(GAM4980_RAM_SIZE);b.flash=malloc(GAM4980_FLASH_SIZE);
    b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    if(!b.ram||!b.flash||!b.rom_8||!b.rom_e||!load_exact(argv[1],b.rom_8,GAM4980_ROM_SIZE)||!load_exact(argv[2],b.rom_e,GAM4980_ROM_SIZE)||gam4980_init(&b)<=0)return 2;
    for(unsigned i=1;i<8;++i){sys.bk_tab[i]=i;mem_bs(i);}
    services.version=FW_GRAPHICS_SERVICE_VERSION;services.page3=(uintptr_t)s6502_page3;
    services.banks=(uintptr_t)sys.bk_tab;services.write8_resolved=(uintptr_t)resolved_write;
    services.framebuffer=(uintptr_t)frame;services.metrics=(uintptr_t)metrics;
    services.synced_frame=(uintptr_t)native_graphics_synced_frame;
    services.synced_rows=(uintptr_t)native_graphics_synced_rows;
    for(entry_id=0;entry_id<sizeof(entries)/sizeof(entries[0]);++entry_id){
        unsigned accepted=0;
        for(case_id=0;case_id<1000;++case_id){
            host_graphics_context_t c={0},ref;
            uint32_t got,want;int want_dirty;
            unsigned destination=case_id%37==0?0x400:0x400+(next_random()%0xbb0),source=0x3000+(next_random()&0x3ff);
            /* A second guest bank aliases physical LCD RAM. Source==dest
             * exercises sequential overlap rather than memcpy semantics. */
            sys.bk_tab[7]=case_id%3==0?0:7;mem_bs(7);
            if(case_id%3==0)destination+=0x7000;
            if(case_id%7==0)source=destination;
            for(unsigned i=0;i<0x8000;++i)sys.ram[i]=(uint8_t)next_random();
            sys.ram[_SYSCON]=0;sys.ram[0x3e5]=1;sys.ram[0x3e6]=0;sys.ram[0x3e7]=4;sys.ram[0x3e8]=0;sys.ram[0x3e9]=0x10;
            zp_set(0x28,0x2400);zp_set(0x2f,source);zp_set(0x31,destination);zp_set(0x38,destination);zp_set(0x3a,destination);zp_set(0x20e9,destination);
            sys.ram[0x2081]=(uint8_t)(next_random()%160);sys.ram[0x2082]=(uint8_t)(next_random()%96);sys.ram[0x2083]=(uint8_t)(next_random()%160);
            sys.ram[0x20cf]=(uint8_t)(case_id%9);sys.ram[0x20da]=(uint8_t)(1+case_id%3);sys.ram[0x20de]=1;
            sys.ram[0x20e7]=3+(case_id&1);sys.ram[0x20e8]=(uint8_t)(case_id%21);sys.ram[0x20d8]=1+(case_id%20);
            if(entries[entry_id]==0x6988 && case_id%2==0) {
                destination=0x800u;source=0x3000u;
                zp_set(0x2f,source);zp_set(0x3a,destination);
                zp_set(0x38,destination-1u);zp_set(0x20e9,destination);
                sys.ram[0x2082]=20u;sys.ram[0x2081]=0;
                sys.ram[0x2083]=159u;sys.ram[0x20cf]=case_id%8;
                sys.ram[0x20e8]=20u;sys.ram[0x20da]=1u+case_id%4;
            }
            sys.ram[0x2400]=sys.ram[0x2082];sys.ram[0x2401]=159;sys.ram[0x2402]=95;zp_set(0x2403,source);sys.ram[0x2405]=0;
            c.pc=entries[entry_id];c.ac=sys.ram[0x2081];c.ix=(uint8_t)next_random();c.iy=0;c.sp=(uint8_t)next_random();c.status=(uint8_t)next_random()&~8u;
            if(case_id%31==0)c.status|=8u;
            memcpy(shadow_pages,sys.mem_r,sizeof(shadow_pages));
            if(case_id&1)shadow_pages[source>>8]=0; /* real callback fallback */
            c.ram=(uintptr_t)sys.ram;c.pages=(uintptr_t)shadow_pages;c.read8=(uintptr_t)mem_read;c.graphics=(uintptr_t)&services;
            c.dirty=(uintptr_t)&lcd_dirty;
            services.framebuffer=(case_id%17==0)?0:(uintptr_t)frame;
            memset(metrics,0,sizeof(metrics));
            c.cycles=7;c.cycle_budget=c.cycles+(case_id%5==0?0:case_id%5==1?1:case_id%5==2?256:case_id%5==3?2048:10000);
            sys.ram[0x100|(uint8_t)(c.sp+1)]=0xff;sys.ram[0x100|(uint8_t)(c.sp+2)]=0x3f;
            memcpy(initial,sys.ram,0x8000);ref=c;lcd_dirty=0;
            want=reference(&ref);want_dirty=lcd_dirty;memcpy(expected,sys.ram,0x8000);
            memcpy(sys.ram,initial,0x8000);lcd_dirty=0;
            memset(frame,0xff,sizeof(frame));memset(reference_frame,0xff,sizeof(reference_frame));
            for(unsigned p=0x401;p<=0x1000;++p)fw_gfx_lcd_mirror(sys.ram,frame,0,p);
            for(unsigned y=0;y<96;++y){
                uint8_t row[20];
                for(unsigned x=0;x<20;++x)row[x]=sys.ram[fw_gfx_lcd_address(x,y)];
                gam4980_native_graphics_row_presented(y,row);
            }
            picture_test_state=3u;picture_test_calls=0u;
            services.picture_state=(uintptr_t)&picture_test_state;
            services.picture_complete=(uintptr_t)picture_test_complete;
            got=firmware_native_graphics(&c);
            fast_rows+=metrics[3];
            {
                unsigned pc=entries[entry_id];
                unsigned done=got && (pc==0x6b1a || pc==0x6ba4 || pc==0x5c5d) && !sys.ram[0x20da];
                if(picture_test_calls!=done || (done && picture_test_state)) {
                    fprintf(stderr,"picture completion event mismatch\n");return 1;
                }
                if(got && pc==0x682d) {
                    unsigned full=!sys.ram[0x2081] && !sys.ram[0x2082] && sys.ram[0x2083]>=158 && sys.ram[0x2084]>=95;
                    if(picture_test_state!=(1u | (full?2u:0u)))return 1;
                }
            }
            if(services.batch){fprintf(stderr,"dangling NAT row batch\n");return 1;}
            if(services.framebuffer)for(unsigned y=0;y<96;++y){
                uint8_t row[20];
                for(unsigned x=0;x<20;++x)row[x]=sys.ram[fw_gfx_lcd_address(x,y)];
                /* A synchronized row must match actual pixels, not merely
                 * have matching guest bytes in two stale snapshots. */
                if(gam4980_native_graphics_row_synced(y,row)){
                    uint32_t refrow[4800];
                    memcpy(refrow,frame,sizeof(frame));
                    fw_gfx_lcd_span(sys.ram,refrow,0,0,19,y,row,255,255);
                    if(memcmp(frame,refrow,sizeof(frame))){fprintf(stderr,"false synced row %u\n",y);return 1;}
                }
            }
            for(unsigned p=0x401;p<=0x1000;++p)fw_gfx_lcd_mirror(expected,reference_frame,0,p);
            if(got!=want||!same_cpu(&c,&ref)||lcd_dirty!=want_dirty||memcmp(sys.ram,expected,0x8000)||(services.framebuffer&&memcmp(frame,reference_frame,sizeof(frame)))||(!services.framebuffer&&(metrics[0]||metrics[1]))){
                unsigned first=0;while(first<0x8000&&sys.ram[first]==expected[first])++first;
                fprintf(stderr,"entry=%04x case=%u cycle=%u/%u pc=%04x/%04x dirty=%d/%d RAM=%04x LCD=%d\n",entries[entry_id],case_id,got,want,c.pc,ref.pc,lcd_dirty,want_dirty,first,memcmp(frame,reference_frame,sizeof(frame)));return 1;
            }
            accepted+=got!=0;++checks;
        }
        if(!accepted){fprintf(stderr,"entry %04x never accepted\n",entries[entry_id]);return 1;}
        printf("NAT graphics %04x: 1000 cases, accepted %u\n",entries[entry_id],accepted);
    }
    gam4980_deinit();free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);
    if(!fast_rows){fprintf(stderr,"bulk blitter never exercised\n");return 1;}
    printf("NAT graphics: %u exact CPU/RAM/cycle/LCD cases PASS; bulk rows %u\n",checks,fast_rows);return 0;
}

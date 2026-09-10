#include <stdint.h>
#include <string.h>
typedef struct {
 uint32_t pc,ac,ix,iy,sp,status,cycle_budget,cycles;
 uintptr_t ram,pages,read8,graphics,dirty;
} s6502_iram_asm_context_t;
typedef struct {
 uint32_t version;
 uintptr_t framebuffer,expand_lut,page3,banks,write8_resolved,clock,poll,metrics;
 uintptr_t batch,synced_frame,synced_rows,picture_state,picture_complete,read_physical,public_metrics;
} firmware_native_graphics_services_t;
#define FW_GRAPHICS_SERVICE_VERSION 5
#define FIRMWARE_NATIVE_HOST_TEST
#include "../src/firmware_native_public_graphics.c"
static uint8_t *test_ram,*test_e,*test_8;
static uint8_t read_virtual(uint16_t a){return test_ram[a];}
static uint8_t read_physical(uint32_t a){return a>=0xe00000?test_e[a-0xe00000]:test_8[a-0x800000];}
__declspec(dllexport) unsigned run_public(uint8_t *ram,uint8_t *rom8,uint8_t *rome,unsigned pc,unsigned a,unsigned *out)
{
 uint8_t *pages[256];unsigned i;int dirty=0;
 s6502_iram_asm_context_t c={0};firmware_native_graphics_services_t g={0};
 test_ram=ram;test_e=rome;test_8=rom8;
 for(i=0;i<256;++i)pages[i]=ram+i*256;
 c.pc=pc;c.ac=a;c.sp=253;c.status=48;c.cycle_budget=10000000;
 c.ram=(uintptr_t)ram;c.pages=(uintptr_t)pages;c.read8=(uintptr_t)read_virtual;c.graphics=(uintptr_t)&g;c.dirty=(uintptr_t)&dirty;
 g.version=5;g.page3=(uintptr_t)(ram+0x300);g.read_physical=(uintptr_t)read_physical;
 i=firmware_native_public_graphics(&c);
 out[0]=c.pc;out[1]=c.sp;out[2]=c.cycles;
 return i;
}

/* Prove the complete glyph's mapping once, before touching pixels. */
typedef struct { s6502_iram_asm_context_t *c; uint8_t *ram; uint8_t **pages; } fnt_flat_env;
FNT_INLINE int ft_begin(fnt_flat_env *f,s6502_iram_asm_context_t *c,uint32_t wide)
{
    uint32_t i,source,x,y,d,a,n;
    firmware_native_graphics_services_t *g=(firmware_native_graphics_services_t*)(FNT_POINTER)c->graphics;
    uint8_t *p=(uint8_t*)(FNT_POINTER)g->page3;
    f->c=c;f->ram=(uint8_t*)(FNT_POINTER)c->ram;f->pages=(uint8_t**)(FNT_POINTER)c->pages;
    if(!f->pages || g->framebuffer || p[0xe5]!=1 || p[0xe6] || p[0xe7]!=4 || p[0xe8] || p[0xe9]!=16)return 0;
    if(f->pages[0x20]!=f->ram+0x2000)return 0;
    for(i=4;i<=16;++i)if(f->pages[i]!=f->ram+(i<<8))return 0;
    x=f->ram[0x2081];y=f->ram[0x2082];n=wide?32u:16u;
    if(c->ix>=n || (wide && (c->ix&1u)) || x>(wide?144u:152u) || y+(n-c->ix)/(wide?2u:1u)>96u)return 0;
    d=f->ram[0x3a]|((uint32_t)f->ram[0x3b]<<8);
    a=f->ram[0x38]|((uint32_t)f->ram[0x39]<<8);
    if(d!=(x<8u?(y<=65u?0x400u+(65u-y)*32u:0x400u+y*32u):
            (y<=65u?0x400u+(65u-y)*32u:0x400u+y*32u)+(x>>3)-1u))return 0;
    if(x<8u && a!=fw_gfx_lcd_address(0,y))return 0;
    source=f->ram[0x2f]|((uint32_t)f->ram[0x30]<<8);
    if(source<0x2100u || source+n>0x10000u)return 0;
    for(i=(source+c->ix)>>8;i<=(source+n-1u)>>8;++i)if(!f->pages[i])return 0;
    return 1;
}
FNT_INLINE uint8_t ft_read(fnt_flat_env *f,uint16_t a)
{
    if(a>=0x2000u && a<0x2100u)return f->ram[a];
    return f->pages[a>>8][a&255u];
}
FNT_INLINE void ft_write(fnt_flat_env *f,uint16_t a,uint8_t v)
{
    uint8_t old=f->ram[a];f->ram[a]=v;
    if(a<=0x1000u && a>0x400u) {
        if(f->c->lcd_write_calls)++*(uint32_t*)(FNT_POINTER)f->c->lcd_write_calls;
        if(old!=v) {
            if(f->c->dirty)*(int*)(FNT_POINTER)f->c->dirty=1;
            if(f->c->lcd_changed_writes)++*(uint32_t*)(FNT_POINTER)f->c->lcd_changed_writes;
        }
    }
}
#define FW_CURSOR_NAME ft_cursor
#define FW_CURSOR_CONTEXT fnt_flat_env *
#define FW_CURSOR_READ(a) ft_read(c, (a))
#define FW_CURSOR_WRITE(a,v) ft_write(c, (a), (v))
#define FW_CURSOR_CAN_FOLD 1
#include "firmware_native_text_cursor.h"
#include "firmware_native_text_flat_generated.h"

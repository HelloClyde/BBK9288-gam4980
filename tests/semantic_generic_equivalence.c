#include <stdio.h>
#include <string.h>
#include "../src/gam4980_core.c"

static unsigned char ram_test[65536], initial[65536], reference[65536];
static void test_write(uint16_t addr, uint8_t value) { ram_test[addr] = value; }

int main(void)
{
    unsigned kind, trial, i, count = 0;
    for (i = 0; i < 256; ++i) {
        sys.mem_r[i] = ram_test + i * 256;
        sys.mem_iw[i] = test_write;
    }
    sys.ram = ram_test;
    s6502_stack_ram = ram_test;
    s6502_game_aot_banks = sys.bk_tab;
    for (kind = 18; kind <= 25; ++kind) {
        const c6502_template_spec_t *spec = &c6502_template_specs[kind - 1];
        unsigned char code[256] = {0};
        memcpy(code, spec->bytes, spec->size);
        if (kind <= 19) {
            code[2]=0x40; code[8]=0x41; code[4]=0x44; code[10]=0x45;
            code[6]=0x48; code[12]=0x49;
        } else if (kind <= 21) {
            code[1]=0x40; code[5]=0x41; code[3]=0x48; code[7]=0x49;
        } else if (kind == 22) {
            code[3]=code[8]=0x28; code[5]=0x48; code[10]=0x49;
        } else if (kind == 23) {
            code[3]=0x40; code[8]=0x41; code[5]=code[10]=0x28;
        }
        code[spec->size]=0x4c; code[spec->size+1]=0; code[spec->size+2]=0x60;
        for (trial=0; trial<256; ++trial) {
            s6502_t a={0}, b;
            unsigned ca, cb;
            if (kind >= 22) code[1]=(uint8_t)trial;
            if (kind == 21) { /* Test overlapping COPY16 destinations. */
                code[3]=(trial&1)?0x41:0x48; code[7]=code[3]+1;
            }
            if (!s6502_game_aot_template_at(code, spec->size, kind)) return 2;
            for (i=0;i<sizeof(initial);++i) initial[i]=(uint8_t)(i*37+trial*13);
            initial[0x28]=0xff; initial[0x29]=0x30;
            initial[_SYSCON]=0;
            /* For stores include pointer aliasing in the destination. */
            if (kind==23 && trial%3==0) {
                unsigned base=(uint16_t)(0x28-trial);
                initial[0x28]=(uint8_t)base; initial[0x29]=(uint8_t)(base>>8);
            }
            memset(s6502_game_aot_hash,0,sizeof(s6502_game_aot_hash));
            s6502_game_aot_entry_count=0;
            s6502_game_aot_add_entry(code,256,0,(uint8_t)(7+kind),1);
            s6502_game_aot_code_base=code;
            s6502_game_aot_entry_limit=1;
            s6502_game_aot_bank_mask=1<<5;
            s6502_game_aot_banks[5]=0x20d;
            s6502_game_aot_requested=1;
            s6502_game_aot_enabled=1;
            sys.mem_r[0x50]=code;
            a.pc=0x5000; a.ac=(uint8_t)trial; a.ix=13; a.iy=17;
            a.sp=0xd0; a.status=(uint8_t)trial;
            b=a;
            memcpy(ram_test,initial,sizeof(initial));
            s6502_game_aot_semantic_mask=0;
            ca=s6502_exec(&a,1);
            memcpy(reference,ram_test,sizeof(reference));
            memcpy(ram_test,initial,sizeof(initial));
            s6502_game_aot_semantic_mask=1u<<(kind-1);
            cb=s6502_exec(&b,1);
            if(ca!=cb || a.pc!=b.pc || a.ac!=b.ac || a.ix!=b.ix ||
               a.iy!=b.iy || a.sp!=b.sp || a.status!=b.status ||
               memcmp(reference,ram_test,sizeof(reference))) {
                printf("FAIL kind=%u trial=%u cycles=%u/%u flags=%02x/%02x\n",
                       kind,trial,ca,cb,a.status,b.status); return 1;
            }
            ++count;
        }
    }
    printf("PASS cases=%u\n",count);
    return 0;
}

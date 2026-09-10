#define main arithmetic_test_main
#include "firmware_hle_multiply_test.c"
#undef main
#define FIRMWARE_NATIVE_HOST_TEST
#define s6502_iram_asm_context_t host_multiply_context
#include "../src/firmware_native_runtime.c"
#include "../src/firmware_native_float.c"
#include "../src/firmware_native_float_add.c"
#include "../src/firmware_native_conversion.c"
#include "../src/firmware_native_switch.c"
#include "../src/firmware_native_string.c"
#include "../src/firmware_native_strchr.c"
#include "../src/firmware_native_strcmp.c"
#undef s6502_iram_asm_context_t
#undef FIRMWARE_NATIVE_HOST_TEST

int main(int argc,char **argv)
{
    gam4980_buffers_t b={0};unsigned t,i,mode;int result=1;
    uint8_t *initial=malloc(GAM4980_RAM_SIZE),*expected=malloc(GAM4980_RAM_SIZE);
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);
    b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    if(argc!=3 || !initial || !expected || !b.ram || !b.flash || !b.rom_8 || !b.rom_e ||
       !load_exact(argv[1],b.rom_8,GAM4980_ROM_SIZE) || !load_exact(argv[2],b.rom_e,GAM4980_ROM_SIZE) ||
       gam4980_init(&b)<=0)return 2;
    gam4980_set_firmware_hle_enabled(0);memset(s6502_aot_validation,2,sizeof(s6502_aot_validation));
    for(mode=0;mode<45u;++mode)for(t=0;t<20000u;++t){
        unsigned left=next_random(),right=next_random(),cycles=0,sp=8u+t%234u;
        uint32_t (*native)(host_multiply_context *)=mode==1u || mode==3u || mode==4u || mode>=6u?firmware_native_divide:firmware_native_multiply;
        if(mode==8u)native=firmware_native_runtime;
        if(mode>=9u)native=firmware_native_float;
        if(mode>=19u)native=firmware_native_float_add;
        if(mode>=22u)native=mode==40u?firmware_native_float:firmware_native_conversion;
        if(mode==41u)native=firmware_native_switch;
        if(mode==42u)native=firmware_native_string;
        if(mode==43u)native=firmware_native_strchr;
        if(mode==44u)native=firmware_native_strcmp;
        s6502_t saved,ref;host_multiply_context c={0},unchanged;
        if(t<16u)left=0;else if(t<32u)right=0;
        else if(t<40u)left=0x80000000u;else if(t<48u)right=0x80000000u;
        else if(t<56u)left=right=0xffffffffu;else if(t<64u)right=1u;
        for(i=0;i<0x1000u;++i)sys.ram[i]=(uint8_t)next_random();
        for(i=0;i<4u;++i){sys.ram[0x103u+sp+i]=(uint8_t)(left>>(i*8u));sys.ram[0x107u+sp+i]=(uint8_t)(right>>(i*8u));}
        if(mode==1)for(i=0;i<4u;++i){sys.ram[0x107u+sp+i]=t%2u?0:(uint8_t)next_random();sys.ram[0x10bu+sp+i]=(uint8_t)(right>>(i*8u));}
        if(mode>=2){
            unsigned lp=0x1100u+(t&255u),rp=0x1400u+((t*7u)&255u);sp=18u+t%238u;
            if(mode>=11u)rp=0xa00u+((t*7u)&255u);
            if(mode==4u || mode==7u)sp=19u+t%237u;
            sys.ram[0x20]=(uint8_t)lp;sys.ram[0x21]=(uint8_t)(lp>>8);sys.ram[0x23]=(uint8_t)rp;sys.ram[0x24]=(uint8_t)(rp>>8);
            for(i=0;i<4u;++i){sys.ram[lp+i]=(uint8_t)(left>>(8u*i));sys.ram[rp+i]=(uint8_t)(right>>(8u*i));}
            if(mode>=9u)for(i=t%5u;i<4u;++i)sys.ram[rp+i]=sys.ram[lp+i];
        }
        sys.ram[_SYSCON]=0;sys.ram[0x2a]=(uint8_t)t;sys.ram[0x2b]=6;
        sys.cpu.pc=mode==7?0xd604:mode==6?0xd435:mode==5?0xd6f5:mode==4?0xdcc8:mode==3?0xdc83:mode==2?0xdcf7:mode==1?0xd0a8:0xd201;sys.cpu.sp=sp;sys.cpu.status=(uint8_t)(next_random()&~8u);
        if(mode==8u){sp=18u+t%220u;sys.cpu.sp=sp;sys.cpu.pc=0xd752;sys.cpu.ac=1u+4u*(t%3u);}
        if(mode>=9u)sys.cpu.pc=mode==12u?0xe524:mode==11u?0xe282:mode==10u?0xe51c:0xe039;
        if(mode>=13u)sys.cpu.pc=mode==13u?0xe31d:0xe528;
        if(mode>=15u)sys.cpu.pc=mode==15u?0xe3b7:0xe52c;
        if(mode>=17u)sys.cpu.pc=mode==17u?0xe100:0xe520;
        if(mode>=19u)sys.cpu.pc=mode==19u?0xde29:mode==20u?0xe517:0xe530;
        if(mode>=22u){static const unsigned entries[]={0xd4e1,0xd557,0xd835,0xd88a,0xdd4f,0xdd8c,0xd498,0xd4c0,0xd506,0xd534,0xdd19,0xdd32,0xdd52,0xdd6f,0xd81d,0xd825,0xd86c,0xd878,0xd31a,0xdb5c,0x63dd,0x5fc4,0x604f};sys.cpu.pc=entries[mode-22u];}
        if(mode>=28u && mode<36u){sys.cpu.ac=(uint8_t)next_random();sys.ram[0x20]=(uint8_t)next_random();sys.ram[0x21]=(uint8_t)next_random();sys.ram[0x23]=(uint8_t)next_random();sys.ram[0x24]=(uint8_t)next_random();}
        if(mode==41u){
            unsigned count=1u+t%32u,table=0x800u+(t&255u),key=t%2u?0xffffu:t%count;
            sys.cpu.ix=count;sys.cpu.iy=0;sys.ram[0x20]=sys.ram[0x21]=0x44;
            sys.ram[0x23]=(uint8_t)key;sys.ram[0x24]=(uint8_t)(key>>8);sys.ram[0x26]=(uint8_t)table;sys.ram[0x27]=(uint8_t)(table>>8);
            for(i=0;i<count;++i){sys.ram[table+2u*i]=(uint8_t)i;sys.ram[table+2u*i+1u]=0;sys.ram[table+2u*count+2u*i]=0x44;sys.ram[table+2u*count+2u*i+1u]=0x44;}
        }
        if(mode>=42u){
            unsigned args=0x900u+(t&255u),source=0x1200u+((t*7u)&255u),length=t%128u;
            for(i=5u;i<=8u;++i){sys.bk_tab[i]=(uint16_t)(0xeb0u+i-5u);mem_bs(i);}
            sys.ram[0x28]=(uint8_t)args;sys.ram[0x29]=(uint8_t)(args>>8);sys.ram[args]=(uint8_t)source;sys.ram[args+1u]=(uint8_t)(source>>8);
            for(i=0;i<length;++i)sys.ram[source+i]=(uint8_t)(1u+(i%255u));sys.ram[source+length]=0;
            if(mode==43u){sys.cpu.pc=0x5fc4;sys.ram[args+2u]=(uint8_t)(t%131u);}
            if(mode==44u){unsigned other=0x1800u+(t&255u);sys.ram[args+2u]=(uint8_t)other;sys.ram[args+3u]=(uint8_t)(other>>8);memcpy(sys.ram+other,sys.ram+source,length+1u);if(t%3u)sys.ram[other+t%(length+1u)]=(uint8_t)next_random();}
        }
        sys.ram[0x100u|(uint8_t)(sp+1u)]=0x43;sys.ram[0x100u|(uint8_t)(sp+2u)]=0x44;
        saved=sys.cpu;memcpy(initial,sys.ram,GAM4980_RAM_SIZE);
        do{cycles+=s6502_exec(&sys.cpu,1);}while(sys.cpu.pc!=0x4444 && cycles<30000u);
        ref=sys.cpu;memcpy(expected,sys.ram,GAM4980_RAM_SIZE);
        memcpy(sys.ram,initial,GAM4980_RAM_SIZE);sys.cpu=saved;
        c.pc=saved.pc;c.ac=saved.ac;c.ix=saved.ix;c.iy=saved.iy;c.sp=saved.sp;c.status=saved.status;
        c.ram=(uintptr_t)sys.ram;c.read8=(uintptr_t)mem_read;c.write8=(uintptr_t)mem_write;
        c.cycle_budget=cycles-1u;unchanged=c;
        if(native(&c) || memcmp(&c,&unchanged,sizeof(c)) || memcmp(initial,sys.ram,GAM4980_RAM_SIZE)){
            fprintf(stderr,"long multiply budget mode=%u t=%u cycles=%u native=%u left=%x right=%x\n",mode,t,cycles,c.cycles,left,right);
            for(i=0x100u+sp-16u;i<=0x100u+sp;++i)fprintf(stderr,"%x:%02x/%02x ",i,sys.ram[i],expected[i]);fputc('\n',stderr);goto done;
        }
        c.cycle_budget=cycles;i=native(&c);
        if(i!=cycles || c.pc!=ref.pc || c.ac!=ref.ac || c.ix!=ref.ix || c.iy!=ref.iy ||
           c.sp!=ref.sp || c.status!=ref.status || memcmp(expected,sys.ram,GAM4980_RAM_SIZE)){
            fprintf(stderr,"long multiply t=%u cycles=%u/%u A=%x/%x P=%x/%x\n",t,i,cycles,c.ac,ref.ac,c.status,ref.status);
            for(i=0;i<GAM4980_RAM_SIZE;++i)if(expected[i]!=sys.ram[i]){fprintf(stderr,"RAM %x %x/%x\n",i,sys.ram[i],expected[i]);break;}
            goto done;
        }
    }
    puts("native contracts: 900000 exact-state and budget cases passed");result=0;
done:
    gam4980_deinit();free(initial);free(expected);free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);return result;
}

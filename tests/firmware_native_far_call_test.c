#define main old_bank_test_main
#include "firmware_hle_bank_switch_test.c"
#undef main

int main(int argc,char **argv)
{
    gam4980_buffers_t b={0};unsigned t,i;int result=1;
    uint8_t *initial=malloc(GAM4980_RAM_SIZE),*expected=malloc(GAM4980_RAM_SIZE);
    b.ram=calloc(1,GAM4980_RAM_SIZE);b.flash=calloc(1,GAM4980_FLASH_SIZE);b.flash_size=GAM4980_FLASH_SIZE;
    b.rom_8=malloc(GAM4980_ROM_SIZE);b.rom_e=malloc(GAM4980_ROM_SIZE);
    if(argc!=3 || !initial || !expected || !b.ram || !b.flash || !b.rom_8 || !b.rom_e ||
       !load_exact(argv[1],b.rom_8,GAM4980_ROM_SIZE) || !load_exact(argv[2],b.rom_e,GAM4980_ROM_SIZE) || gam4980_init(&b)<=0)return 2;
    gam4980_set_firmware_hle_enabled(0);memset(s6502_aot_validation,2,sizeof(s6502_aot_validation));
#ifdef FW_NATIVE_BANK_TEST_SERVICES
    native_graphics_services.bank_metrics=1;
    if(native_bank_descriptor_safe(0x100) || native_bank_descriptor_safe(0x4fff) ||
       native_bank_descriptor_safe(0x5000) || native_bank_descriptor_safe(0x8fff) ||
       native_bank_descriptor_safe(0xfffe) || native_bank_descriptor_safe(0x2001) ||
       !native_bank_descriptor_safe(0xe749))goto done;
#endif
    printf("firmware bank base=%02x%02x\n",mem_read(0x03d5),mem_read(0x03d6));
    for(t=0;t<10000u;++t){
        unsigned cycles=0,pointer=0x600u+(t&255u);uint16_t banks[16],ref_banks[16];uint8_t selected,ref_selected;
        s6502_t saved,ref;host_fw_context c={0};
#ifdef FW_NATIVE_BANK_TEST_SERVICES
        static const unsigned bases[]={0x600,0x1100,0x3000,0xe700};
        pointer=bases[t%4u]+(t&127u);
        c.graphics=(uintptr_t)&test_bank_services;
#endif
        for(i=0;i<0x300u;++i)sys.ram[i]=(uint8_t)next_random();sys.ram[_SYSCON]=0;
        sys.ram[0x400]=0x60;
#ifdef FW_NATIVE_BANK_TEST_SERVICES
        sys.mem_r[pointer>>8][pointer&255u]=0;
        sys.mem_r[(pointer+1u)>>8][(pointer+1u)&255u]=4;
        sys.mem_r[(pointer+2u)>>8][(pointer+2u)&255u]=(uint8_t)t;
#else
        sys.ram[pointer]=0;sys.ram[pointer+1u]=4;sys.ram[pointer+2u]=(uint8_t)t;
#endif
        sys.ram[0x26]=(uint8_t)pointer;sys.ram[0x27]=(uint8_t)(pointer>>8);
        sys.cpu.pc=0xd2f6;sys.cpu.ac=(uint8_t)next_random();sys.cpu.ix=(uint8_t)next_random();sys.cpu.iy=(uint8_t)next_random();sys.cpu.sp=(uint8_t)t;sys.cpu.status=(uint8_t)(next_random()&~8u);
        sys.ram[0x100u|(uint8_t)(sys.cpu.sp+1u)]=0x43;sys.ram[0x100u|(uint8_t)(sys.cpu.sp+2u)]=0x44;
        saved=sys.cpu;memcpy(initial,sys.ram,GAM4980_RAM_SIZE);memcpy(banks,sys.bk_tab,sizeof(banks));selected=sys.bk_sel;
        do{cycles+=s6502_exec(&sys.cpu,1);}while(sys.cpu.pc!=0x4444 && cycles<10000u);
        ref=sys.cpu;memcpy(expected,sys.ram,GAM4980_RAM_SIZE);memcpy(ref_banks,sys.bk_tab,sizeof(banks));ref_selected=sys.bk_sel;
        restore_banks(banks,selected);memcpy(sys.ram,initial,GAM4980_RAM_SIZE);sys.cpu=saved;
        c.pc=saved.pc;c.ac=saved.ac;c.ix=saved.ix;c.iy=saved.iy;c.sp=saved.sp;c.status=saved.status;
        c.ram=(uintptr_t)sys.ram;c.read8=(uintptr_t)mem_read;c.write8=(uintptr_t)mem_write;c.cycle_budget=1;
        if(firmware_native_bank(&c) || memcmp(initial,sys.ram,GAM4980_RAM_SIZE))goto done;
        c.cycle_budget=50000;
        if(!firmware_native_bank(&c) || c.pc!=0x400){fprintf(stderr,"far-call entry t=%u pc=%x\n",t,c.pc);goto done;}
        sys.cpu.pc=c.pc;sys.cpu.ac=c.ac;sys.cpu.ix=c.ix;sys.cpu.iy=c.iy;sys.cpu.sp=c.sp;sys.cpu.status=c.status;
        c.cycles+=s6502_exec(&sys.cpu,6);c.pc=sys.cpu.pc;c.ac=sys.cpu.ac;c.ix=sys.cpu.ix;c.iy=sys.cpu.iy;c.sp=sys.cpu.sp;c.status=sys.cpu.status;
        if(!firmware_native_bank(&c) || c.cycles!=cycles || c.pc!=ref.pc || c.ac!=ref.ac || c.ix!=ref.ix || c.iy!=ref.iy || c.sp!=ref.sp || c.status!=ref.status ||
           memcmp(expected,sys.ram,GAM4980_RAM_SIZE) || memcmp(ref_banks,sys.bk_tab,sizeof(banks)) || ref_selected!=sys.bk_sel){
            fprintf(stderr,"far-call t=%u cost=%u/%u pc=%x/%x A=%x/%x P=%x/%x\n",t,c.cycles,cycles,c.pc,ref.pc,c.ac,ref.ac,c.status,ref.status);
            for(i=0;i<GAM4980_RAM_SIZE;++i)if(expected[i]!=sys.ram[i]){fprintf(stderr,"RAM %x %x/%x\n",i,sys.ram[i],expected[i]);break;}
            goto done;
        }
    }
#ifdef FW_NATIVE_BANK_TEST_SERVICES
    /* Full 16-bit carry/mask range, comparing the actual page mapper too. */
    for(t=0;t<65536u;t+=257u){
        uint16_t before[16],after[16];uint8_t sel=sys.bk_sel,high;
        uint8_t *pages[64];uint8_t (*readers[64])(uint16_t);void (*writers[64])(uint16_t,uint8_t);
        memcpy(before,sys.bk_tab,sizeof(before));
        page0_write(0x0c,5);page0_write(0x0d,(uint8_t)t);page0_write(0x0e,(uint8_t)(t>>8));high=(uint8_t)(t>>8);
        for(i=0;i<3;++i){unsigned next=page0_read(0x0d)+1u;page0_write(0x0c,(uint8_t)(6u+i));
            page0_write(0x0d,(uint8_t)next);high=(uint8_t)(high+(next>>8));page0_write(0x0e,high);}
        memcpy(after,sys.bk_tab,sizeof(after));
        memcpy(pages,sys.mem_r+80,sizeof(pages));memcpy(readers,sys.mem_ir+80,sizeof(readers));memcpy(writers,sys.mem_iw+80,sizeof(writers));
        restore_banks(before,sel);
        if(!native_bank_map4(t) || sys.bk_sel!=8 || sys.ram[0x2000]!=high ||
           memcmp(after,sys.bk_tab,sizeof(after)) || memcmp(pages,sys.mem_r+80,sizeof(pages)) ||
           memcmp(readers,sys.mem_ir+80,sizeof(readers)) || memcmp(writers,sys.mem_iw+80,sizeof(writers)))goto done;
    }
    printf("batch accepted=%u extended accepted=%u budget rejected=%u\n",native_bank_metrics[1],native_bank_metrics[10],native_bank_metrics[6]);
    if(!native_bank_metrics[1] || !native_bank_metrics[10] || !native_bank_metrics[6])goto done;
#endif
    puts("far-call: 10000 native call/return CPU/RAM/bank cases passed");result=0;
done:
    gam4980_deinit();free(initial);free(expected);free(b.ram);free(b.flash);free(b.rom_8);free(b.rom_e);return result;
}

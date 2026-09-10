/* Packed-byte compositor. Mapping is proved once per row; only final ROM
 * continuation state is materialized. Unusual aliases use the old path. */
FG_INLINE uint8_t *fg_span(fg_env_t *f, uint32_t a, uint32_t n)
{
    uint32_t end, p; uint8_t *base;
    if (!n || a < 0x300u || a+n>0x10000u) return 0;
    base=f->pages[a>>8]; if(!base)return 0;
    end=(a+n-1u)>>8;
    for(p=(a>>8)+1u;p<=end;++p)
        if((FG_POINTER)f->pages[p]!=(FG_POINTER)base+((p-(a>>8))<<8))return 0;
    return base+(a&255u);
}

FG_INLINE int fg_fast_bitmap(fg_env_t *f, uint32_t sp, uint32_t status,
                             uint32_t budget, fg_region_result_t *r)
{
    uint8_t *m=f->ram;
    uint32_t total=0,rows=0;
    for(;;) {
        uint32_t w=m[0x20e8], shift=m[0x20cf], cost;
        uint32_t src=fg_alg_zp16(f,m,0x2f), dst=fg_alg_zp16(f,m,0x31);
        uint32_t more=(m[0x2083]&7u)>=(m[0x2081]&7u);
        uint32_t count=w?(w+more):1u, out=w+1u, i, pair=0, changed=0;
        uint32_t physical, finaldst, carry, oldhigh;
        uint8_t *s,*d; uint8_t value;
        if(w>20u || shift>7u || !m[0x20da] ||
           !(cost=fg_alg_bitmap_row_cycles(f,m)) || cost>budget-total)break;
        s=fg_span(f,src,count); d=fg_span(f,dst,out);
        if(!s || !d || (FG_POINTER)d<(FG_POINTER)m ||
           (FG_POINTER)d-(FG_POINTER)m+out>0x8000u)break;
        physical=(uint32_t)((FG_POINTER)d-(FG_POINTER)m);
        /* No writes to controls, stack, or the special $0400 alias. */
        if(physical<=0x400u || (physical<=0x1000u && physical+out>0x1001u) ||
           (physical<0x2100u && physical+out>0x2000u) ||
           ((FG_POINTER)s<(FG_POINTER)d+out && (FG_POINTER)d<(FG_POINTER)s+count))break;
        if((FG_POINTER)s>=(FG_POINTER)m && (FG_POINTER)s<(FG_POINTER)m+0x8000u) {
            uint32_t source_physical=(uint32_t)((FG_POINTER)s-(FG_POINTER)m);
            if(source_physical<=0x400u || (source_physical<0x2100u && source_physical+count>0x2000u))break;
        }
        if(!w) {
            m[0x20e3]|=m[0x20e4];
            /* Compose a shifted packed-byte pair, not individual pixels. */
            pair=fg_alg_shift_pair(f,0,s[0],(uint8_t)shift);
            value=(uint8_t)((d[0]&m[0x20e3])|(uint8_t)pair);
            changed+=(d[0]!=value);d[0]=value;
        } else {
            pair=fg_alg_shift_pair(f,0,s[0],(uint8_t)shift);
            value=(uint8_t)((d[0]&m[0x20e3])|(uint8_t)pair);
            changed+=(d[0]!=value);d[0]=value;
            for(i=1u;i<w;++i) {
                pair=fg_alg_shift_pair(f,s[i-1u],s[i],(uint8_t)shift);
                value=(uint8_t)pair;changed+=(d[i]!=value);d[i]=value;
            }
            pair=fg_alg_shift_pair(f,s[w-1u],more?s[w]:0,(uint8_t)shift);
            value=(uint8_t)((d[w]&m[0x20e4])|((uint8_t)pair & (uint8_t)~m[0x20e4]));
            changed+=(d[w]!=value);d[w]=value;
            m[0x20d0]=(uint8_t)(m[0x2081]&7u);
        }
        m[0x20d8]=0;
        m[0x20e5]=(uint8_t)(pair>>8);
        m[0x20e6]=(uint8_t)pair;
        if(w)m[0x20e6]&=(uint8_t)~m[0x20e4];
        src+=count;m[0x2f]=(uint8_t)src;m[0x30]=(uint8_t)(src>>8);
        finaldst=(dst+w)&65535u; oldhigh=finaldst>>8;
        carry=((finaldst&255u)+m[0x20de])>>8;
        finaldst=(finaldst+m[0x20de])&65535u;
        m[0x31]=(uint8_t)finaldst;m[0x32]=(uint8_t)(finaldst>>8);
        status=(status&~0x40u)|((oldhigh==0x7fu && carry)?0x40u:0u);
        --m[0x20da];total+=cost;++rows;
        if(physical<=0x1000u && physical+out<=0x1001u) {
            if(f->context->lcd_write_calls)*(uint32_t*)(FG_POINTER)f->context->lcd_write_calls+=out;
            if(changed && f->context->dirty)*(int*)(FG_POINTER)f->context->dirty=1;
            if(f->context->lcd_changed_writes)*(uint32_t*)(FG_POINTER)f->context->lcd_changed_writes+=changed;
        }
        if(f->services->framebuffer)for(i=0;i<out;++i)fw_graphics_stage(f->context,f->services,physical+i);
        if(!m[0x20da] || (m[0x200]&8u))break;
    }
    if(!rows)return 0;
    if(f->services->metrics)((uint32_t*)(FG_POINTER)f->services->metrics)[3]+=rows;
    r->cycles=total;r->rows=rows;r->ix=r->iy=0;r->ac=m[0x20da];
    r->status=(uint8_t)((status&~0x83u)|1u|(m[0x20da]?m[0x20da]&0x80u:2u));
    r->sp=(uint8_t)sp;r->pc=0x5c5d;
    if(!m[0x20da]) {
        r->pc=(uint16_t)(m[0x100u|(uint8_t)(sp+1u)]|((uint16_t)m[0x100u|(uint8_t)(sp+2u)]<<8));
        ++r->pc;r->sp=(uint8_t)(sp+2u);
    }
    return 1;
}

FG_INLINE int fg_fast_shift(fg_env_t *f, uint32_t sp, uint32_t status,
                            uint32_t budget, fg_region_result_t *r)
{
    uint8_t *m=f->ram;uint32_t total=0,rows=0;
    for(;;) {
        uint32_t w=m[0x20e8],shift=m[0x20cf],cost=fg_alg_shift_row_cycles(f,m);
        uint32_t src=fg_alg_zp16(f,m,0x2f),dst=fg_alg_zp16(f,m,0x3a);
        uint32_t first=fg_alg_zp16(f,m,0x38),more=(m[0x2083]&7u)>=(m[0x2081]&7u);
        uint32_t count=w+more,i,physical,firstphysical,changed=0,firstchanged;
        uint8_t *s,*d,*a;uint16_t pair;uint8_t value;
        if(!cost || cost>budget-total || !w || w>20 || shift>7 || !m[0x20da])break;
        s=fg_span(f,src,count);d=fg_span(f,dst,w);a=fg_span(f,first,1);
        if(!s || !d || !a || dst==0x400u ||
           (FG_POINTER)d<(FG_POINTER)m || (FG_POINTER)d-(FG_POINTER)m+w>0x1001u ||
           (FG_POINTER)a<(FG_POINTER)m || (FG_POINTER)a-(FG_POINTER)m>=0x1001u)break;
        physical=(uint32_t)((FG_POINTER)d-(FG_POINTER)m);
        firstphysical=(uint32_t)((FG_POINTER)a-(FG_POINTER)m);
        if(physical<=0x400u || firstphysical<=0x400u ||
           ((FG_POINTER)s<(FG_POINTER)d+w && (FG_POINTER)d<(FG_POINTER)s+count) ||
           ((FG_POINTER)s<=(FG_POINTER)a && (FG_POINTER)a<(FG_POINTER)s+count) ||
           ((FG_POINTER)a>=(FG_POINTER)d && (FG_POINTER)a<(FG_POINTER)d+w))break;
        if((FG_POINTER)s>=(FG_POINTER)m && (FG_POINTER)s<(FG_POINTER)m+0x2100u)break;
        pair=fg_alg_shift_pair(f,0,s[0],(uint8_t)shift);
        value=(uint8_t)((a[0]&m[0x20e3])|(uint8_t)pair);
        firstchanged=a[0]!=value;a[0]=value;
        for(i=1;i<w;++i) {
            pair=fg_alg_shift_pair(f,s[i-1],s[i],(uint8_t)shift);
            value=(uint8_t)pair;changed+=d[i-1]!=value;d[i-1]=value;
        }
        m[0x20e7]=3;m[0x20d8]=0;m[0x20d0]=m[0x2081]&7u;
        m[0x20e5]=(uint8_t)(pair>>8);m[0x20e6]=(uint8_t)pair;
        src+=w-1;dst+=w-1;
        m[0x2f]=(uint8_t)src;m[0x30]=(uint8_t)(src>>8);
        m[0x3a]=(uint8_t)dst;m[0x3b]=(uint8_t)(dst>>8);
        if(f->context->lcd_write_calls)*(uint32_t*)(FG_POINTER)f->context->lcd_write_calls+=w;
        if((changed||firstchanged) && f->context->dirty)*(int*)(FG_POINTER)f->context->dirty=1;
        if(f->context->lcd_changed_writes)*(uint32_t*)(FG_POINTER)f->context->lcd_changed_writes+=changed+firstchanged;
        if(f->services->framebuffer) {
            fw_graphics_stage(f->context,f->services,firstphysical);
            for(i=0;i+1<w;++i)fw_graphics_stage(f->context,f->services,physical+i);
        }
        /* The shared terminal-byte continuation materializes state once,
         * after the entire packed row, not after every source byte. */
        fg_alg_picture_tail(f,(int)more,sp,status,0x7fffffffu,r);
        total+=cost;++rows;status=r->status;sp=r->sp;
        if(r->pc!=0x6988u || !m[0x20da] || (m[0x200]&8u))break;
    }
    if(!rows)return 0;
    if(f->services->metrics)((uint32_t*)(FG_POINTER)f->services->metrics)[3]+=rows;
    r->cycles=total;r->rows=rows;return 1;
}

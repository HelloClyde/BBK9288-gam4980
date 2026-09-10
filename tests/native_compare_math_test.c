#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "../src/firmware_native_compare_math.h"
int main(void) {
    uint32_t seed=9288,t;
    for(t=0;t<1000000u;++t) {
        uint32_t l,r,p,a=0,b=0,sum=0,carry=1,count=0,i,n=t&1?2:4;
        seed=seed*1664525u+1013904223u;l=seed;
        seed=seed*1664525u+1013904223u;r=seed;
        if(n==2){l&=65535;r&=65535;}
        p=(t>>2)&255;
        for(i=0;i<n;++i){a=(l>>(8*i))&255;b=(~(r>>(8*i)))&255;
            sum=a+b+carry;carry=sum>255;count+=(uint8_t)sum!=0;}
        assert(count==fw_compare_count(n==2?(uint16_t)(l-r):l-r,n));
        assert(fw_compare_status(l,r,n,p)==(uint8_t)((p&~0xc3u)|0x30u|carry|
            (count?0:2)|(((a^sum)&(b^sum)&128)?64:0)|(sum&128)));
    }
    puts("PASS shared compare math 1000000 byte-reference cases");return 0;
}

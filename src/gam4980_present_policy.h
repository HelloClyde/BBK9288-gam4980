#ifndef GAM4980_PRESENT_POLICY_H
#define GAM4980_PRESENT_POLICY_H
/* Presentation only. Never changes guest execution/interrupt deadlines.
 * 256-Hz host clock: require 2 unchanged execution batches + 6 ticks quiet;
 * unknown/no-yield drawing has a 64-tick bounded fallback. */
typedef struct { u32 pending, first, last_change, quiet; } gam_present_policy;
enum { PRESENT_NONE, PRESENT_PICTURE, PRESENT_WAIT, PRESENT_QUIET, PRESENT_TIMEOUT };
static u32 gam_present_decide(gam_present_policy *p, u32 now, int changed,
                              int picture, int busy, int waiting)
{
    if(changed) {
        if(!p->pending)p->first=now;
        p->pending=1u;p->last_change=now;p->quiet=0u;
    } else if(p->pending && p->quiet<2u) ++p->quiet;
    if(picture)return PRESENT_PICTURE;
    if(!p->pending)return PRESENT_NONE;
    if(!busy && waiting)return PRESENT_WAIT;
    if(!busy && p->quiet>=2u && (u32)(now-p->last_change)>=6u)return PRESENT_QUIET;
    if((u32)(now-p->first)>=64u)return PRESENT_TIMEOUT;
    return PRESENT_NONE;
}
#endif

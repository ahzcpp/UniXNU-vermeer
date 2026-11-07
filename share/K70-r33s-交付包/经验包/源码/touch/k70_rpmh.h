/* Minimal polled RPMh regulator vote from the APPS RSC (DRV2), for the K70 touch rails.
 * K70 dtbo (vermeer overlay, fragment@33/34): goodix@0 "xiaomi,vermeer-spi" avdd-supply = L2B (pm8550 ldob2,
 * 3.2 V per fragment@33), vdd-supply = L12B (ldob12, 1.8 V). Both are RPMh VRM resources, so the rails are switched
 * by votes sent through the application processor's RSC, not by PMIC register writes.
 * Formats follow Linux v6.6: drivers/soc/qcom/cmd-db.c (database layout), rpmh-rsc.c (register offsets for RSC
 * v2.7/v3.0, __tcs_buffer_write, __tcs_set_trigger, tcs_tx_done) and drivers/regulator/qcom-rpmh-regulator.c
 * (VRM voltage/enable/mode registers, PMIC5 LDO HPM = 7). Stock DT: apps_rsc drv-2 at 0x17a20000,
 * qcom,tcs-offset 0xd00, tcs-config ACTIVE x3 first (TCS 0..2); aop_cmd_db_region at 0x81c60000 (0x20000).
 */
#ifndef K70_RPMH_H
#define K70_RPMH_H
#include <stdint.h>

#define KR_RSC_DRV2 0x17a20000u
#define KR_RSC_SIZE 0x10000u
#define KR_TCS_OFFSET 0xd00u
#define KR_ACTIVE_TCS 3u
#define KR_CMDDB 0x81c60000u
#define KR_CMDDB_SIZE 0x20000u
#define KR_VRM_VOLTAGE 0x0u
#define KR_VRM_ENABLE 0x4u
#define KR_VRM_MODE 0x8u
#define KR_LDO_MODE_HPM 7u
#define KR_AMC_ENABLE (1u<<16)
#define KR_AMC_TRIGGER (1u<<24)
#define KR_MSGID (8u|(1u<<16)|(1u<<8))   /* CMD_MSGID_LEN | WRITE | RESP_REQ */
#define KR_CMD_COMPL (1u<<16)

struct kr_regs {unsigned tcs_stride,cmd_stride,irq_enable,irq_status,irq_clear,control,status,cmd_enable,msgid,addr,data,cmd_status;};
static const struct kr_regs kr_v27={672,20,0x00,0x04,0x08,0x14,0x18,0x1c,0x30,0x34,0x38,0x3c};
static const struct kr_regs kr_v30={672,24,0x00,0x04,0x08,0x24,0x28,0x2c,0x34,0x38,0x3c,0x40};

static struct {
    vm_offset_t rsc,db;
    const struct kr_regs *r;
    uint32_t version,addr[2],tcs,done_how,read_en,read_mv;
    const char *error;
} kr;

static uint32_t kr_rd(vm_offset_t b,unsigned o){uint32_t v=*(volatile uint32_t *)(b+o);__asm__ volatile("dmb oshld":::"memory");return v;}
static void kr_wr(vm_offset_t b,unsigned o,uint32_t v){*(volatile uint32_t *)(b+o)=v;__asm__ volatile("dsb sy":::"memory");}
static uint8_t kr_db8(unsigned o){return *(volatile uint8_t *)(kr.db+o);}
static unsigned kr_db16(unsigned o){return kr_db8(o)|((unsigned)kr_db8(o+1)<<8);}
static uint32_t kr_db32(unsigned o){return kr_db16(o)|((uint32_t)kr_db16(o+2)<<16);}

/* cmd_db_read_addr(): header {version, magic db 30 03 0c, rsc_hdr[8] (16 bytes each), checksum, reserved, data[]};
 * rsc_hdr {slv_id, header_offset, data_offset, cnt, ...}; entry_header (24 bytes) {id[8], priority[2], addr, len, offset}. */
static uint32_t kr_lookup(const char *id)
{
    static const uint8_t magic[4]={0xdb,0x30,0x03,0x0c};
    for(unsigned i=0;i<4;i++)if(kr_db8(4+i)!=magic[i]){kr.error="cmd-db magic";return 0;}
    for(unsigned s=0;s<8;s++) {
        unsigned h=8+16*s;
        if(!kr_db16(h))break;
        unsigned ent=144+kr_db16(h+2),cnt=kr_db16(h+6);
        for(unsigned j=0;j<cnt && ent+24*j+24<=KR_CMDDB_SIZE;j++) {
            unsigned e=ent+24*j,k=0;
            for(;k<8;k++){uint8_t c=kr_db8(e+k),q=id[k];if(c!=q)break;if(!q){k=8;break;}}
            if(k==8)return kr_db32(e+16);
        }
    }
    kr.error="cmd-db entry missing";return 0;
}
static unsigned kr_treg(unsigned tcs,unsigned reg){return KR_TCS_OFFSET+kr.r->tcs_stride*tcs+reg;}
static unsigned kr_creg(unsigned tcs,unsigned reg,unsigned cmd){return kr_treg(tcs,reg)+kr.r->cmd_stride*cmd;}
/* One active-only request with response (rpmh_write): n commands in one TCS, polled to completion. */
static int kr_send(const uint32_t *addr,const uint32_t *data,unsigned n)
{
    unsigned tcs=KR_ACTIVE_TCS;
    for(unsigned t=0;t<KR_ACTIVE_TCS;t++)
        if(!kr_rd(kr.rsc,kr_treg(t,kr.r->cmd_enable))){tcs=t;break;}
    if(tcs==KR_ACTIVE_TCS){kr.error="no idle active TCS";return 0;}
    kr.tcs=tcs;
    uint32_t mask=0;
    for(unsigned j=0;j<n;j++) {
        kr_wr(kr.rsc,kr_creg(tcs,kr.r->msgid,j),KR_MSGID);
        kr_wr(kr.rsc,kr_creg(tcs,kr.r->addr,j),addr[j]);
        kr_wr(kr.rsc,kr_creg(tcs,kr.r->data,j),data[j]);
        mask|=1u<<j;
    }
    kr_wr(kr.rsc,kr_treg(tcs,kr.r->cmd_enable),mask);
    kr_wr(kr.rsc,KR_TCS_OFFSET+kr.r->irq_enable,kr_rd(kr.rsc,KR_TCS_OFFSET+kr.r->irq_enable)|(1u<<tcs));
    uint32_t c=kr_rd(kr.rsc,kr_treg(tcs,kr.r->control));   /* __tcs_set_trigger(true) */
    c&=~KR_AMC_TRIGGER;kr_wr(kr.rsc,kr_treg(tcs,kr.r->control),c);
    c&=~KR_AMC_ENABLE;kr_wr(kr.rsc,kr_treg(tcs,kr.r->control),c);
    kr_wr(kr.rsc,kr_treg(tcs,kr.r->control),KR_AMC_ENABLE);
    kr_wr(kr.rsc,kr_treg(tcs,kr.r->control),KR_AMC_ENABLE|KR_AMC_TRIGGER);
    int done=0;
    for(unsigned i=0;i<2000 && !done;i++) {   /* up to ~200 ms */
        if(kr_rd(kr.rsc,KR_TCS_OFFSET+kr.r->irq_status)&(1u<<tcs)){done=1;kr.done_how=1;break;}
        unsigned all=1;
        for(unsigned j=0;j<n;j++)if(!(kr_rd(kr.rsc,kr_creg(tcs,kr.r->cmd_status,j))&KR_CMD_COMPL))all=0;
        if(all){done=1;kr.done_how=2;break;}
        delay(100);
    }
    /* tcs_tx_done: reclaim the TCS */
    kr_wr(kr.rsc,kr_treg(tcs,kr.r->cmd_enable),0);
    kr_wr(kr.rsc,KR_TCS_OFFSET+kr.r->irq_clear,1u<<tcs);
    if(!done){kr.error="RPMh vote timed out";return 0;}
    return 1;
}
/* Read request (CMD_MSGID without WRITE, response required): the resource's current value comes back in
 * RSC_DRV_CMD_RESP_DATA (downstream rpmh_read). Diagnostic only; 0xffffffff when it does not complete. */
static uint32_t kr_read(uint32_t addr)
{
    unsigned tcs=KR_ACTIVE_TCS;
    for(unsigned t=0;t<KR_ACTIVE_TCS;t++)
        if(!kr_rd(kr.rsc,kr_treg(t,kr.r->cmd_enable))){tcs=t;break;}
    if(tcs==KR_ACTIVE_TCS)return 0xffffffffu;
    unsigned resp=kr.r->cmd_status+4;   /* RSC_DRV_CMD_RESP_DATA follows CMD_STATUS */
    kr_wr(kr.rsc,kr_creg(tcs,kr.r->msgid,0),8u|(1u<<8));
    kr_wr(kr.rsc,kr_creg(tcs,kr.r->addr,0),addr);
    kr_wr(kr.rsc,kr_creg(tcs,kr.r->data,0),0);
    kr_wr(kr.rsc,kr_treg(tcs,kr.r->cmd_enable),1);
    uint32_t c=kr_rd(kr.rsc,kr_treg(tcs,kr.r->control));
    c&=~KR_AMC_TRIGGER;kr_wr(kr.rsc,kr_treg(tcs,kr.r->control),c);
    c&=~KR_AMC_ENABLE;kr_wr(kr.rsc,kr_treg(tcs,kr.r->control),c);
    kr_wr(kr.rsc,kr_treg(tcs,kr.r->control),KR_AMC_ENABLE);
    kr_wr(kr.rsc,kr_treg(tcs,kr.r->control),KR_AMC_ENABLE|KR_AMC_TRIGGER);
    uint32_t v=0xffffffffu;
    for(unsigned i=0;i<500;i++) {
        if((kr_rd(kr.rsc,KR_TCS_OFFSET+kr.r->irq_status)&(1u<<tcs)) ||
           (kr_rd(kr.rsc,kr_creg(tcs,kr.r->cmd_status,0))&KR_CMD_COMPL)){v=kr_rd(kr.rsc,kr_creg(tcs,resp,0));break;}
        delay(100);
    }
    kr_wr(kr.rsc,kr_treg(tcs,kr.r->cmd_enable),0);
    kr_wr(kr.rsc,KR_TCS_OFFSET+kr.r->irq_clear,1u<<tcs);
    return v;
}
/* Turn on one VRM LDO: voltage (mV), HPM, enable — the order rpmh_regulator applies them. */
static int kr_ldo_on(unsigned slot,const char *name,unsigned mv)
{
    uint32_t base=kr_lookup(name);
    kr.addr[slot]=base;
    if(!base)return 0;
    const uint32_t a[3]={base+KR_VRM_VOLTAGE,base+KR_VRM_MODE,base+KR_VRM_ENABLE};
    const uint32_t d[3]={mv,KR_LDO_MODE_HPM,1};
    return kr_send(a,d,3);
}
static int kr_init(void)
{
    kr.rsc=ml_io_map(KR_RSC_DRV2,KR_RSC_SIZE);kr.db=ml_io_map(KR_CMDDB,KR_CMDDB_SIZE);
    if(!kr.rsc || !kr.db){kr.error="RSC/cmd-db map failed";return 0;}
    kr.version=kr_rd(kr.rsc,0);
    kr.r=((kr.version>>16)&0xff)==3?&kr_v30:&kr_v27;
    return 1;
}
#endif

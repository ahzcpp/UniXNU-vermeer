/* Minimal polled GPI DMA SPI transport for the K70 touch SE (QUP wrapper 1, SE4).
 * K70's SE4 firmware has the FIFO interface disabled (GENI_IF_DISABLE_RO bit 0), so, as in Linux's
 * spi-geni-qcom.c, SPI must go through the GPI DMA engine. Register layout, channel states, commands
 * and TRE formats follow Linux v6.6 drivers/dma/qcom/gpi.c and drivers/spi/spi-geni-qcom.c.
 * Stock DT: spi@a90000 dmas = <&gpi_dma1 0 4 1 64 2>, <&gpi_dma1 1 4 1 64 2>; gpi-dma@a00000
 * iommus = <&apps_smmu 0xb6 0>, qcom,gpii-mask = <0x1e>. Polling only (no GIC interrupt).
 * The DMA memory is a static, physically contiguous kernel-image buffer; caches are maintained
 * explicitly; the GPI stream gets the same SMMU pass-through the UFS/sheng touch drivers use.
 */
#ifndef K70_GPI_SPI_H
#define K70_GPI_SPI_H
#include <stdint.h>
#include "SmmuBypass.h"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_vtophys(vm_offset_t);
extern vm_offset_t ml_static_vtop(vm_offset_t);

#define KG_GPI_BASE   0x00a00000u
#define KG_GPI_SIZE   0x60000u
#define KG_SMMU_BASE  0x15000000u
#define KG_SMMU_SIZE  0xd0000u
#define KG_GPI_SID    0xb6u
#define KG_GPII       1u          /* first GPII of qcom,gpii-mask 0x1e */
#define KG_SEID       4u
#define KG_PROTO_SPI  1u
#define KG_EL         16u
#define KG_TX_CH      0u
#define KG_RX_CH      1u

#define KG_CH_CNTXT(k)  (0x20000u+0x4000u*KG_GPII+0x80u*(k))
#define KG_CH_QOS(k)    (0x2005Cu+0x4000u*KG_GPII+0x80u*(k))
#define KG_CH_SCR(k,i)  (0x20060u+0x4000u*KG_GPII+0x80u*(k)+4u*(i))
#define KG_EV_CNTXT     (0x21000u+0x4000u*KG_GPII)
#define KG_CH_DB(k)     (0x22000u+0x4000u*KG_GPII+0x8u*(k))
#define KG_EV_DB        (0x22100u+0x4000u*KG_GPII)
#define KG_CH_CMD       (0x23008u+0x4000u*KG_GPII)
#define KG_EV_CMD       (0x23010u+0x4000u*KG_GPII)
#define KG_G(off)       ((off)+0x4000u*KG_GPII)
/* CNTXT words */
#define KG_C0 0x0u
#define KG_C1_LEN 0x4u
#define KG_C2_BASE_LO 0x8u
#define KG_C3_BASE_HI 0xCu
#define KG_C4_RP_LO 0x10u
#define KG_C8_INTMOD 0x20u
#define KG_C10_MSI_LO 0x28u
#define KG_C11_MSI_HI 0x2Cu
#define KG_C12_RPU_LO 0x30u
#define KG_C13_RPU_HI 0x34u
/* TRE words (gpi.c) */
#define KG_TRE_DMA 0x10u
#define KG_TRE_GO 0x20u
#define KG_TRE_CONFIG0 0x22u
#define KG_F_CHAIN (1u<<0)
#define KG_F_IEOB (1u<<8)
#define KG_F_IEOT (1u<<9)
#define KG_F_LINK (1u<<11)
#define KG_SPI_DUPLEX 3u
/* SE registers used to hand the engine to GSI (qcom-geni-se.c geni_se_select_gpi_mode). */
#define KG_SE_M_IRQ_EN 0x614u
#define KG_SE_M_IRQ_CLR 0x618u
#define KG_SE_S_IRQ_CLR 0x648u
#define KG_SE_DMA_MODE_EN 0x258u
#define KG_SE_GSI_EVENT_EN 0xe18u
#define KG_SE_IRQ_EN 0xe1cu
#define KG_SE_SPI_TRANS_CFG 0x24cu

/* No buffer crosses a 4 KiB page: the kernel image is not physically contiguous page to page here
 * (r26s: g1/138), so every page is translated on its own. */
struct kg_dma {
    uint8_t ev[4096];          /* page 0: event ring, 256 x 16, aligned to its length */
    uint8_t txr[1024];         /* page 1: TX channel ring, 64 TREs */
    uint8_t rxr[1024];         /*         RX channel ring, 64 TREs */
    uint8_t txb[2048];         /*         TX data */
    uint8_t rxb[2048];         /* page 2: RX data */
    uint8_t pad[2048];
} __attribute__((aligned(16384)));
static struct kg_dma kg_mem;
#define KG_PAGES (sizeof(struct kg_dma)/4096u)

static struct {
    vm_offset_t gpi, smmu, se;
    uint64_t pa;               /* physical address of kg_mem's first page */
    uint64_t page_pa[KG_PAGES];
    unsigned tx_wp, rx_wp, ev_rp;   /* byte offsets inside their rings */
    unsigned step, fail_line;
    uint32_t last_ev[4], err, ch_state[2], ev_state, smmu_slot;
    const char *smmu_error;
    unsigned clk_src, clk_div;
} kg;

static uint32_t kg_rd(vm_offset_t b, unsigned o) {uint32_t v=*(volatile uint32_t *)(b+o);__asm__ volatile("dmb oshld":::"memory");return v;}
static void kg_wr(vm_offset_t b, unsigned o, uint32_t v) {*(volatile uint32_t *)(b+o)=v;__asm__ volatile("dsb sy":::"memory");}
static uint32_t kg_smmu_rd(void *c, unsigned o) {(void)c;return kg_rd(kg.smmu,o);}
static void kg_smmu_wr(void *c, unsigned o, uint32_t v) {(void)c;kg_wr(kg.smmu,o,v);}
static void kg_cache(void *p, size_t n, int invalidate)
{
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
    size_t line=4u<<((ctr>>16)&15u);
    uintptr_t a=(uintptr_t)p&~(line-1),end=(uintptr_t)p+n;
    for(;a<end;a+=line) {
        if(invalidate)__asm__ volatile("dc civac, %0"::"r"(a):"memory");
        else __asm__ volatile("dc cvac, %0"::"r"(a):"memory");
    }
    __asm__ volatile("dsb sy":::"memory");
}
/* The MMU's own answer (AT S1E1R, as mmu_kvtop), ml_vtophys if the probe faults. */
static uint64_t kg_vtop(vm_offset_t va)
{
    boolean_t enabled=ml_set_interrupts_enabled(FALSE);
    uint64_t par;
    __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1":"=r"(par):"r"(va):"memory");
    ml_set_interrupts_enabled(enabled);
    if(!(par&1u))return (par&0x0000fffffffff000ull)|(va&0xfffu);
    return ml_vtophys(va);
}
static uint64_t kg_pa(const void *p)
{
    uint64_t off=(uint64_t)((const uint8_t *)p-(const uint8_t *)&kg_mem);
    return kg.page_pa[off>>12]+(off&0xfffu);
}
#define KG_FAIL() do{kg.fail_line=__LINE__;return 0;}while(0)

static unsigned kg_ch_state(unsigned k) {return (kg_rd(kg.gpi,KG_CH_CNTXT(k)+KG_C0)>>20)&15u;}
static unsigned kg_ev_state(void) {return (kg_rd(kg.gpi,KG_EV_CNTXT+KG_C0)>>20)&15u;}
static int kg_wait(int ev, unsigned k, unsigned want)
{
    for(unsigned i=0;i<2500;i++) {   /* 250 ms, as CMD_TIMEOUT_MS */
        unsigned s=ev?kg_ev_state():kg_ch_state(k);
        if(s==want)return 1;
        delay(100);
    }
    kg.ch_state[0]=kg_ch_state(0);kg.ch_state[1]=kg_ch_state(1);kg.ev_state=kg_ev_state();
    return 0;
}
static int kg_ch_cmd(unsigned op, unsigned k, unsigned want)
{
    kg_wr(kg.gpi,KG_CH_CMD,(op<<24)|k);
    return kg_wait(0,k,want);
}

/* One-time bring-up. se: the mapped SE4 registers (already clocked, SPI firmware, pins set). */
static int kg_init(vm_offset_t se)
{
    kg.se=se;kg.step=1;
    /* Translate each 4 KiB page; the SMMU pass-through covers the span of all of them. */
    uint64_t lo=~0ull,hi=0;
    for(unsigned i=0;i<KG_PAGES;i++) {
        uint64_t pa=kg_vtop((vm_offset_t)&kg_mem+i*4096u);
        kg.page_pa[i]=pa;
        if(!pa || (pa&0xfffu))KG_FAIL();
        if(pa<lo)lo=pa;
        if(pa+4096u>hi)hi=pa+4096u;
    }
    kg.pa=kg.page_pa[0];
    kg.step=2;
    kg.gpi=ml_io_map(KG_GPI_BASE,KG_GPI_SIZE);kg.smmu=ml_io_map(KG_SMMU_BASE,KG_SMMU_SIZE);
    if(!kg.gpi || !kg.smmu)KG_FAIL();
    kg.step=3;   /* GPI stream -> pass-through context bank */
    struct smmu_io io={NULL,kg_smmu_rd,kg_smmu_wr};
    struct smmu_plan plan=smmu_plan_bypass(&io,KG_GPI_SID,lo,hi-lo);
    kg.smmu_slot=plan.slot;
    kg.smmu_error=smmu_configure_bypass(&io,&plan,KG_GPI_SID);
    if(kg.smmu_error)KG_FAIL();
    kg.step=4;   /* SE to GSI mode (geni_se_select_gpi_mode), CS controlled by the GO TRE */
    kg_wr(se,KG_SE_M_IRQ_CLR,0xffffffffu);kg_wr(se,KG_SE_S_IRQ_CLR,0xffffffffu);
    kg_wr(se,KG_SE_IRQ_EN,0);
    kg_wr(se,KG_SE_M_IRQ_EN,kg_rd(se,KG_SE_M_IRQ_EN)&~((1u<<0)|(1u<<30)|(1u<<26)|(1u<<27)));
    kg_wr(se,KG_SE_DMA_MODE_EN,1);
    kg_wr(se,KG_SE_GSI_EVENT_EN,kg_rd(se,KG_SE_GSI_EVENT_EN)|0xfu);
    kg_wr(se,KG_SE_SPI_TRANS_CFG,kg_rd(se,KG_SE_SPI_TRANS_CFG)&~2u);
    kg.step=5;   /* leftover GPII state from an earlier owner: reset it */
    for(unsigned k=0;k<2;k++) if(kg_ch_state(k)!=0){if(kg_ch_state(k)==2)kg_ch_cmd(2,k,3);kg_wr(kg.gpi,KG_CH_CMD,(9u<<24)|k);delay(2000);kg_wr(kg.gpi,KG_CH_CMD,(10u<<24)|k);delay(2000);}
    if(kg_ev_state()!=0){kg_wr(kg.gpi,KG_EV_CMD,(9u<<24));delay(2000);kg_wr(kg.gpi,KG_EV_CMD,(10u<<24));delay(2000);}
    /* every GPII interrupt masked (nobody services it here); states and rings are polled */
    kg_wr(kg.gpi,KG_G(0x23088),0);kg_wr(kg.gpi,KG_G(0x230B8),0);kg_wr(kg.gpi,KG_G(0x23098),0);
    kg_wr(kg.gpi,KG_G(0x2309C),0);kg_wr(kg.gpi,KG_G(0x23108),0);kg_wr(kg.gpi,KG_G(0x23120),0);
    kg_wr(kg.gpi,KG_G(0x23188),0);kg_wr(kg.gpi,KG_G(0x2318C),0);kg_wr(kg.gpi,KG_G(0x23400),0);
    kg_wr(kg.gpi,KG_G(0x23404),0);kg_wr(kg.gpi,KG_G(0x23180),1);kg_wr(kg.gpi,KG_G(0x23200),0);
    bzero(&kg_mem,sizeof(kg_mem));kg_cache(&kg_mem,sizeof(kg_mem),0);
    kg.step=6;   /* event ring (gpi_alloc_ev_chan) */
    kg_wr(kg.gpi,KG_EV_CMD,0);
    if(!kg_wait(1,0,1))KG_FAIL();
    uint64_t ev=kg_pa(kg_mem.ev);
    kg_wr(kg.gpi,KG_EV_CNTXT+KG_C0,(KG_EL<<24)|(1u<<16)|2u);
    kg_wr(kg.gpi,KG_EV_CNTXT+KG_C1_LEN,sizeof(kg_mem.ev));
    kg_wr(kg.gpi,KG_EV_CNTXT+KG_C2_BASE_LO,(uint32_t)ev);kg_wr(kg.gpi,KG_EV_CNTXT+KG_C3_BASE_HI,(uint32_t)(ev>>32));
    kg_wr(kg.gpi,KG_EV_DB+4,(uint32_t)(ev>>32));
    kg_wr(kg.gpi,KG_EV_CNTXT+KG_C8_INTMOD,0);kg_wr(kg.gpi,KG_EV_CNTXT+KG_C10_MSI_LO,0);kg_wr(kg.gpi,KG_EV_CNTXT+KG_C11_MSI_HI,0);
    kg_wr(kg.gpi,KG_EV_CNTXT+KG_C12_RPU_LO,0);kg_wr(kg.gpi,KG_EV_CNTXT+KG_C13_RPU_HI,0);
    kg.ev_rp=0;
    kg_wr(kg.gpi,KG_EV_DB,(uint32_t)(ev+sizeof(kg_mem.ev)-KG_EL));
    kg.step=7;   /* both channels (gpi_alloc_chan), then start */
    for(unsigned k=0;k<2;k++) {
        if(!kg_ch_cmd(0,k,1))KG_FAIL();
        uint64_t ring=kg_pa(k?kg_mem.rxr:kg_mem.txr);
        kg_wr(kg.gpi,KG_CH_CNTXT(k)+KG_C0,(KG_EL<<24)|((k==KG_TX_CH?1u:0u)<<3)|2u);
        kg_wr(kg.gpi,KG_CH_CNTXT(k)+KG_C1_LEN,1024);
        kg_wr(kg.gpi,KG_CH_CNTXT(k)+KG_C2_BASE_LO,(uint32_t)ring);kg_wr(kg.gpi,KG_CH_CNTXT(k)+KG_C3_BASE_HI,(uint32_t)(ring>>32));
        kg_wr(kg.gpi,KG_CH_DB(k)+4,(uint32_t)(ring>>32));
        kg_wr(kg.gpi,KG_CH_SCR(k,0),((k?0u:1u)<<16)|(KG_PROTO_SPI<<4)|KG_SEID);
        kg_wr(kg.gpi,KG_CH_SCR(k,1),0);kg_wr(kg.gpi,KG_CH_SCR(k,2),0);kg_wr(kg.gpi,KG_CH_SCR(k,3),0);
        kg_wr(kg.gpi,KG_CH_QOS(k),1);
    }
    kg.tx_wp=kg.rx_wp=0;
    kg.step=8;
    for(unsigned k=0;k<2;k++) if(!kg_ch_cmd(1,k,2))KG_FAIL();
    kg.step=9;
    return 1;
}
static void kg_tre(uint8_t *ring, unsigned *wp, uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3)
{
    uint32_t *t=(uint32_t *)(ring+*wp);
    t[0]=d0;t[1]=d1;t[2]=d2;t[3]=d3;
    *wp=(*wp+KG_EL)&1023u;
}
/* One full-duplex SPI transfer of n bytes (setup_gsi_xfer + gpi_create_spi_tre), polled to completion. */
static int kg_transfer(const uint8_t *tx, uint8_t *rx, unsigned n)
{
    if(kg.step!=9)return 0;   /* not ready, or an earlier transfer left the channels unknown */
    if(!n || n>sizeof(kg_mem.txb))return 0;
    bcopy(tx,kg_mem.txb,n);bzero(kg_mem.rxb,n);
    kg_cache(kg_mem.txb,n,0);kg_cache(kg_mem.rxb,n,1);
    unsigned rx0=kg.rx_wp,tx0=kg.tx_wp;
    kg_tre(kg_mem.rxr,&kg.rx_wp,(uint32_t)kg_pa(kg_mem.rxb),(uint32_t)(kg_pa(kg_mem.rxb)>>32),n,KG_TRE_DMA<<16);
    /* CONFIG0: 8-bit words (word_len = 8 - MIN_WORD_LEN 4), packed, mode 0; DFS level / divider. */
    kg_tre(kg_mem.txr,&kg.tx_wp,4u|(1u<<24)|(1u<<25),0,(kg.clk_div&0xfffu)|((kg.clk_src&15u)<<16),(KG_TRE_CONFIG0<<16)|KG_F_CHAIN);
    kg_tre(kg_mem.txr,&kg.tx_wp,KG_SPI_DUPLEX,0,0,(KG_TRE_GO<<16)|KG_F_CHAIN|KG_F_LINK);
    kg_tre(kg_mem.txr,&kg.tx_wp,(uint32_t)kg_pa(kg_mem.txb),(uint32_t)(kg_pa(kg_mem.txb)>>32),n,(KG_TRE_DMA<<16)|KG_F_IEOT);
    kg_cache(kg_mem.rxr+rx0,KG_EL,0);
    for(unsigned o=tx0;o!=kg.tx_wp;o=(o+KG_EL)&1023u)kg_cache(kg_mem.txr+o,KG_EL,0);
    kg_wr(kg.gpi,KG_CH_DB(KG_RX_CH),(uint32_t)kg_pa(kg_mem.rxr+kg.rx_wp));
    kg_wr(kg.gpi,KG_CH_DB(KG_TX_CH),(uint32_t)kg_pa(kg_mem.txr+kg.tx_wp));
    int tx_done=0,rx_done=0;
    uint64_t ev=kg_pa(kg_mem.ev);
    for(unsigned i=0;i<3000 && !(tx_done && rx_done);i++) {   /* up to ~300 ms */
        uint32_t hw=kg_rd(kg.gpi,KG_EV_CNTXT+KG_C4_RP_LO);
        unsigned hw_off=(unsigned)((hw-(uint32_t)ev)&4095u);
        while(kg.ev_rp!=hw_off) {
            uint32_t *e=(uint32_t *)(kg_mem.ev+kg.ev_rp);
            kg_cache(e,KG_EL,1);
            kg.last_ev[0]=e[0];kg.last_ev[1]=e[1];kg.last_ev[2]=e[2];kg.last_ev[3]=e[3];
            unsigned type=(e[3]>>16)&255u,chid=e[3]>>24,code=e[2]>>24;
            if(type==0x22 || type==0x30) {
                if(code==16){kg.err=e[3];kg.step=11;}      /* MSM_GPI_TCE_UNEXP_ERR */
                if(chid==KG_TX_CH)tx_done=1;else if(chid==KG_RX_CH)rx_done=1;
            }
            kg.ev_rp=(kg.ev_rp+KG_EL)&4095u;
        }
        /* recycle: every consumed element is free again (wp = rp - 1 element) */
        kg_wr(kg.gpi,KG_EV_DB,(uint32_t)(ev+((kg.ev_rp+4096u-KG_EL)&4095u)));
        if(tx_done && !rx_done) {   /* RX may complete without its own event: its ring read pointer moved */
            uint32_t rrp=kg_rd(kg.gpi,KG_CH_CNTXT(KG_RX_CH)+KG_C4_RP_LO);
            if(rrp==(uint32_t)kg_pa(kg_mem.rxr+kg.rx_wp))rx_done=1;
        }
        if(!(tx_done && rx_done))delay(100);
    }
    if(!(tx_done && rx_done) || kg.step==11) {
        kg.ch_state[0]=kg_ch_state(0);kg.ch_state[1]=kg_ch_state(1);kg.ev_state=kg_ev_state();
        if(kg.step!=11)kg.step=10;
        return 0;
    }
    kg_cache(kg_mem.rxb,n,1);
    bcopy(kg_mem.rxb,rx,n);
    return 1;
}
#endif

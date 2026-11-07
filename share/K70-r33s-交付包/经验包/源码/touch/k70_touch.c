/* K70 GT9916 (Goodix Berlin-D) touch over GENI SPI SE4, for the UniXNU iPadOS 17 kernel.
 * GT9916 protocol and clock vote: ported from xyyyds0106/ios-vermeer-lab (vermeer_touch.c).
 * SPI engine: UniXNU QcomGeniSpi (touch/qcom-geni-spi, unmodified). K70's SE4 has FIFO_IF_DISABLE set, exactly
 * like sheng (same SM8550 SE4 at 0xa90000, same GPIO44-47/25), so — as sheng's TouchProbe does — transfers use the
 * serial engine's own DMA master behind an apps-SMMU pass-through for the QUP wrapper stream 0xa3. No GPI (r27s
 * showed GPI register access stalls the CPU), no PMIC writes, no firmware/config upload, no storage.
 * Frames go to ShengHidInput's NativeHidInput through native_touch_* (seqlock) + k70_touch_notify(); start gate
 * k70-touch=1; state is exported for the on-screen strip.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <mach/thread_act.h>
#include <machine/machine_routines.h>
#include <machine/atomic.h>
#include <pexpert/pexpert.h>
#include "vermeer_goodix_info.h"
#include "vermeer_touch_clock.h"
#include "SmmuBypass.h"
#include "qcom-geni-spi/GeniSpi.c"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
#include "k70_rpmh.h"
extern vm_offset_t ml_vtophys(vm_offset_t);
extern volatile uint32_t native_touch_seq, native_touch_count;
extern volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];
extern void k70_touch_notify(void);
extern int k70_touch_sink_ready(void);
void k70_touch_start(void);
const char *k70_touch_status_line(unsigned part);

#define VT_SE 0x00a90000u
#define VT_WRAPPER 0x00ac0000u   /* geniqup@ac0000, spi4's wrapper */
#define VT_GCC 0x00100000u
#define VT_GPIO 0x0f100000u /* TLMM base f000000 + Kalama REG_BASE100000 */
#define VT_SMMU 0x15000000u
#define VT_SMMU_SIZE 0xd0000u
#define VT_QUP_SID 0xa3u         /* sm8550.dtsi geniqup@ac0000 iommus = <&apps_smmu 0xa3 0> */
#define VT_SPI_HZ 4000000u       /* conservative; GT9916 parts run at 10+ MHz */
#define VT_VOTE 0x52008u
#define VT_CLOCKS VTC_VOTES
#define VT_RCG 0x184f0u          /* gcc_qupv3_wrap1_s4_clk_src, DFS-managed */
/* Wrapper1 core2x/core/master/slave AHB, SE4. Never SE3. */
static const unsigned vt_halt[5]={0x23294,0x23284,0x2327c,0x23280,0x184e4};
/* The engine's DMA buffers: one 4 KiB page per direction, each translated on its own (kernel image pages are
 * not physically contiguous across pages here). */
static struct {uint8_t tx[4096],rx[4096];} __attribute__((aligned(16384))) vt_dma;
static struct {
    vm_offset_t se,gcc,gpio,wrapper,smmu;
    uint32_t vote_added,pins[4];
    bool pins_saved,root_forced,engine_touched;
    struct geni_spi spi;
    int last_err,smmu_slot;
    unsigned stage_detail,confirmed,nonzero,status_or,resets,cmd_ack,polls,frame_ready,frame_flag;
    uint32_t fw_state,raw[2];
    uint32_t _Atomic event_header;
    unsigned level,div;
    uint64_t sclk;
    struct vt_info info;
    struct vt_frame contacts;
    unsigned _Atomic stage;
    uint64_t _Atomic reports,bad,transactions,emitted;
    uint32_t _Atomic last_header;
    uint32_t _Atomic clock_vote,clock_master,clock_slave,clock_se,clock_root,clock_cfg,clock_dfs;
    uint32_t _Atomic clock_core2x,clock_core;
    uint32_t _Atomic engine_proto,reset_level,interface,irq_level;
    char _Atomic reason[48];
    uint8_t tx[4096],rx[4096];
} touch;
enum { VT_WAIT=1,VT_CLOCK,VT_FIRMWARE,VT_IDENTITY,VT_READY,VT_STOPPED };
static void vt_reason(const char *text)
{
    unsigned i;
    for(i=0;i+1<sizeof(touch.reason) && text[i];i++)touch.reason[i]=text[i];
    touch.reason[i]=0;
    printf("K70 TOUCH: %s\n",text);
}
static uint32_t vt_rd(vm_offset_t base,unsigned reg)
{
    uint32_t v=*(volatile uint32_t *)(base+reg);
    __asm__ volatile("dmb oshld" ::: "memory");return v;
}
static void vt_wr(vm_offset_t base,unsigned reg,uint32_t v)
{
    *(volatile uint32_t *)(base+reg)=v;
    __asm__ volatile("dsb sy" ::: "memory");
}
static uint32_t vt_io_rd(void *ctx,uint32_t reg){return vt_rd((vm_offset_t)ctx,reg);}
static void vt_io_wr(void *ctx,uint32_t reg,uint32_t v){vt_wr((vm_offset_t)ctx,reg,v);}
static void vt_io_delay(void *ctx,unsigned us){(void)ctx;delay(us);}
static uint32_t vt_smmu_rd(void *ctx,unsigned reg){return vt_rd((vm_offset_t)ctx,reg);}
static void vt_smmu_wr(void *ctx,unsigned reg,uint32_t v){vt_wr((vm_offset_t)ctx,reg,v);}
static void vt_sleep(unsigned ms)
{
    uint64_t deadline;clock_interval_to_deadline(ms,NSEC_PER_MSEC,&deadline);
    assert_wait_deadline((event_t)&touch,THREAD_UNINT,deadline);
    thread_block(THREAD_CONTINUE_NULL);
}
static void vt_cache(void *p,size_t n,int clean_only)
{
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
    size_t line=4u<<((ctr>>16)&15u);
    for(uintptr_t a=(uintptr_t)p&~(line-1),end=(uintptr_t)p+n;a<end;a+=line) {
        if(clean_only)__asm__ volatile("dc cvac, %0"::"r"(a):"memory");
        else __asm__ volatile("dc civac, %0"::"r"(a):"memory");
    }
    __asm__ volatile("dsb sy":::"memory");
}
static void vt_dma_clean(void *ctx,void *p,size_t n){(void)ctx;vt_cache(p,n,1);}
static void vt_dma_flush(void *ctx,void *p,size_t n){(void)ctx;vt_cache(p,n,0);}
/* The MMU's own answer (AT S1E1R, as mmu_kvtop); ml_vtophys if the probe faults. */
static uint64_t vt_vtop(const void *p)
{
    vm_offset_t va=(vm_offset_t)p;
    boolean_t enabled=ml_set_interrupts_enabled(FALSE);
    uint64_t par;
    __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1":"=r"(par):"r"(va):"memory");
    ml_set_interrupts_enabled(enabled);
    if(!(par&1u))return (par&0x0000fffffffff000ull)|(va&0xfffu);
    return ml_vtophys(va);
}

/* The panel is 1440x3200 (vt_decode scales the native axes to it); NativeHidInput normalises
 * x over 30480 and y over 20320 to 0..32767, so scale to those maxima here. */
static void vt_publish(const struct vt_frame *frame)
{
    unsigned count=0;
    uint32_t seq=native_touch_seq;
    native_touch_seq=seq|1u;
    __asm__ volatile("dmb ish" ::: "memory");
    for(unsigned i=0;i<VT_CONTACTS;i++) {
        if(!frame->point[i].active)continue;
        native_touch_ids[count]=(int)i;
        native_touch_xs[count]=(int)((uint32_t)frame->point[i].x*30480u/1440u);
        native_touch_ys[count]=(int)((uint32_t)frame->point[i].y*20320u/3200u);
        count++;
    }
    native_touch_count=count;
    __asm__ volatile("dmb ish" ::: "memory");
    native_touch_seq=(seq|1u)+1u;
    __asm__ volatile("dmb ish" ::: "memory");
    k70_touch_notify();
}
static void vt_emit_count(void *ctx,unsigned id,int down,unsigned x,unsigned y)
{
    (void)ctx;(void)id;(void)down;(void)x;(void)y;
    os_atomic_inc(&touch.emitted,relaxed);
}
static int vt_xfer(unsigned n)
{
    touch.last_err=geni_spi_transfer(&touch.spi,touch.tx,touch.rx,n,0);
    return touch.last_err==0;
}
static int vt_spi_read(uint32_t addr,uint8_t *out,unsigned n)
{
    if(!n || n+8>sizeof(touch.tx))return 0;   /* one DMA page per direction */
    bzero(touch.tx,n+8);touch.tx[0]=0xf1;
    for(unsigned i=0;i<4;i++)touch.tx[1+i]=(uint8_t)(addr>>(24-i*8));
    touch.tx[5]=touch.tx[6]=touch.tx[7]=0xff;
    if(!vt_xfer(n+8))return 0;
    bcopy(touch.rx+8,out,n);os_atomic_inc(&touch.transactions,relaxed);return 1;
}
/* Berlin SPI write: 0xf0, 32-bit address, data (goodix_berlin_spi.c). */
static int vt_spi_write(uint32_t addr,const uint8_t *data,unsigned n)
{
    if(n+5>sizeof(touch.tx))return 0;
    touch.tx[0]=0xf0;
    for(unsigned i=0;i<4;i++)touch.tx[i+1]=(uint8_t)(addr>>(24-i*8));
    bcopy(data,touch.tx+5,n);
    if(!vt_xfer(n+5))return 0;
    os_atomic_inc(&touch.transactions,relaxed);return 1;
}
/* brl_send_cmd: struct goodix_ts_cmd {state, ack, len, cmd, data[10]} (16 bytes) with a u8-sum LE16 checksum over
 * len..data appended, written to misc.cmd_addr; the firmware sets ack = 0x80 when it took the command. */
static int vt_send_cmd(uint8_t cmd,const uint8_t *data,unsigned n)
{
    uint8_t buf[16]={0},back[16];
    if(!touch.info.cmd || n>8)return 0;
    buf[2]=(uint8_t)(n+4);buf[3]=cmd;
    for(unsigned i=0;i<n;i++)buf[4+i]=data[i];
    unsigned sum=0;for(unsigned i=2;i<2+n+2;i++)sum+=buf[i];
    buf[4+n]=(uint8_t)sum;buf[5+n]=(uint8_t)(sum>>8);
    for(unsigned tries=0;tries<6;tries++) {
        if(!vt_spi_write(touch.info.cmd,buf,sizeof(buf)))return 0;
        for(unsigned i=0;i<6;i++) {
            if(!vt_spi_read(touch.info.cmd,back,sizeof(back)))return 0;
            touch.cmd_ack=back[1];
            if(back[1]==0x80){vt_sleep(2);return 1;}
            if(back[1]==0x02 || back[1]==0x00){vt_sleep(1);continue;}
            vt_sleep(back[1]==0x03?10:1);break;
        }
    }
    return 0;
}
static int vt_ack_event(void)
{
    /* Clear the event status byte, as goodix_berlin_irq does after every non-zero status.
     * info.event is accepted only after GT9916 identity and IC-info checks.
     */
    static const uint8_t zero=0;
    return vt_spi_write(touch.info.event,&zero,1);
}
static int vt_clock_enable(void)
{
    touch.gcc=ml_io_map(VT_GCC,0x54000);
    if(!touch.gcc){vt_reason("GCC map failed");return 0;}
    uint32_t before=vt_rd(touch.gcc,VT_VOTE);
    touch.vote_added=VT_CLOCKS&~before;
    vt_wr(touch.gcc,VT_VOTE,before|VT_CLOCKS);
    for(unsigned i=0;i<1000;i++) {
        uint32_t votes=vt_rd(touch.gcc,VT_VOTE),core2x=vt_rd(touch.gcc,vt_halt[0]),core=vt_rd(touch.gcc,vt_halt[1]);
        uint32_t master=vt_rd(touch.gcc,vt_halt[2]),slave=vt_rd(touch.gcc,vt_halt[3]),se=vt_rd(touch.gcc,vt_halt[4]);
        uint32_t cmd=vt_rd(touch.gcc,VT_RCG);
        os_atomic_store(&touch.clock_vote,votes,relaxed);os_atomic_store(&touch.clock_master,master,relaxed);
        os_atomic_store(&touch.clock_core2x,core2x,relaxed);os_atomic_store(&touch.clock_core,core,relaxed);
        os_atomic_store(&touch.clock_slave,slave,relaxed);os_atomic_store(&touch.clock_se,se,relaxed);
        os_atomic_store(&touch.clock_root,cmd,relaxed);os_atomic_store(&touch.clock_cfg,vt_rd(touch.gcc,VT_RCG+4),relaxed);
        os_atomic_store(&touch.clock_dfs,vt_rd(touch.gcc,VT_RCG+0x14),relaxed);
        if(vtc_ready(votes,core2x,core,master,slave,se))return 1;
        /* Match clk_rcg2_set_force_enable when SE4's root is parked. This
         * requests the existing root only: no rate, parent, DFS or PLL edit.
         */
        if(i==100 && (votes&VT_CLOCKS)==VT_CLOCKS && (se&0x80000000u) &&
           (cmd&0x80000000u) && !(cmd&3u)) {
            touch.root_forced=true;vt_wr(touch.gcc,VT_RCG,cmd|2u);
        }
        delay(10);
    }
    vt_reason("SE4/AHB clock vote not ready");return 0;
}
/* clk-rcg2.c: source rate by select (gcc_parent_map_0: 0 TCXO, 1 GPLL0 600 MHz, 6 GPLL0 even 300 MHz), half-integer
 * divider, then M/N (N stored inverted). Same arithmetic as sheng's TouchBoard.h. */
static uint64_t vt_rcg_hz(uint32_t cfg,uint32_t mreg,uint32_t nreg)
{
    unsigned src=(cfg>>8)&7,mode=(cfg>>12)&3,hid=cfg&0x1f;
    uint64_t f=src==0?19200000ull:src==1?600000000ull:src==6?300000000ull:0;
    if(hid){f*=2;f/=hid+1;}
    if(mode){uint32_t m=mreg&0xffff,n=((~nreg)&0xffff)+m;if(n){f*=m;f/=n;}}
    return f;
}
/* Highest SPI rate not above target over the DFS levels (level selected through GENI_CLK_SEL). */
static int vt_choose_clock(unsigned target)
{
    int found=0;uint64_t best=0;
    unsigned os=touch.spi.oversampling?touch.spi.oversampling:1;
    int dfs=vt_rd(touch.gcc,VT_RCG+0x14)&1u;
    for(unsigned l=0;l<(dfs?8u:1u);l++) {
        uint64_t f=dfs?vt_rcg_hz(vt_rd(touch.gcc,VT_RCG+0x1c+4*l),vt_rd(touch.gcc,VT_RCG+0x5c+4*l),vt_rd(touch.gcc,VT_RCG+0x9c+4*l))
                      :vt_rcg_hz(vt_rd(touch.gcc,VT_RCG+4),vt_rd(touch.gcc,VT_RCG+8),vt_rd(touch.gcc,VT_RCG+0xc));
        if(!f)continue;
        uint64_t d=(f+(uint64_t)os*target-1)/((uint64_t)os*target);if(d<1)d=1;if(d>0xfff)continue;
        uint64_t actual=f/(os*d);if(actual>target)continue;
        if(!found || actual>best){found=1;best=actual;touch.level=l;touch.div=(unsigned)d;touch.sclk=f;}
    }
    return found;
}
/* Engine DMA buffers and the QUP stream's SMMU pass-through (sheng TouchProbe prepareDma). */
static int vt_dma_prepare(void)
{
    uint64_t tx=vt_vtop(vt_dma.tx),rx=vt_vtop(vt_dma.rx);
    if(!tx || !rx || (tx&0xfffu) || (rx&0xfffu)){vt_reason("DMA page translation failed");return 0;}
    bzero(&vt_dma,sizeof(vt_dma));vt_cache(&vt_dma,sizeof(vt_dma),0);
    touch.smmu=ml_io_map(VT_SMMU,VT_SMMU_SIZE);
    if(!touch.smmu){vt_reason("SMMU map failed");return 0;}
    uint64_t lo=tx<rx?tx:rx,hi=(tx<rx?rx:tx)+4096u;
    struct smmu_io io={(void *)touch.smmu,vt_smmu_rd,vt_smmu_wr};
    struct smmu_plan plan=smmu_plan_bypass(&io,VT_QUP_SID,lo,hi-lo);
    touch.smmu_slot=(int)plan.slot;
    const char *e=smmu_configure_bypass(&io,&plan,VT_QUP_SID);
    if(e){vt_reason(e);return 0;}
    touch.spi.use_dma=1;
    touch.spi.dma=(struct geni_dma){.ctx=NULL,.clean=vt_dma_clean,.clean_inval=vt_dma_flush,.inval=vt_dma_flush,
        .tx_virt=vt_dma.tx,.rx_virt=vt_dma.rx,.tx_phys=tx,.rx_phys=rx,.size=sizeof(vt_dma.tx)};
    return 1;
}
static int vt_engine_setup(void)
{
    touch.se=ml_io_map(VT_SE,0x4000);touch.gpio=ml_io_map(VT_GPIO,0x30000);touch.wrapper=ml_io_map(VT_WRAPPER,0x2000);
    if(!touch.se || !touch.gpio || !touch.wrapper){vt_reason("SE4/TLMM/QUP map failed");return 0;}
    touch.spi.se=(struct geni_io){(void *)touch.se,vt_io_rd,vt_io_wr,vt_io_delay};
    touch.spi.wrapper=(struct geni_io){(void *)touch.wrapper,vt_io_rd,vt_io_wr,vt_io_delay};
    if(geni_spi_read_info(&touch.spi)){vt_reason("SE4 info read failed");return 0;}
    os_atomic_store(&touch.engine_proto,touch.spi.info.protocol,relaxed);
    os_atomic_store(&touch.interface,touch.spi.info.if_disable,relaxed);
    if(touch.spi.info.protocol!=GENI_PROTO_SPI){vt_reason("SE4 firmware is not SPI");return 0;}
    /* Power-on, goodix_berlin_power_on order: reset (GPIO24, active low) held low as a GPIO output, iovdd
     * (L12B 1.8 V) through RPMh, 3 ms, avdd (L2B 3.2 V), 15 ms, reset high; the device confirm and the 100 ms
     * firmware boot follow once the bus is up. ABL leaves both rails off (r28s read all zeros, IRQ low).
     */
    uint32_t reset=vt_rd(touch.gpio,24*0x1000);
    vt_wr(touch.gpio,24*0x1000+4,0);
    vt_wr(touch.gpio,24*0x1000,(reset&~0x3fcu)|0x200u|(1u<<6));   /* GPIO function, output, 4 mA */
    touch.stage_detail=1;   /* the RSC is the next new MMIO: a freeze here names it */
    if(!kr_init() || !kr_ldo_on(1,"ldob12",1800)){vt_reason(kr.error?kr.error:"RPMh iovdd vote failed");return 0;}
    vt_sleep(3);
    if(!kr_ldo_on(0,"ldob2",3200)){vt_reason(kr.error?kr.error:"RPMh avdd vote failed");return 0;}
    touch.stage_detail=2;
    kr.read_en=kr_read(kr.addr[0]+KR_VRM_ENABLE);kr.read_mv=kr_read(kr.addr[0]+KR_VRM_VOLTAGE);   /* avdd as RPMh sees it */
    vt_sleep(15);   /* reset stays low until the SPI engine is ready, so the device confirm lands right after it */
    for(unsigned i=0;i<4;i++) {
        unsigned reg=(44+i)*0x1000;touch.pins[i]=vt_rd(touch.gpio,reg);
        /* Exact active DT: ALT1, 6mA; pulldown MISO/MOSI/SCLK, pullup CS. */
        uint32_t value=(touch.pins[i]&~0x3ffu)|4u|(2u<<6)|(i==3?3u:1u);
        vt_wr(touch.gpio,reg,value);
    }
    touch.pins_saved=true;
    if(touch.spi.info.fifo_disabled && !vt_dma_prepare())return 0;
    touch.engine_touched=true;
    touch.spi.poll_limit=200000;
    touch.spi.poll_only=2;   /* sheng: keep the top-level IRQ enable so the status registers stay visible to the poll */
    int r=geni_spi_init(&touch.spi);
    if(r){touch.last_err=r;vt_reason("GENI SPI init refused");return 0;}
    if(!vt_choose_clock(VT_SPI_HZ)){vt_reason("no SE4 clock level fits");return 0;}
    r=geni_spi_set_clock(&touch.spi,VT_SPI_HZ,(unsigned)touch.sclk,touch.level);
    if(!r)r=geni_spi_setup(&touch.spi,0,0);
    if(r){touch.last_err=r;vt_reason("GENI SPI clock/mode refused");return 0;}
    /* goodix_berlin_power_on, after reset release: 4 ms, then confirm the device (write 8 x 0xAA to the boot
     * option, read it back; up to 3 tries 5 ms apart), then 100 ms for the firmware to boot. Without the
     * confirm the controller can stay in its boot ROM: it answers the version read but never scans. */
    vt_wr(touch.gpio,24*0x1000+4,2);
    os_atomic_store(&touch.reset_level,vt_rd(touch.gpio,24*0x1000+4),relaxed);
    vt_sleep(4);
    touch.confirmed=0;
    for(unsigned tries=0;tries<3 && !touch.confirmed;tries++) {
        uint8_t back[8];
        static const uint8_t aa[8]={0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa};
        if(vt_spi_write(0x10000,aa,8) && vt_spi_read(0x10000,back,8) && !memcmp(aa,back,8))touch.confirmed=1;
        else vt_sleep(5);
    }
    vt_sleep(100);
    os_atomic_store(&touch.irq_level,vt_rd(touch.gpio,25*0x1000+4),relaxed);
    return 1;
}
static void vt_restore(void)
{
    /* Once the engine has been programmed (and maybe left mid-transfer) everything stays as it is until
     * reboot: disabling an active engine or its clocks could turn a recoverable error into a fault. */
    if(touch.engine_touched)return;
    if(touch.pins_saved)for(unsigned i=0;i<4;i++)vt_wr(touch.gpio,(44+i)*0x1000,touch.pins[i]);
    if(touch.gcc && touch.root_forced)vt_wr(touch.gcc,VT_RCG,vt_rd(touch.gcc,VT_RCG)&~2u);
    if(touch.gcc && touch.vote_added)
        vt_wr(touch.gcc,VT_VOTE,vt_rd(touch.gcc,VT_VOTE)&~touch.vote_added);
}
/* Is the analog front end scanning at all? brld_get_cap_data: 0x90/0x81 selects raw-data frames, the frame flag at
 * misc.frame_data_addr (+24) gets bit 7 when a frame is ready (cleared first); the mutual raw data follows the frame
 * header, attribute and log blocks (+28, +30, +32 lengths) and 8 more bytes. Then 0x90/0 returns to coordinates. */
static void vt_selftest(void)
{
    static const uint8_t raw=0x81,off=0,zero=0;
    uint32_t frame=touch.info.frame;
    if(!frame || !vt_send_cmd(0x90,&raw,1)){touch.frame_ready=9;return;}
    for(unsigned f=0;f<2;f++) {
        uint8_t flag=0;
        touch.frame_ready=0;
        vt_spi_write(frame,&zero,1);
        for(unsigned i=0;i<50;i++) {
            vt_sleep(5);
            if(vt_spi_read(frame,&flag,1) && (flag&0x80u)){touch.frame_ready=1;break;}
        }
        touch.frame_flag=flag;
        if(!touch.frame_ready)break;
        uint8_t v[4];
        if(vt_spi_read(frame+touch.info.frame_skip+8,v,4))touch.raw[f]=vt_le32(v);
    }
    vt_send_cmd(0x90,&off,1);
    vt_spi_write(touch.info.event,&zero,1);
}
static int vt_identify(void)
{
    uint8_t version[28],length[2],data[1024];
    if(!vt_spi_read(0x10014,version,sizeof(version))){vt_reason("SPI version read failed");return 0;}
    /* What came back: all 00 or all ff means nobody answered (no power or still in reset). */
    os_atomic_store(&touch.last_header,vt_le32(version),relaxed);
    if(!vt_version_9916(version,sizeof(version))){vt_reason("not a GT9916 (version check)");return 0;}
    if(!vt_spi_read(0x10070,length,2)){vt_reason("SPI IC-info length read failed");return 0;}
    unsigned n=vt_le16(length);
    if(n<181 || n>=sizeof(data) || !vt_spi_read(0x10070,data,n) || !vt_info_decode(data,n,&touch.info)) {
        vt_reason("IC-info guard refused");return 0;
    }
    printf("K70 TOUCH: GT9916 verified, event=%08x axes=%ux%u\n",touch.info.event,touch.info.max_x,touch.info.max_y);
    /* Xiaomi's 9916 driver (brl_reset on vermeer) sends goodix_htc_enable(0) after every reset: command 0x90 with
     * data 0 turns host touch computing (raw frames for the host) off, so the firmware reports coordinates. */
    static const uint8_t off=0;
    vt_send_cmd(0x90,&off,1);
    vt_selftest();
    return 1;
}
/* Host touch processing. Vermeer's GT9916 firmware leaves coordinates to Android's THP stack (Xiaomi's driver reads
 * thp_frame from misc.frame_data_addr when enable_touch_raw is set; in coordinate mode not one event ever arrived,
 * r30s-r32s), so, as sheng does for its Novatek panel, contacts are computed here from capacitance frames.
 * 0x90/0x82 selects firmware-baselined difference frames (brld_get_cap_data); a frame is ready when bit 7 of the
 * byte at frame_data_addr is set, the matrix (tx-major: [drv][sen], int16 LE) follows the frame head, attribute
 * and log blocks and 8 more bytes, and the flag is cleared by writing 0 (brl_after_event_handler).
 * Boot args: k70-touch-thr (signal threshold, default 200), k70-touch-map (bit0 swap which matrix axis is the
 * screen's long axis, bit1 mirror x, bit2 mirror y). */
#define VT_NODES_MAX 1500u
static int16_t vt_m[VT_NODES_MAX];
static uint8_t vt_used[VT_NODES_MAX];
static uint8_t vt_fbuf[4096];
static struct { unsigned thr,map,frames,peaks,mode_ok; int max,max_t,max_r; } thp;
static int vt_thp_start(void)
{
    unsigned t=touch.info.tx,r=touch.info.rx;
    if(!touch.info.frame || t<4 || r<4 || t*r>VT_NODES_MAX || touch.info.frame_skip+8+t*r*2>4000)return 0;
    thp.thr=200;thp.map=0;
    PE_parse_boot_argn("k70-touch-thr",&thp.thr,sizeof(thp.thr));
    PE_parse_boot_argn("k70-touch-map",&thp.map,sizeof(thp.map));
    static const uint8_t diff=0x82,zero=0;
    thp.mode_ok=vt_send_cmd(0x90,&diff,1);
    vt_spi_write(touch.info.frame,&zero,1);
    return 1;
}
/* 1: a new frame is in vt_m, 0: none ready yet, -1: bus failure */
static int vt_thp_frame(void)
{
    uint8_t flag=0;
    static const uint8_t zero=0;
    if(!vt_spi_read(touch.info.frame,&flag,1))return -1;
    if(!(flag&0x80u))return 0;
    unsigned nodes=touch.info.tx*touch.info.rx,off=touch.info.frame_skip+8;
    if(!vt_spi_read(touch.info.frame,vt_fbuf,off+nodes*2))return -1;
    if(!vt_spi_write(touch.info.frame,&zero,1))return -1;
    for(unsigned i=0;i<nodes;i++)vt_m[i]=(int16_t)vt_le16(vt_fbuf+off+2*i);
    thp.frames++;
    return 1;
}
static void vt_thp_process(void)
{
    unsigned T=touch.info.tx,R=touch.info.rx,nodes=T*R;
    int thr=(int)thp.thr;
    struct vt_frame frame={0};
    struct {int x,y;} pk[5];unsigned np=0;
    bzero(vt_used,nodes);
    thp.max=0;
    for(unsigned k=0;k<5;k++) {
        int best=0;unsigned bi=0;
        for(unsigned i=0;i<nodes;i++) {
            int v=vt_m[i];if(v<0)v=-v;
            if(!vt_used[i] && v>best){best=v;bi=i;}
        }
        if(!k){thp.max=best;thp.max_t=(int)(bi/R);thp.max_r=(int)(bi%R);}
        if(best<thr)break;
        int pt=(int)(bi/R),pr=(int)(bi%R);
        long sw=0,st=0,sr=0;
        for(int dt=-1;dt<=1;dt++)for(int dr=-1;dr<=1;dr++) {
            int t=pt+dt,r=pr+dr;
            if(t<0 || r<0 || t>=(int)T || r>=(int)R)continue;
            int v=vt_m[t*(int)R+r];if(v<0)v=-v;
            int w=v-thr/2;if(w<=0)continue;
            sw+=w;st+=(long)w*(2*t+1);sr+=(long)w*(2*r+1);
        }
        for(int dt=-2;dt<=2;dt++)for(int dr=-2;dr<=2;dr++) {
            int t=pt+dt,r=pr+dr;
            if(t>=0 && r>=0 && t<(int)T && r<(int)R)vt_used[t*(int)R+r]=1;
        }
        if(!sw)continue;
        /* centre positions in half-node units over [0, 2N): the long matrix axis is the screen's long (3200) axis */
        long ct=st,cr=sr;
        int t_is_y=(T>=R)^((thp.map&1u)!=0);
        long cx=t_is_y?cr:ct,cy=t_is_y?ct:cr;
        long nx=2L*(t_is_y?R:T),ny=2L*(t_is_y?T:R);
        int x=(int)(cx*1440/(nx*sw)),y=(int)(cy*3200/(ny*sw));
        if(thp.map&2u)x=1439-x;
        if(thp.map&4u)y=3199-y;
        if(x<0)x=0;if(x>1439)x=1439;if(y<0)y=0;if(y>3199)y=3199;
        pk[np].x=x;pk[np].y=y;np++;
    }
    thp.peaks=np;
    /* Keep a finger's slot across frames: nearest previous contact within 300 px, else the first free slot. */
    unsigned claimed=0;
    for(unsigned k=0;k<np;k++) {
        int slot=-1;long bestd=300L*300L;
        for(unsigned s=0;s<VT_CONTACTS;s++) {
            if(!touch.contacts.point[s].active || (claimed&(1u<<s)))continue;
            long dx=(long)touch.contacts.point[s].x-pk[k].x,dy=(long)touch.contacts.point[s].y-pk[k].y;
            if(dx*dx+dy*dy<bestd){bestd=dx*dx+dy*dy;slot=(int)s;}
        }
        if(slot<0)for(unsigned s=0;s<VT_CONTACTS;s++)
            if(!touch.contacts.point[s].active && !(claimed&(1u<<s))){slot=(int)s;break;}
        if(slot<0)continue;
        claimed|=1u<<slot;
        frame.point[slot].active=1;frame.point[slot].x=(uint16_t)pk[k].x;frame.point[slot].y=(uint16_t)pk[k].y;
    }
    unsigned before=0;
    for(unsigned s=0;s<VT_CONTACTS;s++)if(touch.contacts.point[s].active)before++;
    if(!np && !before)return;
    vt_apply(&touch.contacts,&frame,vt_emit_count,NULL);
    vt_publish(&touch.contacts);
    os_atomic_inc(&touch.reports,relaxed);
}
static void vt_thread(void *arg,wait_result_t result)
{
    (void)arg;(void)result;
    /* NativeHidInput registers its sink once IOHIDFamily has matched it. */
    for(unsigned i=0;i<1800 && !k70_touch_sink_ready();i++)vt_sleep(100);
    if(!k70_touch_sink_ready()){vt_reason("HID touch sink never registered");goto stopped;}
    os_atomic_store(&touch.stage,VT_CLOCK,relaxed);
    if(!vt_clock_enable())goto stopped;
    os_atomic_store(&touch.stage,VT_FIRMWARE,relaxed);
    if(!vt_engine_setup())goto stopped;
    os_atomic_store(&touch.stage,VT_IDENTITY,relaxed);
    if(!vt_identify())goto stopped;
    os_atomic_store(&touch.stage,VT_READY,relaxed);
    vt_reason("GT9916 ready");
    if(vt_thp_start()) {
        vt_reason("GT9916 host touch");
        for(;;) {
            int r=vt_thp_frame();
            if(r<0){vt_reason("SPI frame read failed");goto stopped;}
            if(!r){vt_sleep(4);continue;}
            vt_thp_process();
        }
    }
    for(;;) {
        uint8_t packet[VT_PACKET_MAX];
        if(!vt_spi_read(touch.info.event,packet,8)){vt_reason("SPI event read failed");break;}
        os_atomic_store(&touch.last_header,vt_le32(packet),relaxed);
        if(!(++touch.polls&127u) && touch.info.state) {   /* about once a second: the firmware state word */
            uint8_t st[4];
            if(vt_spi_read(touch.info.state,st,4))touch.fw_state=vt_le32(st);
        }
        if(!packet[0]){vt_sleep(8);continue;}
        /* goodix_berlin_irq: any non-zero status is handled, then cleared. */
        touch.nonzero++;touch.status_or|=packet[0];
        os_atomic_store(&touch.event_header,vt_le32(packet),relaxed);
        if(!vt_checksum(packet,8)){os_atomic_inc(&touch.bad,relaxed);goto clear;}
        if(packet[0]&0x80u) {
            unsigned count=packet[2]&15u,n=count?8+count*8+2:8;
            struct vt_frame frame;
            if(count>10 || (n>8 && !vt_spi_read(touch.info.event+8,packet+8,n-8)) ||
               vt_decode(packet,n,touch.info.max_x,touch.info.max_y,&frame)!=VT_TOUCH)os_atomic_inc(&touch.bad,relaxed);
            else {
                vt_apply(&touch.contacts,&frame,vt_emit_count,NULL);
                vt_publish(&touch.contacts);
                os_atomic_inc(&touch.reports,relaxed);
            }
        }
        if((packet[0]&0x40u) && packet[2]==3) {   /* REQUEST_CODE_RESET: goodix_berlin_request_handle_reset */
            touch.resets++;
            vt_wr(touch.gpio,24*0x1000+4,0);vt_sleep(2);vt_wr(touch.gpio,24*0x1000+4,2);vt_sleep(100);
            continue;
        }
clear:
        if(!vt_ack_event()){vt_reason("SPI event ack failed");break;}
        vt_sleep(8);
    }
stopped:
    vt_release_all(&touch.contacts,vt_emit_count,NULL);
    vt_publish(&touch.contacts);
    vt_restore();os_atomic_store(&touch.stage,VT_STOPPED,relaxed);
    thread_terminate(current_thread());
}
void k70_touch_start(void)
{
    unsigned int enabled=0;
    if(!PE_parse_boot_argn("k70-touch",&enabled,sizeof(enabled)) || enabled!=1)return;
    vt_reason("waiting for HID sink");
    os_atomic_store(&touch.stage,VT_WAIT,relaxed);
    thread_t thread=THREAD_NULL;
    kern_return_t result=kernel_thread_start(vt_thread,NULL,&thread);
    if(result==KERN_SUCCESS)thread_deallocate(thread);
    else os_atomic_store(&touch.stage,VT_STOPPED,relaxed);
}
/* Two strip rows: counters, then the reason with the clock (early) or engine/DMA snapshot that explains the stage. */
const char *k70_touch_status_line(unsigned part)
{
    static char line[2][100];
    unsigned stage=os_atomic_load(&touch.stage,relaxed);
    if(!part) {   /* reports, bad, non-zero statuses (count|OR of status bits), confirm, live IRQ pin, last event header */
        unsigned irq=touch.gpio?(vt_rd(touch.gpio,25*0x1000+4)&1u):9u;
        (void)irq;   /* reports, frames, contacts, strongest signal and where, threshold/map, matrix size, mode ack */
        snprintf(line[0],sizeof(line[0]),"TOUCH st%u rep=%llu fr=%u n=%u M=%d@%d,%d thr=%u map=%u %ux%u md%u h%02x",stage,
            (unsigned long long)os_atomic_load(&touch.reports,relaxed),thp.frames,thp.peaks,thp.max,thp.max_t,thp.max_r,
            thp.thr,thp.map,touch.info.tx,touch.info.rx,thp.mode_ok,touch.cmd_ack);
        return line[0];
    }
    if(stage<=VT_CLOCK || (stage==VT_STOPPED && !touch.se))
        snprintf(line[1],sizeof(line[1]),"%s v%08x se%08x m%08x s%08x c%08x",(const char *)touch.reason,
            os_atomic_load(&touch.clock_vote,relaxed),os_atomic_load(&touch.clock_se,relaxed),
            os_atomic_load(&touch.clock_master,relaxed),os_atomic_load(&touch.clock_slave,relaxed),os_atomic_load(&touch.clock_core,relaxed));
    else   /* avdd as RPMh reads it back (enable/mV), raw-frame self test (ready/flag, two raw words), power step */
        snprintf(line[1],sizeof(line[1]),"%s A%x/%u F%u/%02x %08x %08x p%u e%d",(const char *)touch.reason,
            kr.read_en,kr.read_mv,touch.frame_ready,touch.frame_flag,touch.raw[0],touch.raw[1],touch.stage_detail,touch.last_err);
    return line[1];
}

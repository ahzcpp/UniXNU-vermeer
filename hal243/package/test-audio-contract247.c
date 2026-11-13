/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "K70AudioContract247.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {checks++;assert(x);}while(0)
int main(void) {
    K70AudioMap247 m[3]={{0x100000,0x4000},{0x200000,262144},{0x300000,131072}},bad[3];
    ShengAudioControl c={0};
    c.magic=SHENG_AUDIO_MAGIC;c.version=1;c.rate=48000;c.out_channels=2;c.in_channels=1;
    c.period_frames=480;c.ring_frames=65536;c.lead_frames=14400;c.ticks_per_frame_q32=400ull<<32;
    CHECK(k70_audio_maps247(m));CHECK(k70_audio_contract247(&c,625,12));
    CHECK(!k70_audio_contract247(&c,125,3)); /* 24MHz kernel timebase mismatches19.2MHzDSP timeline */
    CHECK(!k70_audio_contract247(&c,0,12));CHECK(!k70_audio_contract247(&c,625,0));CHECK(!k70_audio_contract247(0,625,12));CHECK(!k70_audio_maps247(0));
    for(unsigned i=0;i<3;i++) {
        memcpy(bad,m,sizeof m);bad[i].address=0;CHECK(!k70_audio_maps247(bad));
        memcpy(bad,m,sizeof m);bad[i].bytes=1;CHECK(!k70_audio_maps247(bad));
        memcpy(bad,m,sizeof m);bad[i].address=UINT64_MAX-3;bad[i].bytes=8;CHECK(!k70_audio_maps247(bad));
        for(unsigned j=0;j<3;j++)if(i!=j){memcpy(bad,m,sizeof m);bad[i].address=m[j].address;CHECK(!k70_audio_maps247(bad));}
    }
    size_t n=k70_audio_control_bytes247();
    for(size_t i=0;i<n;i++){memcpy(bad,m,sizeof m);bad[0].bytes=i;CHECK(!k70_audio_maps247(bad));}
    for(unsigned i=0;i<sizeof(c);i++) {
        ShengAudioControl b=c;((unsigned char*)&b)[i]^=0xff;
        /* Only layout/timing are validated here: changing legitimate live counters is allowed. */
        if(i<32 && i!=28 && i!=29 && i!=30 && i!=31)CHECK(!k70_audio_contract247(&b,625,12));
    }
    for(uint32_t lead=0;lead<65536;lead++){c.lead_frames=lead;CHECK(k70_audio_contract247(&c,625,12)==(lead>0 && lead<65056));}
    c.lead_frames=14400;
    for(uint32_t hz=1000000;hz<=100000000;hz+=1009){
        c.ticks_per_frame_q32=(uint64_t)((double)hz/48000.0*4294967296.0);
        CHECK(k70_audio_contract247(&c,1000000000u,hz));
    }
    c.ticks_per_frame_q32=0;CHECK(!k70_audio_contract247(&c,625,12));
    c.ticks_per_frame_q32=UINT64_MAX;CHECK(!k70_audio_contract247(&c,625,12));
    printf("PASS %u assertions: map bounds/overlap, ABI dimensions and true timebase agreement; no hardware\n",checks);
    return 0;
}

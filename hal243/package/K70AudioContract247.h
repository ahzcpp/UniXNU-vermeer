/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* User-space shared-map qualification, independent of CoreAudio SDK headers.
 * Caller checks control extent BEFORE copying this immutable layout snapshot.
 * Sizes remain fixed by ABI1; mutable shared metadata never bounds ring IO.
 */
#ifndef K70_AUDIO_CONTRACT247_H
#define K70_AUDIO_CONTRACT247_H
#include <stddef.h>
#include <stdint.h>
#include "ShengAudioAbi.h"
typedef struct { uint64_t address,bytes; } K70AudioMap247;
static inline size_t k70_audio_control_bytes247(void) {
    size_t n=sizeof(ShengAudioControl);
    if(n<SHENG_AUDIO_DIAG_OFFSET+sizeof(ShengAudioDiag))n=SHENG_AUDIO_DIAG_OFFSET+sizeof(ShengAudioDiag);
    if(n<SHENG_AUDIO_TRACE_OFFSET+sizeof(ShengAudioTrace))n=SHENG_AUDIO_TRACE_OFFSET+sizeof(ShengAudioTrace);
    return n;
}
static inline int k70_audio_span247(K70AudioMap247 m,uint64_t minimum,uint64_t alignment) {
    return m.address && m.bytes>=minimum && m.bytes<=UINT64_MAX-m.address && !(m.address&(alignment-1u));
}
static inline int k70_audio_maps247(const K70AudioMap247 m[3]) {
    if(!m || !k70_audio_span247(m[0],k70_audio_control_bytes247(),8) ||
       !k70_audio_span247(m[1],(uint64_t)SHENG_AUDIO_RING_FRAMES*SHENG_AUDIO_OUT_CHANNELS*2u,2) ||
       !k70_audio_span247(m[2],(uint64_t)SHENG_AUDIO_RING_FRAMES*SHENG_AUDIO_IN_CHANNELS*2u,2))return 0;
    for(unsigned i=0;i<3;i++)for(unsigned j=i+1;j<3;j++)
        if(m[i].address<m[j].address+m[j].bytes && m[j].address<m[i].address+m[i].bytes)return 0;
    return 1;
}
static inline int k70_audio_contract247(const ShengAudioControl *c,uint32_t numer,uint32_t denom) {
    if(!c || !numer || !denom || c->magic!=SHENG_AUDIO_MAGIC || c->version!=SHENG_AUDIO_VERSION ||
       c->rate!=SHENG_AUDIO_RATE || c->out_channels!=SHENG_AUDIO_OUT_CHANNELS || c->in_channels!=SHENG_AUDIO_IN_CHANNELS ||
       c->period_frames!=SHENG_AUDIO_PERIOD_FRAMES || c->ring_frames!=SHENG_AUDIO_RING_FRAMES ||
       !c->lead_frames || c->lead_frames>=SHENG_AUDIO_RING_FRAMES-SHENG_AUDIO_PERIOD_FRAMES || !c->ticks_per_frame_q32)return 0;
    /* Actual20H392 caulk clock also computes 1e9*denom/numer. Do not rewrite it.
     * Reject mismatched board/user clocks before any stream can start. */
    const double hz=1000000000.0*(double)denom/(double)numer;
    const double ticks=(double)c->ticks_per_frame_q32/4294967296.0;
    const double observed=ticks*(double)SHENG_AUDIO_RATE;
    return observed>=hz*(1.0-0.000001) && observed<=hz*(1.0+0.000001);
}
#endif

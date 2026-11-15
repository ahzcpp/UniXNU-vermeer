/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Darwin ioctl ABI from the actual local running XNU source (see INPUTS.json).
 * Prefer the SDK header; fallback is needed only when iPhoneOS omits it. */
#ifndef NETDIAG_BPF_ABI_H
#define NETDIAG_BPF_ABI_H
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <net/if.h>
#if __has_include(<net/bpf.h>)
#include <net/bpf.h>
typedef struct bpf_program NdBpfProgram;
#define ND_BIOCGBLEN BIOCGBLEN
#define ND_BIOCSBLEN BIOCSBLEN
#define ND_BIOCSETF BIOCSETF
#define ND_BIOCGDLT BIOCGDLT
#define ND_BIOCGETIF BIOCGETIF
#define ND_BIOCSETIF BIOCSETIF
#define ND_BIOCIMMEDIATE BIOCIMMEDIATE
#define ND_BIOCSHDRCMPLT BIOCSHDRCMPLT
#define ND_BIOCSSEESENT BIOCSSEESENT
_Static_assert(sizeof(struct bpf_insn)==8,"Darwin classic BPF instruction");
_Static_assert(offsetof(struct bpf_hdr,bh_caplen)==8&&offsetof(struct bpf_hdr,bh_datalen)==12&&offsetof(struct bpf_hdr,bh_hdrlen)==16,"Darwin LP64 BPF timeval32 header");
#else
typedef struct {uint32_t bf_len;NdBpfInsn *bf_insns;} NdBpfProgram;
#define ND_BIOCGBLEN _IOR('B',102,unsigned int)
#define ND_BIOCSBLEN _IOWR('B',102,unsigned int)
#define ND_BIOCSETF _IOW('B',103,NdBpfProgram)
#define ND_BIOCGDLT _IOR('B',106,unsigned int)
#define ND_BIOCGETIF _IOR('B',107,struct ifreq)
#define ND_BIOCSETIF _IOW('B',108,struct ifreq)
#define ND_BIOCIMMEDIATE _IOW('B',112,unsigned int)
#define ND_BIOCSHDRCMPLT _IOW('B',117,unsigned int)
#define ND_BIOCSSEESENT _IOW('B',119,unsigned int)
#endif
_Static_assert(sizeof(NdBpfProgram)==16&&offsetof(NdBpfProgram,bf_insns)==8,"64bit BPF program ABI");
_Static_assert(sizeof(struct ifreq)==32,"Darwin ifreq ABI");
_Static_assert(ND_BIOCSETIF==0x8020426cUL&&ND_BIOCSETF==0x80104267UL&&ND_BIOCGDLT==0x4004426aUL,"Darwin BPF ioctl encodings");
#endif

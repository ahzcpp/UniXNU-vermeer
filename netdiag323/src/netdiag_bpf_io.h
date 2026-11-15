/* SPDX-License-Identifier: BSD-2-Clause-Patent. Native Darwin I/O only. */
#ifndef NETDIAG_BPF_IO_H
#define NETDIAG_BPF_IO_H
#include "netdiag_bpf_core.h"
#include "netdiag_bpf_abi.h"
typedef struct {int fd;uint8_t *buf;unsigned capacity;size_t at,have;} NdBpfPort;
static int nd_bpf_fail_close(int fd,const char **phase,const char *where,int error) {
 if(fd>=0)close(fd);*phase=where;errno=error;return -1;
}
static int nd_bpf_open_native(NdBpfPort *p,unsigned index,const uint8_t mac[6],const char **phase) {
 if(!p||!phase||!nd_bpf_mac_valid(mac)||!index||if_nametoindex("en0")!=index){errno=EINVAL;return -1;}
 int fd=-1,last=ENOENT;
 for(unsigned minor=0;minor<64;minor++) {
  char path[32];snprintf(path,sizeof path,"/dev/bpf%u",minor);
  fd=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
  if(fd>=0)break;
  last=errno;if(last!=EBUSY&&last!=ENOENT)return nd_bpf_fail_close(-1,phase,"open",last);
 }
 if(fd<0)return nd_bpf_fail_close(-1,phase,"open",last);
 struct stat st;
 if(fstat(fd,&st))return nd_bpf_fail_close(fd,phase,"character-device",errno);
 if(!S_ISCHR(st.st_mode))return nd_bpf_fail_close(fd,phase,"character-device",ENODEV);
 unsigned size=16384,one=1,zero=0,dlt=0;
 if(ioctl(fd,ND_BIOCSBLEN,&size)<0)return nd_bpf_fail_close(fd,phase,"buffer-size",errno);
 NdBpfProgram program={.bf_len=sizeof(nd_dhcp_bpf_filter)/sizeof(nd_dhcp_bpf_filter[0]),.bf_insns=(void *)nd_dhcp_bpf_filter};
 /* Before binding there is no capture. Install restrictive filter FIRST. */
 if(ioctl(fd,ND_BIOCSETF,&program)<0)return nd_bpf_fail_close(fd,phase,"dhcp-filter",errno);
 struct ifreq req;memset(&req,0,sizeof req);memcpy(req.ifr_name,"en0",4);
 if(ioctl(fd,ND_BIOCSETIF,&req)<0)return nd_bpf_fail_close(fd,phase,"bind-en0",errno);
 memset(&req,0,sizeof req);
 if(ioctl(fd,ND_BIOCGETIF,&req)<0)return nd_bpf_fail_close(fd,phase,"verify-interface",errno);
 if(memcmp(req.ifr_name,"en0",4)||if_nametoindex("en0")!=index)return nd_bpf_fail_close(fd,phase,"verify-interface",ENODEV);
 if(ioctl(fd,ND_BIOCGDLT,&dlt)<0)return nd_bpf_fail_close(fd,phase,"datalink",errno);
 if(dlt!=1)return nd_bpf_fail_close(fd,phase,"not-ethernet",EPROTONOSUPPORT);
 if(ioctl(fd,ND_BIOCSHDRCMPLT,&one)<0)return nd_bpf_fail_close(fd,phase,"header-complete",errno);
 if(ioctl(fd,ND_BIOCIMMEDIATE,&one)<0)return nd_bpf_fail_close(fd,phase,"immediate",errno);
 if(ioctl(fd,ND_BIOCSSEESENT,&zero)<0)return nd_bpf_fail_close(fd,phase,"incoming-only",errno);
 if(ioctl(fd,ND_BIOCGBLEN,&size)<0)return nd_bpf_fail_close(fd,phase,"read-buffer-size",errno);
 if(!size||size>65536)return nd_bpf_fail_close(fd,phase,"read-buffer-budget",EMSGSIZE);
 uint8_t *buf=malloc(size);if(!buf)return nd_bpf_fail_close(fd,phase,"read-buffer",ENOMEM);
 p->fd=fd;p->buf=buf;p->capacity=size;p->at=p->have=0;return 0;
}
static void nd_bpf_close_native(NdBpfPort *p) {
 int saved=errno;if(p->fd>=0)close(p->fd);free(p->buf);p->fd=-1;p->buf=NULL;p->capacity=0;p->at=p->have=0;errno=saved;
}
#endif

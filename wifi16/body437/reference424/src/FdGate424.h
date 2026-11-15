/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Bounded own-process descriptor snapshot; no fd identities logged.
 * PROC_PIDLISTFDS is actual native XNU10063 flavor1, 8-byte entries.
 */
#ifndef FD_GATE424_H
#define FD_GATE424_H
#include <stddef.h>
#include <stdint.h>
#define NW424_FD_CAP 128
struct nw424_fdentry { int32_t fd;uint32_t type; };
_Static_assert(sizeof(struct nw424_fdentry)==8,"actual XNU proc_fdinfo size");
_Static_assert(offsetof(struct nw424_fdentry,type)==4,"actual XNU proc_fdinfo type offset");
struct nw424_fdops {void *context;int(*list)(void *,struct nw424_fdentry *,size_t);int(*close_fd)(void *,int);int(*error)(void *);};
struct nw424_fdresult {unsigned stage;int count,error,closed;};
static int nw424_snapshot(const struct nw424_fdops *op,struct nw424_fdentry *out,struct nw424_fdresult *r) {
 int bytes=op->list(op->context,out,NW424_FD_CAP);
 if(bytes<=0){r->error=op->error(op->context);return -1;}
 if(bytes%(int)sizeof(*out)||bytes>=(int)(NW424_FD_CAP*sizeof(*out)))return -1;
 r->count=bytes/(int)sizeof(*out);
 for(int i=0;i<r->count;i++) {
  if(out[i].fd<0)return -1;
  for(int j=0;j<i;j++)if(out[j].fd==out[i].fd)return -1;
 }
 return 0;
}
static int nw424_isolate_fds(const struct nw424_fdops *op,int console,struct nw424_fdresult *r) {
 *r=(struct nw424_fdresult){.stage=1,.count=-1};if(console<3)return -1;
 struct nw424_fdentry f[NW424_FD_CAP];if(nw424_snapshot(op,f,r))return -1;
 int seen=0;for(int i=0;i<r->count;i++)if(f[i].fd==console)seen++;
 if(seen!=1)return -1;
 /* Unknown SDK/inherited descriptors are not silently closed or used later. */
 for(int i=0;i<r->count;i++)if(f[i].fd!=console&&f[i].fd>2)return -1;
 r->stage=2;
 for(int i=0;i<r->count;i++)if(f[i].fd!=console){if(op->close_fd(op->context,f[i].fd)){r->error=op->error(op->context);return -1;}r->closed++;}
 r->stage=3;if(nw424_snapshot(op,f,r))return -1;
 if(r->count!=1||f[0].fd!=console)return -1;
 r->stage=4;return 0;
}
static int nw424_verify_final_fds(const struct nw424_fdops *op,int console,struct nw424_fdresult *r) {
 *r=(struct nw424_fdresult){.stage=5,.count=-1};struct nw424_fdentry f[NW424_FD_CAP];if(console<3||nw424_snapshot(op,f,r))return -1;
 if(r->count!=4)return -1;
 unsigned mask=0;int seen=0;for(int i=0;i<r->count;i++){if(f[i].fd==console)seen++;else if(f[i].fd<=2)mask|=1u<<(unsigned)f[i].fd;else return -1;}
 if(seen!=1||mask!=7)return -1;r->stage=6;return 0;
}
#endif

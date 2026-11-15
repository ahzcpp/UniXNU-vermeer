#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../src/Identity424.h"
#include "../src/FdGate424.h"
static unsigned checks;
#define C(x) do{checks++;assert(x);}while(0)
struct state {struct nw424_ids ids;char trace[8];unsigned pos;int fail,bad_ids,count,one_rc,err;uint32_t only;};
static struct nw424_ids ids(void *v){return ((struct state*)v)->ids;}
static int clear(void *v){struct state*s=v;s->trace[s->pos++]='S';if(s->fail==2){s->err=EPERM;return -1;}return 0;}
static int gid(void *v){struct state*s=v;s->trace[s->pos++]='G';if(s->fail==3){s->err=EACCES;return -1;}s->ids.rgid=s->ids.egid=501;if(s->bad_ids==3)s->ids.rgid=0;if(s->bad_ids==4)s->ids.egid=0;return 0;}
static int uid(void *v){struct state*s=v;s->trace[s->pos++]='U';if(s->fail==4){s->err=EPERM;return -1;}s->ids.ruid=s->ids.euid=501;if(s->bad_ids==1)s->ids.ruid=0;if(s->bad_ids==2)s->ids.euid=0;return 0;}
static int count(void*v){struct state*s=v;if(s->fail==6){s->err=EIO;return -1;}return s->count;}
static int one(void*v,uint32_t*out){struct state*s=v;if(s->fail==7){s->err=EFAULT;return -1;}*out=s->only;return s->one_rc;}
static int err(void*v){return ((struct state*)v)->err;}
static struct state fresh(void){return (struct state){.count=1,.one_rc=1,.only=501};}
static int run(struct state*s,struct nw424_identity*r){struct nw424_identity_ops op={s,ids,clear,gid,uid,count,one,err};return nw424_drop_identity(&op,r);}
struct fdstate {int n,fd[NW424_FD_CAP],calls,closes,err,return_bytes,fail_close,reopen,duplicates;};
static int list(void*v,struct nw424_fdentry*out,size_t cap){struct fdstate*s=v;s->calls++;assert(cap==NW424_FD_CAP);if(s->return_bytes<0){s->err=EIO;return s->return_bytes;}if(s->reopen&&s->calls==2)s->fd[s->n++]=9;for(int i=0;i<s->n;i++)out[i]=(struct nw424_fdentry){s->fd[i],0};return s->return_bytes?s->return_bytes:s->n*8;}
static int closefd(void*v,int fd){struct fdstate*s=v;s->closes++;if(s->fail_close==fd){s->err=EINTR;return -1;}for(int i=0;i<s->n;i++)if(s->fd[i]==fd){memmove(s->fd+i,s->fd+i+1,(size_t)(s->n-i-1)*sizeof(int));s->n--;return 0;}s->err=EBADF;return -1;}
static int fderr(void*v){return ((struct fdstate*)v)->err;}
static struct fdstate fdnew(void){return (struct fdstate){.n=4,.fd={0,1,2,7},.fail_close=-1};}
static int isolate(struct fdstate*s,struct nw424_fdresult*r){struct nw424_fdops op={s,list,closefd,fderr};return nw424_isolate_fds(&op,7,r);}
static int final(struct fdstate*s,struct nw424_fdresult*r){struct nw424_fdops op={s,list,closefd,fderr};return nw424_verify_final_fds(&op,7,r);}
int main(void){
 struct state s=fresh();struct nw424_identity r;C(run(&s,&r)==0);C(r.ok&&r.stage==NW424_VERIFIED);C(r.group_raw_count==1&&r.extra_groups==0);C(!strcmp(s.trace,"SGU"));
 for(int id=0;id<2;id++){s=fresh();if(id)s.ids.euid=501;else s.ids.ruid=501;C(run(&s,&r)!=0);C(r.stage==NW424_ROOT&&!r.ok&&s.pos==0);}
 for(int stage=2;stage<=4;stage++){s=fresh();s.fail=stage;C(run(&s,&r)!=0);C(r.stage==(unsigned)stage&&!r.ok&&r.error!=0);C(s.pos==(unsigned)(stage-1));if(stage==4)C(r.ids.rgid==501&&r.ids.euid==0);}
 for(int bad=1;bad<=4;bad++){s=fresh();s.bad_ids=bad;C(run(&s,&r)!=0);C(r.stage==NW424_VERIFY_IDS&&!r.ok&&r.error==0);}
 for(int n=0;n<=3;n++){s=fresh();s.count=n;int rc=run(&s,&r);C((rc==0)==(n==1));C(r.group_raw_count==n);if(n!=1)C(r.extra_groups==-1&&!r.ok);}
 for(int f=6;f<=7;f++){s=fresh();s.fail=f;C(run(&s,&r)!=0);C(r.stage==(unsigned)f&&!r.ok&&r.error!=0);}
 for(int n=0;n<=2;n++){s=fresh();s.one_rc=n;C((run(&s,&r)==0)==(n==1));}
 s=fresh();s.only=0;C(run(&s,&r)!=0);C(r.stage==NW424_GROUP_VERIFY&&r.extra_groups==-1);
 struct fdstate f=fdnew();struct nw424_fdresult fr;C(isolate(&f,&fr)==0);C(f.n==1&&f.fd[0]==7&&fr.closed==3&&fr.count==1);
 f=fdnew();f.n=1;f.fd[0]=7;C(isolate(&f,&fr)==0);C(fr.closed==0);
 f=fdnew();f.fd[f.n++]=9;C(isolate(&f,&fr)!=0);C(f.closes==0); /* Do not destroy SDK descriptors to fake a clean probe. */
 f=fdnew();f.return_bytes=-1;C(isolate(&f,&fr)!=0);C(fr.error==EIO);
 f=fdnew();f.return_bytes=9;C(isolate(&f,&fr)!=0);C(f.closes==0);
 f=fdnew();f.n=NW424_FD_CAP;for(int i=0;i<f.n;i++)f.fd[i]=i;C(isolate(&f,&fr)!=0);C(f.closes==0);
 f=fdnew();f.fd[1]=0;C(isolate(&f,&fr)!=0);C(f.closes==0);
 f=fdnew();f.fd[0]=-1;C(isolate(&f,&fr)!=0);C(f.closes==0);
 f=fdnew();f.fd[3]=8;C(isolate(&f,&fr)!=0);C(f.closes==0);
 f=fdnew();f.fail_close=1;C(isolate(&f,&fr)!=0);C(fr.stage==2&&fr.error==EINTR&&fr.closed==1);
 f=fdnew();f.reopen=1;C(isolate(&f,&fr)!=0);C(fr.stage==3);
 f=fdnew();C(final(&f,&fr)==0);C(fr.stage==6&&fr.count==4);
 f=fdnew();f.fd[f.n++]=9;C(final(&f,&fr)!=0);
 f=fdnew();f.fd[1]=7;C(final(&f,&fr)!=0);
 f=fdnew();f.n=0;C(final(&f,&fr)!=0);C(fr.count==-1);
 printf("PASS424 actual-production-helpers checks=%u callback-injection-only real_UID_changes=0 native_SDK_proven=0\n",checks);return 0;
}

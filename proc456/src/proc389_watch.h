/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed privacy-bounded groups. Only keys normalized to at most15 bytes are
 * compared; WebKit's common truncation deliberately represents ONE group. */
#ifndef PROC389_WATCH_H
#define PROC389_WATCH_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#define NS389_NAME_KEY_BYTES 15u
#define NS389_CONSOLE_ROWS 320u
/* Reserve one stdout row for the async-signal-safe alarm handler. */
#define NS389_DATA_ROWS 319u
struct ns389_watch_rule {const char *label,*key;};
static const struct ns389_watch_rule ns389_watch[]={
    {"AppStore","AppStore"},
    {"appstored","appstored"},
    {"appstorecomponen","appstorecompone"},
    {"amsaccountsd","amsaccountsd"},
    {"amsengagementd","amsengagementd"},
    {"itunesstored","itunesstored"},
    {"storekitd","storekitd"},
    {"StoreKitUIService","StoreKitUIServi"},
    {"itunescloudd","itunescloudd"},
    {"akd","akd"},
    {"adid","adid"},
    {"fairplayd.H2","fairplayd.H2"},
    {"accountsd","accountsd"},
    {"nsurlsessiond","nsurlsessiond"},
    {"trustd","trustd"},
    {"mDNSResponder","mDNSResponder"},
    {"CommCenter","CommCenter"},
    {"symptomsd","symptomsd"},
    {"networkserviceproxy","networkservicep"},
    {"nesessionmanager","nesessionmanage"},
    {"nehelper","nehelper"},
    {"apsd","apsd"},
    {"mobileactivationd","mobileactivatio"},
    {"configd","configd"},
    {"cfprefsd","cfprefsd"},
    {"SpringBoard","SpringBoard"},
    {"MobileSafari","MobileSafari"},
    {"WebKit","com.apple.WebKi"},
};
#define NS389_NWATCH (sizeof ns389_watch/sizeof ns389_watch[0])
static inline int ns389_watch_index(const char *name,size_t capacity) {
    if(!name||!capacity)return -1;
    size_t n=0;while(n<capacity && name[n])n++;
    if(n==capacity||!n)return -1;
    if(n>NS389_NAME_KEY_BYTES)n=NS389_NAME_KEY_BYTES;
    int found=-1;
    for(size_t i=0;i<NS389_NWATCH;i++){
        size_t k=strlen(ns389_watch[i].key);
        if(k==n && !memcmp(name,ns389_watch[i].key,n)){
            if(found>=0)return -1; /* fail closed if future keys collide */
            found=(int)i;
        }
    }
    return found;
}
struct ns389_pstat {int pid;uint64_t cpu_abs,msg_sent,msg_recv;int seen;unsigned instances;};
static inline void ns389_note_instance(struct ns389_pstat *s,int pid){
    if(!s->instances){s->pid=pid;s->instances=1;return;}
    s->instances++;s->pid=0;s->seen=0;
}
struct ns389_delta {int same_single_pid,cpu_known,sent_known,recv_known;uint64_t cpu_abs,msg_sent,msg_recv;};
static inline struct ns389_delta ns389_delta_for(const struct ns389_pstat *a,const struct ns389_pstat *b){
    struct ns389_delta d={0};
    d.same_single_pid=a->instances==1 && b->instances==1 && a->pid>0 && a->pid==b->pid && a->seen && b->seen;
    if(!d.same_single_pid||!a->seen||!b->seen)return d;
    if(b->cpu_abs>=a->cpu_abs){d.cpu_known=1;d.cpu_abs=b->cpu_abs-a->cpu_abs;}
    if(b->msg_sent>=a->msg_sent){d.sent_known=1;d.msg_sent=b->msg_sent-a->msg_sent;}
    if(b->msg_recv>=a->msg_recv){d.recv_known=1;d.msg_recv=b->msg_recv-a->msg_recv;}
    return d;
}
static inline int ns389_row_admit(unsigned *used,unsigned limit){
    if(*used>=limit)return 0;(*used)++;return 1;
}
#endif

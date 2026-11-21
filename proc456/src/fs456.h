/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Metadata-only fixed-category sampling. No names/paths/content emitted.
 * Directory sums are incomplete entry observations, not APFS allocation usage.
 */
#ifndef FS456_H
#define FS456_H
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif
#define FS456_ENTRIES 256u
#define FS456_DEPTH 4u
#define FS456_MS 100
struct fs456_counts {
    unsigned entries,files,dirs,links,hardlinks,other,cross,errors,cap,depth,time,overflow;
    int last_errno;
    uint64_t apparent,allocated;
    dev_t device;
    long long deadline;
};
static long long fs456_ms(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int fs456_bound(struct fs456_counts *c) {
    if(c->entries>=FS456_ENTRIES){c->cap=1;return 1;}
    long long now=fs456_ms();
    if(now<0 || now>=c->deadline){c->time=1;return 1;}
    return 0;
}
static void fs456_error(struct fs456_counts *c,int e){c->errors++;c->last_errno=e;}
static void fs456_add(struct fs456_counts *c,const struct stat *s) {
    if(s->st_size<0 || s->st_blocks<0 || (uint64_t)s->st_blocks>UINT64_MAX/512 ||
       (uint64_t)s->st_size>UINT64_MAX-c->apparent || (uint64_t)s->st_blocks*512>UINT64_MAX-c->allocated){c->overflow++;return;}
    c->apparent+=(uint64_t)s->st_size;c->allocated+=(uint64_t)s->st_blocks*512;
}
/* Takes ownership of fd, including every error/limit return. */
static void fs456_walk(int fd,unsigned depth,struct fs456_counts *c) {
    DIR *dir=fdopendir(fd);if(!dir){int e=errno;close(fd);fs456_error(c,e);return;}
    while(!fs456_bound(c)) {
        errno=0;struct dirent *entry=readdir(dir);
        if(!entry){if(errno)fs456_error(c,errno);break;}
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        c->entries++;
        struct stat st;errno=0;
        if(fstatat(fd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)){fs456_error(c,errno);continue;}
        if(st.st_dev!=c->device){c->cross++;continue;}
        if(S_ISLNK(st.st_mode)){c->links++;continue;}
        if(S_ISREG(st.st_mode)) {
            c->files++;if(st.st_nlink>1){c->hardlinks++;continue;}fs456_add(c,&st);
        } else if(S_ISDIR(st.st_mode)) {
            c->dirs++;fs456_add(c,&st);
            if(depth>=FS456_DEPTH){c->depth++;continue;}
            if(fs456_bound(c))break;
            int child=openat(fd,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(child<0){fs456_error(c,errno);continue;}
            struct stat actual;
            if(fstat(child,&actual)){int e=errno;close(child);fs456_error(c,e);continue;}
            if(actual.st_dev!=c->device || actual.st_ino!=st.st_ino){close(child);fs456_error(c,ESTALE);continue;}
            fs456_walk(child,depth+1,c);
        } else c->other++;
    }
    if(closedir(dir))fs456_error(c,errno);
}
static void fs456_inventory(int n) {
    static const char *const category[]={"mobile","folders","db","root","tmp","MobileAsset"};
    int root=open("/private/var",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int error=root<0?errno:0;struct stat base;memset(&base,0,sizeof base);
    if(root>=0 && fstat(root,&base)){error=errno;close(root);root=-1;}
    for(unsigned k=0;k<sizeof category/sizeof category[0];k++) {
        struct fs456_counts c={0};c.device=base.st_dev;
        long long begin=fs456_ms();c.deadline=begin>=0?begin+FS456_MS:0;
        if(root<0)fs456_error(&c,error);
        else {
            int fd=openat(root,category[k],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(fd<0)fs456_error(&c,errno);
            else {
                struct stat st;
                if(fstat(fd,&st)){int e=errno;close(fd);fs456_error(&c,e);}
                else if(st.st_dev!=base.st_dev){close(fd);c.cross++;}
                else fs456_walk(fd,0,&c);
            }
        }
        long long end=fs456_ms();
        emit(1,"fs456_cat n=%d cat=%u entries=%u files=%u dirs=%u apparent_b=%llu entry_alloc_b=%llu symlink_skip=%u hardlink_skip=%u other_skip=%u cross_skip=%u errors=%u errno=%d entry_cap=%u depth_cap=%u time_cap=%u overflow=%u elapsed_ms=%lld census=0 coherent=0",
             n,k+1,c.entries,c.files,c.dirs,(unsigned long long)c.apparent,(unsigned long long)c.allocated,c.links,c.hardlinks,c.other,c.cross,c.errors,c.last_errno,c.cap,c.depth,c.time,c.overflow,end>=begin && begin>=0?end-begin:-1);
    }
    if(root>=0)close(root);
}
static void fs456_snapshot(int n,long long origin) {
    struct statfs fs;memset(&fs,0,sizeof fs);long long begin=fs456_ms();errno=0;
    int rc=statfs("/private/var",&fs),error=errno;long long end=fs456_ms();
    int known=rc==0 && fs.f_bsize>0;
    int apfs=0;
#ifdef __APPLE__
    if(known)apfs=!strncmp(fs.f_fstypename,"apfs",sizeof fs.f_fstypename);
#endif
    emit(1,"fs456 n=%d known=%d rc=%d errno=%d apfs=%d block_b=%llu blocks=%llu free_blocks=%llu avail_blocks=%llu files=%llu free_files=%llu flags=%llu begin_ms=%lld end_ms=%lld coherent=0",
         n,known,rc,error,apfs,(unsigned long long)(known?fs.f_bsize:0),(unsigned long long)(known?fs.f_blocks:0),(unsigned long long)(known?fs.f_bfree:0),(unsigned long long)(known?fs.f_bavail:0),(unsigned long long)(known?fs.f_files:0),(unsigned long long)(known?fs.f_ffree:0),(unsigned long long)(known?fs.f_flags:0),begin>=origin && origin>=0?begin-origin:-1,end>=origin && origin>=0?end-origin:-1);
    if(n==0 || n==15){ns351_memory_snapshot(n,memory_emit_all);fs456_inventory(n);}
}
#endif

#define NS450_BSDINFO 3
#define NS450_PATH_BYTES 4096u
#define NS450_SAMPLES 16
#define NS450_INTERVAL 2
#define NS450_PIDS 8
#define NS450_HISTORY (NS450_SAMPLES * NS450_PIDS)
struct ns450_bsdinfo {
    uint32_t flags, status, xstatus, pid, ppid, uid, gid, ruid, rgid;
    uint32_t svuid, svgid, rfu;
    char comm[16], name[32];
    uint32_t nfiles, pgid, pjobc, e_tdev, e_tpgid;
    int32_t nice;
    uint64_t start_sec, start_usec;
};
_Static_assert(sizeof(struct ns450_bsdinfo)==136, "BSDINFO size");
_Static_assert(offsetof(struct ns450_bsdinfo,start_sec)==120, "BSDINFO start");

/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * unixnu-https346: own, read-only HTTPS reachability probe for the K70 iOS 16.7 port.
 *
 * Separates "system DNS works" from "TLS / system networking policy works" from "an app's own problem":
 *   1. wall clock (TLS certificate validity depends on it),
 *   2. getaddrinfo() through the system resolver for two fixed public Apple hosts (rc + address counts only),
 *   3. NSURLSession GET of two fixed public Apple HTTPS URLs with the system's normal certificate validation
 *      (no challenge handler, no trust override), ephemeral session, no cookies, no cache, no credentials,
 *      no background session, 10 s per request.
 * Prints only: clock, resolver rc / counts, HTTP status, byte count, whether the Apple "Success" marker is present,
 * NSError domain/code (+ underlying and CFStream codes), and per-phase timings/protocol from task metrics.
 * Never prints response bodies, headers, cookies, tokens, URLs from the network, addresses or local identity.
 * The whole process is bounded by alarm(60). Console mirror (-c) is optional, non-blocking, bounded.
 * Changes nothing on the system.
 */
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TAG "HTTPS346"
#define PROBE_VERSION "346.1"
#define LINE_MAX_BYTES 480
#define CONSOLE_MAX_ROWS 64
#define REQUEST_TIMEOUT_S 10
#define DNS_TIMEOUT_MS 10000
#define PROCESS_ALARM_S 60

static int g_console_fd = -1;
static atomic_uint g_console_rows;

/* Optional mirror to /dev/console: non-blocking, close-on-exec, must be a character device; one write attempt per
 * line, at most CONSOLE_MAX_ROWS lines. Failure is silent and never affects stdout or the result. */
static void console_enable(void) {
    int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    struct stat st;
    int flags = fcntl(fd, F_GETFL);
    if (fstat(fd, &st) != 0 || !S_ISCHR(st.st_mode) || flags < 0 || !(flags & O_NONBLOCK)) { close(fd); return; }
    g_console_fd = fd;
}

static void out_raw(const char *buf, size_t n) {
    ssize_t w = write(STDOUT_FILENO, buf, n);
    (void)w;
    if (g_console_fd >= 0 && atomic_fetch_add(&g_console_rows, 1) < CONSOLE_MAX_ROWS) {
        w = write(g_console_fd, buf, n);
        (void)w;
    }
}

static void emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void emit(const char *fmt, ...) {
    char buf[LINE_MAX_BYTES];
    int p = snprintf(buf, sizeof buf, TAG " ");
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + p, sizeof buf - (size_t)p - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t len = (size_t)p + (size_t)n;
    if (len > sizeof buf - 2) len = sizeof buf - 2;
    buf[len++] = '\n';
    out_raw(buf, len);
}

static void on_alarm(int sig) {
    (void)sig;
    static const char msg[] = TAG " RESULT result=fail step=alarm60 (process time limit reached)\n";
    ssize_t w = write(STDOUT_FILENO, msg, sizeof msg - 1);
    (void)w;
    if (g_console_fd >= 0) { w = write(g_console_fd, msg, sizeof msg - 1); (void)w; }
    _exit(124);
}

/* Copies a short, printable token (error domains, protocol names); anything else becomes '?'. */
static void safe_token(NSString *s, char *dst, size_t cap) {
    if (cap == 0) return;
    dst[0] = 0;
    if (!s) { snprintf(dst, cap, "-"); return; }
    const char *u = s.UTF8String;
    if (!u) { snprintf(dst, cap, "?"); return; }
    size_t i = 0;
    for (; u[i] && i + 1 < cap && i < 64; i++) {
        unsigned char c = (unsigned char)u[i];
        dst[i] = (c > 32 && c < 127 && c != '"' && c != '\\') ? (char)c : '?';
    }
    dst[i] = 0;
}

static long long mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void emit_clock(const char *when) {
    struct timespec rt;
    clock_gettime(CLOCK_REALTIME, &rt);
    struct tm tm;
    time_t t = rt.tv_sec;
    gmtime_r(&t, &tm);
    char iso[40];
    strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", &tm);
    /* 1735689600 = 2025-01-01: a clock earlier than that cannot validate today's Apple certificates. */
    emit("clock when=%s utc=%s epoch=%lld mono_ms=%lld plausible=%s", when, iso, (long long)rt.tv_sec, mono_ms(),
         rt.tv_sec >= 1735689600 ? "yes" : "no");
}

static const char *eai_name(int rc) {
    switch (rc) {
    case 0: return "ok";
    case EAI_AGAIN: return "EAI_AGAIN";
    case EAI_BADFLAGS: return "EAI_BADFLAGS";
    case EAI_FAIL: return "EAI_FAIL";
    case EAI_FAMILY: return "EAI_FAMILY";
    case EAI_MEMORY: return "EAI_MEMORY";
    case EAI_NONAME: return "EAI_NONAME";
    case EAI_SERVICE: return "EAI_SERVICE";
    case EAI_SOCKTYPE: return "EAI_SOCKTYPE";
    case EAI_SYSTEM: return "EAI_SYSTEM";
#ifdef EAI_NODATA
#if EAI_NODATA != EAI_NONAME
    case EAI_NODATA: return "EAI_NODATA";
#endif
#endif
    default: return "other";
    }
}

/* getaddrinfo runs in its own thread so a stuck resolver cannot consume the whole budget; on timeout the job is
 * intentionally leaked (the thread may still write into it) and the process exits soon after anyway. */
struct gai_job {
    char host[64];
    int rc, err, total, v4, v6;
    long long ms;
    atomic_int done;
};

static void *gai_thread(void *arg) {
    struct gai_job *j = arg;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    long long t0 = mono_ms();
    errno = 0;
    int rc = getaddrinfo(j->host, "443", &hints, &res);
    j->err = errno;
    j->ms = mono_ms() - t0;
    if (rc == 0) {
        for (struct addrinfo *a = res; a; a = a->ai_next) {
            j->total++;
            if (a->ai_family == AF_INET) j->v4++;
            else if (a->ai_family == AF_INET6) j->v6++;
        }
        freeaddrinfo(res);
    }
    j->rc = rc;
    atomic_store(&j->done, 1);
    return NULL;
}

static int probe_dns(const char *host) {
    struct gai_job *j = calloc(1, sizeof *j);
    if (!j) { emit("dns host=%s rc=-1 err=ENOMEM", host); return 0; }
    snprintf(j->host, sizeof j->host, "%s", host);
    pthread_t th;
    if (pthread_create(&th, NULL, gai_thread, j) != 0) { emit("dns host=%s rc=-1 err=pthread_create", host); free(j); return 0; }
    pthread_detach(th);
    long long deadline = mono_ms() + DNS_TIMEOUT_MS;
    while (!atomic_load(&j->done) && mono_ms() < deadline) {
        struct timespec ts = {0, 20 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    if (!atomic_load(&j->done)) {
        emit("dns host=%s rc=timeout limit_ms=%d", host, DNS_TIMEOUT_MS);
        return 0; /* job leaked on purpose */
    }
    if (j->rc == 0)
        emit("dns host=%s rc=0 name=ok total=%d v4=%d v6=%d ms=%lld", host, j->total, j->v4, j->v6, j->ms);
    else if (j->rc == EAI_SYSTEM)
        emit("dns host=%s rc=%d name=%s errno=%d ms=%lld", host, j->rc, eai_name(j->rc), j->err, j->ms);
    else
        emit("dns host=%s rc=%d name=%s ms=%lld", host, j->rc, eai_name(j->rc), j->ms);
    int ok = j->rc == 0 && j->total > 0;
    free(j);
    return ok;
}

@interface Probe346Delegate : NSObject <NSURLSessionTaskDelegate>
@property(atomic, strong) NSURLSessionTaskMetrics *metrics;
@property(atomic, assign) int redirects;
@property(nonatomic, strong) dispatch_semaphore_t metricsDone;
@end

@implementation Probe346Delegate
/* Deliberately NO -URLSession:didReceiveChallenge:… / task:didReceiveChallenge:… : the system performs its normal
 * server-trust evaluation. Redirects are followed normally and only counted. */
- (void)URLSession:(NSURLSession *)session
                          task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completionHandler {
    (void)session; (void)task; (void)response;
    self.redirects = self.redirects + 1;
    completionHandler(request);
}
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)metrics {
    (void)session; (void)task;
    self.metrics = metrics;
    dispatch_semaphore_signal(self.metricsDone);
}
@end

static long long span_ms(NSDate *a, NSDate *b) {
    if (!a || !b) return -1;
    return (long long)([b timeIntervalSinceDate:a] * 1000.0);
}

static void emit_metrics(const char *name, NSURLSessionTaskMetrics *m) {
    if (!m) { emit("metrics name=%s none", name); return; }
    NSArray<NSURLSessionTaskTransactionMetrics *> *tx = m.transactionMetrics;
    emit("metrics name=%s transactions=%lu redirects=%lu total_ms=%lld", name, (unsigned long)tx.count,
         (unsigned long)m.redirectCount, (long long)(m.taskInterval.duration * 1000.0));
    NSUInteger i = 0;
    for (NSURLSessionTaskTransactionMetrics *t in tx) {
        if (i >= 4) break;
        char proto[40];
        safe_token(t.networkProtocolName, proto, sizeof proto);
        NSNumber *tlsv = t.negotiatedTLSProtocolVersion, *cs = t.negotiatedTLSCipherSuite;
        emit("metrics name=%s tx=%lu fetch=%ld proto=%s reused=%d proxy=%d dns_ms=%lld connect_ms=%lld tls_ms=%lld "
             "ttfb_ms=%lld tls_version=0x%04x cipher=0x%04x",
             name, (unsigned long)i, (long)t.resourceFetchType, proto, t.reusedConnection ? 1 : 0, t.proxyConnection ? 1 : 0,
             span_ms(t.domainLookupStartDate, t.domainLookupEndDate), span_ms(t.connectStartDate, t.connectEndDate),
             span_ms(t.secureConnectionStartDate, t.secureConnectionEndDate), span_ms(t.requestStartDate, t.responseStartDate),
             tlsv ? tlsv.unsignedIntValue : 0u, cs ? cs.unsignedIntValue : 0u);
        i++;
    }
}

static void emit_error(const char *name, NSError *e) {
    char dom[72], udom[72];
    safe_token(e.domain, dom, sizeof dom);
    NSError *u = e.userInfo[NSUnderlyingErrorKey];
    if (![u isKindOfClass:[NSError class]]) u = nil;
    safe_token(u ? u.domain : nil, udom, sizeof udom);
    id sd = e.userInfo[@"_kCFStreamErrorDomainKey"], sc = e.userInfo[@"_kCFStreamErrorCodeKey"];
    long sdv = [sd isKindOfClass:[NSNumber class]] ? [sd longValue] : 0;
    long scv = [sc isKindOfClass:[NSNumber class]] ? [sc longValue] : 0;
    int trust = e.userInfo[NSURLErrorFailingURLPeerTrustErrorKey] != nil;
    emit("error name=%s domain=%s code=%ld underlying_domain=%s underlying_code=%ld cfstream_domain=%ld cfstream_code=%ld "
         "peer_trust_present=%d",
         name, dom, (long)e.code, udom, u ? (long)u.code : 0L, sdv, scv, trust);
}

static int probe_https(const char *name, NSString *urlString) {
    NSURLSessionConfiguration *c = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    c.HTTPCookieStorage = nil;
    c.HTTPShouldSetCookies = NO;
    c.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
    c.URLCache = nil;
    c.URLCredentialStorage = nil;
    c.requestCachePolicy = NSURLRequestReloadIgnoringLocalAndRemoteCacheData;
    c.timeoutIntervalForRequest = REQUEST_TIMEOUT_S;
    c.timeoutIntervalForResource = REQUEST_TIMEOUT_S;
    c.waitsForConnectivity = NO;
    c.HTTPMaximumConnectionsPerHost = 1;

    Probe346Delegate *d = [Probe346Delegate new];
    d.metricsDone = dispatch_semaphore_create(0);
    NSOperationQueue *q = [NSOperationQueue new];
    q.maxConcurrentOperationCount = 1;
    NSURLSession *s = [NSURLSession sessionWithConfiguration:c delegate:d delegateQueue:q];

    NSMutableURLRequest *r = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:urlString]
                                                     cachePolicy:NSURLRequestReloadIgnoringLocalAndRemoteCacheData
                                                 timeoutInterval:REQUEST_TIMEOUT_S];
    r.HTTPShouldHandleCookies = NO;

    __block long status = -1;
    __block unsigned long long bytes = 0;
    __block int marker = 0;
    __block NSError *err = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    long long t0 = mono_ms();
    NSURLSessionDataTask *task = [s dataTaskWithRequest:r
                                      completionHandler:^(NSData *data, NSURLResponse *resp, NSError *error) {
                                          if ([resp isKindOfClass:[NSHTTPURLResponse class]]) status = ((NSHTTPURLResponse *)resp).statusCode;
                                          bytes = data.length;
                                          /* Apple's two detection pages contain the word "Success"; only a yes/no leaves
                                           * the process, never the body. */
                                          if (data.length > 0 && data.length <= 65536) {
                                              NSData *needle = [@"Success" dataUsingEncoding:NSASCIIStringEncoding];
                                              marker = [data rangeOfData:needle options:0 range:NSMakeRange(0, data.length)].location != NSNotFound;
                                          }
                                          err = error;
                                          dispatch_semaphore_signal(done);
                                      }];
    [task resume];
    long waited = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(REQUEST_TIMEOUT_S + 2) * NSEC_PER_SEC));
    long long ms = mono_ms() - t0;
    if (waited != 0) {
        [task cancel];
        emit("https name=%s result=fail reason=wait_timeout ms=%lld", name, ms);
        [s invalidateAndCancel];
        return 0;
    }
    (void)dispatch_semaphore_wait(d.metricsDone, dispatch_time(DISPATCH_TIME_NOW, (int64_t)NSEC_PER_SEC));
    int ok = err == nil && status == 200 && marker;
    emit("https name=%s result=%s status=%ld bytes=%llu success_marker=%s redirects=%d ms=%lld", name, ok ? "pass" : "fail",
         status, bytes, marker ? "yes" : "no", d.redirects, ms);
    if (err) emit_error(name, err);
    emit_metrics(name, d.metrics);
    [s invalidateAndCancel];
    return ok;
}

static void usage(void) {
    static const char u[] = "usage: unixnu-https346 [-c]\n  -c  also mirror lines to /dev/console (non-blocking, bounded)\n";
    ssize_t w = write(STDERR_FILENO, u, sizeof u - 1);
    (void)w;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-c")) console_enable();
            else { usage(); return 2; }
        }
        signal(SIGPIPE, SIG_IGN);
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_alarm;
        sigaction(SIGALRM, &sa, NULL);
        alarm(PROCESS_ALARM_S);

        emit("start version=%s pid=%d request_timeout_s=%d dns_timeout_ms=%d alarm_s=%d console=%s", PROBE_VERSION, (int)getpid(),
             REQUEST_TIMEOUT_S, DNS_TIMEOUT_MS, PROCESS_ALARM_S, g_console_fd >= 0 ? "on" : "off");
        emit_clock("start");
        int dns1 = probe_dns("captive.apple.com");
        int dns2 = probe_dns("www.apple.com");
        int h1 = probe_https("captive", @"https://captive.apple.com/hotspot-detect.html");
        int h2 = probe_https("apple_success", @"https://www.apple.com/library/test/success.html");
        emit_clock("end");
        const char *step = !dns1 || !dns2 ? "dns" : (!h1 || !h2 ? "https" : "none");
        emit("RESULT result=%s step=%s dns_captive=%d dns_www=%d https_captive=%d https_success=%d", (dns1 && dns2 && h1 && h2) ? "pass" : "fail",
             step, dns1, dns2, h1, h2);
        alarm(0);
        return (dns1 && dns2 && h1 && h2) ? 0 : 1;
    }
}

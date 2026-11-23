// SPDX-License-Identifier: BSD-2-Clause-Patent
// unixnu-sysapps475 (v478): register the original 20H392 deletable system apps that kernel478 copies from the OS volume's
// staged_system_apps into /private/var/k70-system-apps and exposes read-only over /Applications on every boot.
// LaunchServices only learns about /Applications entries on a database rebuild, so the apps stay invisible to
// SpringBoard until registered. For each <name>.app in the copy whose /Applications view exists and whose bundle id
// is not yet known to LaunchServices: -[LSApplicationWorkspace registerApplication:], then, if still unknown,
// -registerApplicationDictionary: with a system-app record. Runs as mobile (501) after dropping root.
// No file writes, no deletion, no network, no signals. Rows are prefixed "SYSAPPS475 ".
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define VERSION "478.0"
static int g_console = -1;

static void row(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void row(const char *fmt, ...)
{
	char b[900];
	int o = snprintf(b, sizeof b, "SYSAPPS475 ");
	va_list ap; va_start(ap, fmt); int n = vsnprintf(b + o, sizeof b - (size_t)o - 2, fmt, ap); va_end(ap);
	if (o < 0 || n < 0) return;
	size_t len = (size_t)o + (size_t)n; if (len > sizeof b - 2) len = sizeof b - 2;
	b[len++] = '\n';
	ssize_t r = write(STDOUT_FILENO, b, len); (void)r;
	if (g_console >= 0) { r = write(g_console, b, len); (void)r; }
}

static BOOL installed(id ws, NSString *bid)
{
	SEL s = NSSelectorFromString(@"applicationIsInstalled:");
	if (![ws respondsToSelector:s]) return NO;
	return ((BOOL (*)(id, SEL, id))objc_msgSend)(ws, s, bid);
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	struct stat st;
	if (fd >= 0 && !fstat(fd, &st) && S_ISCHR(st.st_mode)) g_console = fd; else if (fd >= 0) close(fd);
	row("start version=%s uid=%u euid=%u", VERSION, getuid(), geteuid());
	if (geteuid() == 0) {
		struct stat ms; int have_marker = lstat("/private/var/db/.sheng-staged-apps", &ms) == 0;
		row("root_check kernel_marker=%d", have_marker);
		if (setgid(501) || setuid(501)) { row("result=error step=drop_privileges errno=%d", errno); return 2; }
	}
	setenv("HOME", "/var/mobile", 1); setenv("USER", "mobile", 1); setenv("LOGNAME", "mobile", 1);
	row("identity uid=%u euid=%u gid=%u", getuid(), geteuid(), getgid());
	sleep(60);
	@autoreleasepool {
		void *cs = dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_NOW);
		void *mcs = dlopen("/System/Library/Frameworks/MobileCoreServices.framework/MobileCoreServices", RTLD_NOW);
		Class wsc = NSClassFromString(@"LSApplicationWorkspace");
		id ws = wsc ? ((id (*)(id, SEL))objc_msgSend)(wsc, NSSelectorFromString(@"defaultWorkspace")) : nil;
		row("frameworks coreservices=%d mobilecoreservices=%d workspace=%d", cs != NULL, mcs != NULL, ws != nil);
		if (!ws) { row("result=error step=workspace"); return 3; }
		NSFileManager *fm = [NSFileManager defaultManager];
		NSError *lerr = nil;
		NSArray *copied = [fm contentsOfDirectoryAtPath:@"/private/var/k70-system-apps" error:&lerr];
		row("copy count=%lu error=%ld", (unsigned long)copied.count, (long)lerr.code);
		SEL regURL = NSSelectorFromString(@"registerApplication:");
		SEL regDict = NSSelectorFromString(@"registerApplicationDictionary:");
		unsigned total = 0, already = 0, now = 0, failed = 0, missing = 0;
		for (NSString *name in [copied sortedArrayUsingSelector:@selector(compare:)]) {
			if (![name hasSuffix:@".app"]) continue;
			total++;
			NSString *app = [@"/Applications" stringByAppendingPathComponent:name];
			NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:[app stringByAppendingPathComponent:@"Info.plist"]];
			NSString *bid = info[@"CFBundleIdentifier"];
			if (![bid isKindOfClass:[NSString class]]) { missing++; row("app name=%s view=missing", name.UTF8String); continue; }
			if (installed(ws, bid)) { already++; row("app name=%s id=%s state=known", name.UTF8String, bid.UTF8String); continue; }
			BOOL r1 = NO, r2 = NO;
			if ([ws respondsToSelector:regURL])
				r1 = ((BOOL (*)(id, SEL, id))objc_msgSend)(ws, regURL, [NSURL fileURLWithPath:app isDirectory:YES]);
			BOOL ok = installed(ws, bid);
			if (!ok && [ws respondsToSelector:regDict]) {
				NSDictionary *d = @{@"ApplicationType": @"System", @"CFBundleIdentifier": bid, @"CodeInfoIdentifier": bid,
				                    @"Path": app, @"IsDeletable": @NO, @"IsContainerized": @NO, @"SignerOrganization": @"Apple Inc.",
				                    @"CompatibilityState": @0, @"_LSBundlePlugins": @{}};
				r2 = ((BOOL (*)(id, SEL, id))objc_msgSend)(ws, regDict, d);
				ok = installed(ws, bid);
			}
			if (ok) now++; else failed++;
			row("app name=%s id=%s register_url=%d register_dict=%d known_after=%d", name.UTF8String, bid.UTF8String, r1, r2, ok);
		}
		row("result=%s total=%u already=%u registered=%u failed=%u view_missing=%u", failed || missing ? "partial" : "done",
		    total, already, now, failed, missing);
	}
	return 0;
}

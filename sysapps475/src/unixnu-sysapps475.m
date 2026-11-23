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

#define VERSION "479.0"
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
	int was_root = geteuid() == 0;
	if (was_root) {
		struct stat ms; int have_marker = lstat("/private/var/db/.sheng-staged-apps", &ms) == 0;
		row("root_check kernel_marker=%d", have_marker);
		// v479: effective ids only, so the saved root id allows one root-side rebuild request later.
		if (setegid(501) || seteuid(501)) { row("result=error step=drop_privileges errno=%d", errno); return 2; }
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
		NSMutableArray *ids = [NSMutableArray array];
		unsigned missing = 0;
		for (NSString *name in [copied sortedArrayUsingSelector:@selector(compare:)]) {
			if (![name hasSuffix:@".app"]) continue;
			NSString *app = [@"/Applications" stringByAppendingPathComponent:name];
			NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:[app stringByAppendingPathComponent:@"Info.plist"]];
			NSString *bid = info[@"CFBundleIdentifier"];
			if ([bid isKindOfClass:[NSString class]]) [ids addObject:bid]; else { missing++; row("app name=%s view=missing", name.UTF8String); }
		}
		unsigned (^count_unknown)(const char *) = ^unsigned(const char *phase) {
			unsigned unknown = 0; NSMutableString *list = [NSMutableString string];
			for (NSString *bid in ids) if (!installed(ws, bid)) { unknown++; [list appendFormat:@"%@ ", bid]; }
			row("check phase=%s total=%lu unknown=%u", phase, (unsigned long)ids.count, unknown);
			if (unknown) row("unknown phase=%s %.700s", phase, list.UTF8String);
			return unknown;
		};
		SEL needs = NSSelectorFromString(@"_LSPrivateDatabaseNeedsRebuild");
		if ([ws respondsToSelector:needs]) row("database_needs_rebuild=%d", ((BOOL (*)(id, SEL))objc_msgSend)(ws, needs));
		unsigned unknown = count_unknown("before");
		SEL rb = NSSelectorFromString(@"_LSPrivateRebuildApplicationDatabasesForSystemApps:internal:user:");
		SEL rbu = NSSelectorFromString(@"_LSPrivateRebuildApplicationDatabasesForSystemApps:internal:user:uid:");
		if (unknown && [ws respondsToSelector:rb]) {
			NSDate *t0 = [NSDate date];
			BOOL r = ((BOOL (*)(id, SEL, BOOL, BOOL, BOOL))objc_msgSend)(ws, rb, YES, YES, NO);
			row("rebuild as=mobile system=1 internal=1 user=0 result=%d seconds=%.1f", r, -[t0 timeIntervalSinceNow]);
			sleep(5);
			unknown = count_unknown("after_mobile_rebuild");
		}
		if (unknown && was_root && [ws respondsToSelector:rbu]) {
			if (seteuid(0) == 0) {
				NSDate *t0 = [NSDate date];
				BOOL r = ((BOOL (*)(id, SEL, BOOL, BOOL, BOOL, unsigned))objc_msgSend)(ws, rbu, YES, YES, NO, 501u);
				row("rebuild as=root uid_arg=501 result=%d seconds=%.1f", r, -[t0 timeIntervalSinceNow]);
				if (seteuid(501)) { row("result=error step=redrop errno=%d", errno); return 4; }
				sleep(5);
				unknown = count_unknown("after_root_rebuild");
			} else row("root_rebuild skipped errno=%d", errno);
		}
		row("result=%s total=%lu unknown=%u view_missing=%u", unknown || missing ? "partial" : "done", (unsigned long)ids.count, unknown, missing);
	}
	return 0;
}
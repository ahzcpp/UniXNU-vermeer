// SPDX-License-Identifier: BSD-2-Clause-Patent
// unixnu-sysapps475: run the ORIGINAL 20H392 SystemAppMigrator.migrator once, the way the DataMigration plugin
// wrapper does (+[DataClassMigrator dataClassMigratorForBundleAtPath:], -setContext:, -performMigration), so the
// deletable system apps staged in /private/var/staged_system_apps (Notes, Calendar, Clock, Weather, ...) get
// installed. On this port they never were: k70-migration-baseline marks DataMigration as already done, so the
// plugin never ran. This tool installs nothing by itself; installd/InstallCoordination do it through Apple's code.
//
// Starts as root only to open the checked console mirror, then drops to mobile (501), the role account of
// com.apple.migrationpluginwrapper. One run per boot (LaunchOnlyOnce); after a successful migration a marker in
// mobile's Library stops later runs. Rows are prefixed "SYSAPPS475 ". No network, no writes of its own besides
// the marker; no signals, no task ports.
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

#define VERSION "476.0"
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

static void ns_rows(NSString *tag, id obj)
{
	NSString *s = [[obj description] stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
	const char *c = s.UTF8String ? s.UTF8String : "(nil)";
	size_t total = strlen(c);
	for (size_t at = 0; at < total && at < 24000; at += 700)
		row("%s part=%zu %.700s", tag.UTF8String, at / 700, c + at);
}

typedef BOOL (*MIGetStatusFn)(BOOL *, NSError **);
typedef NSDictionary *(*MILookupFn)(NSDictionary *);

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	struct stat st;
	if (fd >= 0 && !fstat(fd, &st) && S_ISCHR(st.st_mode)) g_console = fd; else if (fd >= 0) close(fd);
	row("start version=%s uid=%u euid=%u", VERSION, getuid(), geteuid());

	// v476: the Data copy of /private/var/staged_system_apps can be empty (an earlier first-boot migration consumed it)
	// while the kernel's "copied" marker still exists, so sheng-staged-apps never copies again. As root: count the staged
	// entries; if there are none and the marker exists, remove only that port-owned marker (the next boot re-copies the
	// 33 apps from the OS volume) and stop -- running the migrator with nothing staged sends it to the App Store.
	if (geteuid() == 0) {
		static const char *marker_k = "/private/var/db/.sheng-staged-apps";
		int entries = -1, derr = 0;
		NSError *lerr = nil;
		NSArray *list = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:@"/private/var/staged_system_apps" error:&lerr];
		if (list) entries = (int)list.count; else derr = (int)lerr.code;
		struct stat ms; int have_marker = lstat(marker_k, &ms) == 0 && S_ISREG(ms.st_mode);
		struct stat ds; int have_dir = lstat("/private/var/staged_system_apps", &ds) == 0;
		row("root_check staged_entries=%d list_error=%d dir=%d dir_uid=%d dir_mode=%o kernel_marker=%d", entries, derr, have_dir,
		    have_dir ? (int)ds.st_uid : -1, have_dir ? (unsigned)(ds.st_mode & 07777) : 0u, have_marker);
		if (entries <= 0) {
			if (have_marker) {
				int u = unlink(marker_k);
				row("result=restage_requested unlink=%d errno=%d (next boot copies the OS-volume staged apps)", u, u ? errno : 0);
			} else {
				row("result=nothing_staged_no_marker");
			}
			return 0;
		}
		if (setgid(501) || setuid(501)) { row("result=error step=drop_privileges errno=%d", errno); return 2; }
	}
	setenv("HOME", "/var/mobile", 1); setenv("USER", "mobile", 1); setenv("LOGNAME", "mobile", 1);
	row("identity uid=%u euid=%u gid=%u", getuid(), geteuid(), getgid());

	// Let SpringBoard, installd and installcoordinationd come up first (RunAtLoad fires early in boot).
	sleep(75);
	row("settled");
	@autoreleasepool {
		NSString *marker = @"/var/mobile/Library/Preferences/.unixnu-sysapps475-done";
		if ([[NSFileManager defaultManager] fileExistsAtPath:marker]) {
			row("result=skipped reason=marker_present");
			return 0;
		}
		void *mi = dlopen("/System/Library/PrivateFrameworks/MobileInstallation.framework/MobileInstallation", RTLD_NOW);
		void *dm = dlopen("/System/Library/PrivateFrameworks/DataMigration.framework/DataMigration", RTLD_NOW);
		row("frameworks mobileinstallation=%d datamigration=%d", mi != NULL, dm != NULL);
		if (!mi || !dm) { row("result=error step=dlopen detail=%s", dlerror()); return 3; }
		MIGetStatusFn getStatus = (MIGetStatusFn)dlsym(mi, "MobileInstallationGetSystemAppMigrationStatus");
		MILookupFn lookup = (MILookupFn)dlsym(mi, "MobileInstallationLookupSystemAppState");

		NSArray *staged = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:@"/var/staged_system_apps" error:nil];
		row("staged count=%lu", (unsigned long)staged.count);
		ns_rows(@"staged_list", staged);
		if (staged.count == 0) { row("result=skipped reason=nothing_staged"); return 0; }

		if (getStatus) {
			BOOL done = NO; NSError *err = nil; BOOL ok = getStatus(&done, &err);
			row("migration_status_before call=%d complete=%d error=%s", ok, done, err ? err.description.UTF8String : "-");
		}
		if (lookup) ns_rows(@"state_before", lookup(nil));

		Class dcm = NSClassFromString(@"DataClassMigrator");
		SEL make = NSSelectorFromString(@"dataClassMigratorForBundleAtPath:");
		if (!dcm || ![dcm respondsToSelector:make]) { row("result=error step=class"); return 4; }
		NSString *bundle = @"/System/Library/DataClassMigrators/SystemAppMigrator.migrator";
		id migrator = ((id (*)(id, SEL, id))objc_msgSend)(dcm, make, bundle);
		row("migrator class=%s", migrator ? object_getClassName(migrator) : "(nil)");
		if (!migrator) { row("result=error step=load_plugin"); return 5; }
		// Empty context: no upgrade, no restore from backup -- the first boot of an erase install.
		SEL setContext = NSSelectorFromString(@"setContext:");
		if ([migrator respondsToSelector:setContext]) ((void (*)(id, SEL, id))objc_msgSend)(migrator, setContext, @{});
		SEL setDisp = NSSelectorFromString(@"setUserDataDisposition:");
		if ([migrator respondsToSelector:setDisp]) ((void (*)(id, SEL, unsigned))objc_msgSend)(migrator, setDisp, 0u);

		NSDate *t0 = [NSDate date];
		row("perform begin");
		BOOL result = ((BOOL (*)(id, SEL))objc_msgSend)(migrator, NSSelectorFromString(@"performMigration"));
		row("perform end result=%d seconds=%.1f", result, -[t0 timeIntervalSinceNow]);

		if (getStatus) {
			BOOL done = NO; NSError *err = nil; BOOL ok = getStatus(&done, &err);
			row("migration_status_after call=%d complete=%d error=%s", ok, done, err ? err.description.UTF8String : "-");
		}
		if (lookup) ns_rows(@"state_after", lookup(nil));
		if (result) {
			BOOL wrote = [@"475\n" writeToFile:marker atomically:YES encoding:NSUTF8StringEncoding error:nil];
			row("marker written=%d", wrote);
		}
		row("result=%s", result ? "done" : "migration_returned_false");
	}
	return 0;
}

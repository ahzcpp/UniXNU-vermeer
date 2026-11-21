/* Runner-only smoke helper for unixnu-wk469 (built and run on the CI macOS runner; never delivered, never on a device).
 * Run under the role basename com.apple.WebKit.GPU so the observer dumps all of its threads. Known wait relations:
 *   thread H takes an os_unfair_lock and sleeps; thread W blocks on that lock  -> ulock wait, owner = tid(H)
 *   a serial queue runs a sleeping block on thread B; the main thread dispatch_sync()s onto that queue
 *                                                   -> expected workloop sync wait, owner = tid(B) (recorded, informative)
 * Writes "main= hold= wait= block=" thread ids to argv[1] once the relations are in place. Exits by alarm. */
#include <dispatch/dispatch.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static os_unfair_lock g_lock = OS_UNFAIR_LOCK_INIT;
static uint64_t g_hold, g_wait, g_block;
static void *hold(void *a) { (void)a; pthread_threadid_np(NULL, &g_hold); os_unfair_lock_lock(&g_lock); sleep(400); return NULL; }
static void *waiter(void *a) { (void)a; pthread_threadid_np(NULL, &g_wait); os_unfair_lock_lock(&g_lock); return NULL; }
int main(int argc, char **argv)
{
	if (argc != 2) return 64;
	alarm(170);
	pthread_t h, w;
	if (pthread_create(&h, NULL, hold, NULL)) return 1;
	usleep(300000);
	if (pthread_create(&w, NULL, waiter, NULL)) return 1;
	dispatch_queue_t q = dispatch_queue_create("wk469.smoke.serial", DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t started = dispatch_semaphore_create(0);
	dispatch_async(q, ^{ pthread_threadid_np(NULL, &g_block); dispatch_semaphore_signal(started); sleep(400); });
	dispatch_semaphore_wait(started, DISPATCH_TIME_FOREVER);
	usleep(300000);
	uint64_t m = 0; pthread_threadid_np(NULL, &m);
	FILE *f = fopen(argv[1], "w"); if (!f) return 2;
	fprintf(f, "main=%llu hold=%llu wait=%llu block=%llu\n", (unsigned long long)m, (unsigned long long)g_hold,
		(unsigned long long)g_wait, (unsigned long long)g_block);
	fclose(f);
	dispatch_sync(q, ^{ });
	return 0;
}

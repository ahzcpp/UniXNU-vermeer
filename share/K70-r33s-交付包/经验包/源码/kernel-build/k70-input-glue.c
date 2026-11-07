/* K70 input glue for NativeHidInput (ShengHidInput) and HidInputProbe.
 * The touch producer (r24) fills native_touch_* under the seqlock and calls the sink; until then
 * no finger is ever published. The button sink is stored for a later button driver. */
#include <stdint.h>
#include <stddef.h>

volatile uint32_t native_touch_seq, native_touch_count, native_touch_hid_reports;
volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];

static void (*volatile k70_touch_sink)(void *);
static void *volatile k70_touch_sink_context;
static void (*volatile k70_buttons_sink)(void *, uint32_t);
static void *volatile k70_buttons_sink_context;

void native_touch_set_sink(void (*sink)(void *), void *context);
void native_buttons_set_sink(void (*sink)(void *, uint32_t), void *context);
void k70_touch_notify(void);
int k70_touch_sink_ready(void);

/* True once NativeHidInput has started and registered its touch sink. */
int
k70_touch_sink_ready(void)
{
	return __atomic_load_n(&k70_touch_sink, __ATOMIC_ACQUIRE) != NULL;
}

void
native_touch_set_sink(void (*sink)(void *), void *context)
{
	k70_touch_sink = NULL;
	__atomic_store_n(&k70_touch_sink_context, context, __ATOMIC_RELEASE);
	__atomic_store_n(&k70_touch_sink, sink, __ATOMIC_RELEASE);
}

void
native_buttons_set_sink(void (*sink)(void *, uint32_t), void *context)
{
	k70_buttons_sink = NULL;
	__atomic_store_n(&k70_buttons_sink_context, context, __ATOMIC_RELEASE);
	__atomic_store_n(&k70_buttons_sink, sink, __ATOMIC_RELEASE);
}

/* Called by the touch producer after it published a frame under native_touch_seq. */
void
k70_touch_notify(void)
{
	void (*sink)(void *) = __atomic_load_n(&k70_touch_sink, __ATOMIC_ACQUIRE);

	if (sink != NULL) {
		sink(__atomic_load_n(&k70_touch_sink_context, __ATOMIC_ACQUIRE));
	}
}

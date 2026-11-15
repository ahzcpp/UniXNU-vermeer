/* Exercise the actual formatter; host-only mock IOKit provides unused links. */
#define main unused_iokitholder_main
#include "../src/unixnu-iokitholder385.c"
#undef main
#include <assert.h>
int main(void) {
    char huge[4096]; memset(huge,'Z',sizeof huge-1);huge[sizeof huge-1]=0;
    assert(emit("%s",huge)==0);
    return 0;
}

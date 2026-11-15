/* Read-only, fixed capability probe. No unique identifiers or network secrets. */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int console_fd = -1;
static void line(const char *key, const char *value)
{
    char out[192];
    int n = snprintf(out, sizeof out, "MGPROBE339 %s=%s\n", key, value);
    if (n <= 0 || (size_t)n >= sizeof out) return;
    (void)fwrite(out, 1, (size_t)n, stdout); (void)fflush(stdout);
    if (console_fd >= 0) (void)write(console_fd, out, (size_t)n);
}
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "-c") == 0)
        console_fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK);
    else if (argc != 1) return 64;
    (void)alarm(15); /* A missing capability service must never hold boot. */
    void *lib = dlopen("/usr/lib/libMobileGestalt.dylib", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) { line("load", "unavailable"); return 1; }
    typedef CFTypeRef (*CopyAnswer)(CFStringRef);
    typedef uint32_t (*ProductType)(void);
    void *a = dlsym(lib, "MGCopyAnswer"), *b = dlsym(lib, "MGGetProductType");
    CopyAnswer copy = NULL; ProductType type = NULL;
    _Static_assert(sizeof copy == sizeof a && sizeof type == sizeof b, "function pointer ABI");
    memcpy(&copy, &a, sizeof copy); memcpy(&type, &b, sizeof type);
    if (!copy) { line("copy", "unavailable"); return 1; }
    if (type) {char v[32]; (void)snprintf(v, sizeof v, "0x%08x", type()); line("MGGetProductType", v);}
    static const char *const keys[] = {
        "ProductType", "HWModelStr", "IsVirtualDevice", "HasBaseband",
        "telephony", "cellular-data", "CellularDataCapability"
    };
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        CFStringRef key = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
        CFTypeRef value = key ? copy(key) : NULL;
        if (key) CFRelease(key);
        char text[64] = "absent";
        if (value) {
            CFTypeID t = CFGetTypeID(value);
            if (t == CFBooleanGetTypeID()) (void)snprintf(text, sizeof text, "%s", CFBooleanGetValue(value) ? "true" : "false");
            else if (t == CFStringGetTypeID()) {
                if (!CFStringGetCString(value, text, sizeof text, kCFStringEncodingUTF8)) (void)snprintf(text, sizeof text, "string-too-long");
                for (size_t j = 0; text[j]; ++j)
                    if (!((text[j] >= 'a' && text[j] <= 'z') || (text[j] >= 'A' && text[j] <= 'Z') ||
                          (text[j] >= '0' && text[j] <= '9') || text[j] == ',' || text[j] == '_' || text[j] == '-')) text[j] = '?';
            } else if (t == CFNumberGetTypeID()) {
                int32_t n;
                if (CFNumberGetValue(value, kCFNumberSInt32Type, &n)) (void)snprintf(text, sizeof text, "%d", n);
                else (void)snprintf(text, sizeof text, "number-out-of-range");
            } else (void)snprintf(text, sizeof text, "other-type");
            CFRelease(value);
        }
        line(keys[i], text);
    }
    line("done", "1"); (void)alarm(0);
    if (console_fd >= 0) (void)close(console_fd);
    return 0;
}

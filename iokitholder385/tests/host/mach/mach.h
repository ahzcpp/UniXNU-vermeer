/* Host-only stub (Linux simulation test). Never used for the native/SDK build or tests/proto_check.c. */
#ifndef IH_HOST_MACH_STUB_H
#define IH_HOST_MACH_STUB_H
typedef unsigned int mach_port_t;
typedef int kern_return_t;
typedef unsigned int boolean_t;
#define KERN_SUCCESS 0
#define MACH_PORT_NULL ((mach_port_t)0)
#endif

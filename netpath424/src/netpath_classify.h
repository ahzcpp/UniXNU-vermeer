/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef NETPATH376_CLASSIFY_H
#define NETPATH376_CLASSIFY_H
/* An HTTP error response still demonstrates transport/TLS reachability. It
 * must not be reported as successful loading, login, or Store operation. */
static int netpath_http_reply(int got_http,long status,int has_error,int expected_cancel) {
 return got_http&&status>=100&&status<=599&&(!has_error||expected_cancel);
}
#endif

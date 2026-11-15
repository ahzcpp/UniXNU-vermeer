/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include "../src/netpath_classify.h"
int main(void) {
 unsigned checks=0;
 for(long status=-1;status<=700;status++)for(int http=0;http<=1;http++)for(int err=0;err<=1;err++)for(int intended=0;intended<=1;intended++) {
  int got=netpath_http_reply(http,status,err,intended);
  if(!http||status<100||status>599||(err&&!intended))assert(!got);else assert(got);checks++;
 }
 assert(netpath_http_reply(1,403,0,0)); /* Transport reached server, not App Store success. */
 assert(netpath_http_reply(1,200,1,1)); /* Deliberately stop after authenticated headers. */
 assert(!netpath_http_reply(1,200,1,0)); /* An unrelated error is never converted to success. */
 assert(!netpath_http_reply(0,200,0,0)); /* No synthetic HTTP status. */
 printf("NETPATH376 classifier checks=%u pass\n",checks+4);return 0;
}

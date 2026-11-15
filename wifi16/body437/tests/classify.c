/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/Body437.h"
static unsigned checks;
#define C(x) do {assert(x);++checks;} while(0)
static struct body437_state response(long status){struct body437_state s={0};body437_response(&s,1,status);return s;}
int main(void){
 struct body437_state s=response(200);C(!body437_receive(&s,9));body437_finish(&s,BODY437_OK);struct body437_result r=body437_classify(&s,0);C(r.progress&&r.complete_body&&!r.partial_body&&!r.capped_progress);
 s=response(200);C(!body437_receive(&s,8192));C(body437_receive(&s,8192));C(!body437_receive(&s,999));body437_finish(&s,BODY437_CANCELLED);r=body437_classify(&s,0);C(r.progress&&!r.complete_body&&r.partial_body&&r.capped_progress&&s.bytes==16384&&!s.overshoot);
 s=response(200);C(body437_receive(&s,UINT64_MAX));C(s.bytes==BODY437_CAP&&s.overshoot);body437_finish(&s,BODY437_OK);r=body437_classify(&s,0);C(r.capped_progress&&!r.complete_body);
 s=response(200);C(!body437_receive(&s,0));body437_finish(&s,BODY437_CANCELLED);r=body437_classify(&s,0);C(!r.progress&&r.zero_body&&!r.capped_progress);
 s=response(200);body437_finish(&s,BODY437_OK);r=body437_classify(&s,0);C(!r.progress&&!r.complete_body&&r.zero_body);
 s=response(200);C(!body437_receive(&s,10));body437_finish(&s,BODY437_CANCELLED);r=body437_classify(&s,0);C(!r.progress&&!r.capped_progress&&r.partial_body);
 for(int e=BODY437_TIMEOUT;e<=BODY437_OTHER_ERROR;++e){s=response(200);C(body437_receive(&s,BODY437_CAP));body437_finish(&s,e);r=body437_classify(&s,0);C(!r.progress&&!r.capped_progress&&r.partial_body);}
 s=response(200);C(!body437_receive(&s,12));body437_finish(&s,BODY437_TIMEOUT);r=body437_classify(&s,0);C(!r.progress&&!r.complete_body&&r.partial_body);
 s=response(200);C(!body437_receive(&s,12));r=body437_classify(&s,1);C(!r.progress&&!r.complete_body&&r.partial_body&&r.bounded_timeout);body437_finish(&s,BODY437_OK);r=body437_classify(&s,1);C(!r.progress&&!r.complete_body); /* callback won deadline race: conservative */
 s=response(200);C(!body437_receive(&s,12));body437_redirect(&s,302);body437_finish(&s,BODY437_OK);r=body437_classify(&s,0);C(!r.progress&&r.complete_body&&!r.normal_2xx);
 s=response(200);C(!body437_receive(&s,12));body437_finish(&s,BODY437_OK);struct body437_state saved=s;C(!body437_receive(&s,UINT64_MAX));body437_finish(&s,BODY437_OTHER_ERROR);body437_response(&s,0,0);C(!memcmp(&s,&saved,sizeof s));
 s=response(200);s.callbacks=UINT_MAX;C(!body437_receive(&s,1));C(s.callbacks==UINT_MAX);s.redirects=UINT_MAX;body437_redirect(&s,302);C(s.redirects==UINT_MAX);
 s=response(200);s.bytes=BODY437_CAP+1;body437_finish(&s,BODY437_OK);r=body437_classify(&s,0);C(!r.progress&&!r.complete_body);
 /* Exhaust error/status boundaries with real production transitions, not a mirror classifier. */
 for(long status=-1;status<=700;++status)for(int error=0;error<=3;++error)for(int cap=0;cap<=1;++cap)for(int deadline=0;deadline<=1;++deadline){
  s=response(status);if(cap)C(body437_receive(&s,BODY437_CAP));else C(!body437_receive(&s,7));body437_finish(&s,error);r=body437_classify(&s,deadline);
  if(deadline||status<200||status>=300||error>=2||(error==1&&!cap))C(!r.progress);
  else C(r.progress);
  C(!(r.complete_body&&r.capped_progress));C(s.bytes<=BODY437_CAP);
 }
 printf("NETBODY437 classifier checks=%u pass\n",checks);return 0;
}

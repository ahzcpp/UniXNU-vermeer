/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef BODY437_H
#define BODY437_H
#include <stdint.h>
#include <limits.h>
#define BODY437_CAP UINT64_C(16384)
enum body437_error { BODY437_OK=0, BODY437_CANCELLED=1, BODY437_TIMEOUT=2, BODY437_OTHER_ERROR=3 };
struct body437_state {
 uint64_t bytes; unsigned callbacks,redirects; long status;
 int got_http,cap_cancel,overshoot,completed,error;
};
struct body437_result {
 int http_reply,normal_2xx,progress,complete_body,capped_progress,partial_body,zero_body,bounded_timeout;
};
static inline void body437_response(struct body437_state *s,int http,long status) {
 if(s->completed)return;
 s->got_http=http&&status>=100&&status<=599;s->status=status;
}
static inline void body437_redirect(struct body437_state *s,long status) {
 if(s->completed)return;
 if(s->redirects!=UINT_MAX)++s->redirects;
 body437_response(s,1,status);
}
/* Called with the delegate lock held. Reads length only, never data bytes.
 * Counts at most CAP. A framework callback may already exceed remaining CAP;
 * report overshoot and cancel once rather than claiming an exact wire total. */
static inline int body437_receive(struct body437_state *s,uint64_t length) {
 if(s->completed)return 0;
 if(s->callbacks!=UINT_MAX)++s->callbacks;
 if(s->cap_cancel||!length)return 0;
 if(s->bytes>BODY437_CAP)return 0;
 uint64_t room=BODY437_CAP-s->bytes;
 if(length>=room) {
  s->overshoot=length>room;s->bytes=BODY437_CAP;s->cap_cancel=1;return 1;
 }
 s->bytes+=length;return 0;
}
static inline void body437_finish(struct body437_state *s,int error) {
 if(s->completed)return;
 s->error=error;s->completed=1;
}
static inline struct body437_result body437_classify(const struct body437_state *s,int deadline) {
 struct body437_result r={0};r.bounded_timeout=deadline!=0;r.partial_body=s->bytes>0;r.zero_body=s->bytes==0;
 if(s->bytes>BODY437_CAP)return r;
 r.http_reply=s->got_http&&s->status>=100&&s->status<=599;
 r.normal_2xx=r.http_reply&&s->status>=200&&s->status<300&&!s->redirects;
 if(deadline||!s->completed||!s->bytes)return r;
 r.complete_body=!s->cap_cancel&&s->error==BODY437_OK;
 if(r.complete_body)r.partial_body=0;
 r.capped_progress=r.normal_2xx&&s->cap_cancel&&(s->error==BODY437_CANCELLED||s->error==BODY437_OK);
 r.progress=r.normal_2xx&&(r.complete_body||r.capped_progress);
 return r;
}
#endif

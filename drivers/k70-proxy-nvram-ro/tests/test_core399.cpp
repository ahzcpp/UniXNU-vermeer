#include "fixture399.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <atomic>
#include <limits>
using namespace k70ro399;
static unsigned checks;
#define C(x) do{assert(x);checks++;}while(0)
int main(){
 auto v=fixture();Geometry g{8192,1,0};Bank b{};C(validate(v.data(),v.size(),g,&b)==ok);C(b.version==1&&b.ready);
 std::vector<uint8_t> out(v.size()+2,0x5a);C(read(b,0,out.data()+1,v.size())==ok);C(out.front()==0x5a&&out.back()==0x5a);C(!memcmp(v.data(),out.data()+1,v.size()));
 for(size_t at=0;at<=v.size();at++){uint8_t t[34];memset(t,0xa5,sizeof t);auto n=std::min<size_t>(32,v.size()-at);C(read(b,at,t+1,n)==ok);C(t[0]==0xa5&&t[n+1]==0xa5);C(!memcmp(t+1,v.data()+at,n));}
 C(read(b,8192,nullptr,0)==ok);C(read(b,8193,nullptr,0)==bad_bank);C(read(b,0,nullptr,1)==bad_bank);C(read(b,UINT64_MAX,out.data(),1)==bad_bank);C(read(b,1,out.data(),UINT64_MAX)==bad_bank);
 C(select(b,0)==ok);C(select(b,1)==bad_bank);C(reject_write(b)==not_writable);C(read(Bank{},0,out.data(),1)==not_ready);C(reject_write(Bank{})==not_ready);
 for(unsigned i=0;i<32;i++){Bank tmp{};auto a=Admission{bool(i&1),bool(i&2),bool(i&4),bool(i&8),bool(i&16)};C(may_activate(a)==(i==31));C(!tmp.ready);}
 auto bad=[&](std::vector<uint8_t>w,Geometry q,Status expected){Bank x=b;C(validate(w.data(),w.size(),q,&x)==expected);C(!x.ready&&x.bytes==nullptr);};
 bad(v,{8192,2,0},unsupported);bad(v,{8192,1,1},unsupported);bad(v,{8208,1,0},bad_bank);
 auto w=v;w[100]^=1;bad(w,g,bad_bank);w=v;w[1]^=1;bad(w,g,bad_bank);
 w=v;hdr(w,0,"other",32);bad(w,g,unsupported);w=v;put16(w.data()+34,0);w[33]=header_sum(w.data()+32);sum(w);bad(w,g,bad_bank);
 w=v;put16(w.data()+34,65535);w[33]=header_sum(w.data()+32);sum(w);bad(w,g,bad_bank);
 w=v;hdr(w,2096,"common",6096);sum(w);bad(w,g,bad_bank);
 w=v;hdr(w,2096,"mystery",6096);sum(w);bad(w,g,unsupported);
 w=v;memset(w.data()+48,'a',2048);sum(w);bad(w,g,bad_bank);
 w=v;hdr(w,0,"2nvram",32);hdr(w,32,"2common",2064);sum(w);C(validate(w.data(),w.size(),g,&b)==ok&&b.version==2);
 w=v;hdr(w,2096,"system",6096);sum(w);C(validate(w.data(),w.size(),g,&b)==ok);
 C(validate(nullptr,8192,g,&b)==not_ready);C(!b.ready);C(validate(v.data(),v.size(),g,nullptr)==bad_bank);
 // Actual immutable-bank core reads ONLY a separately owned snapshot, not mutated input.
 auto snap=v;C(validate(snap.data(),snap.size(),g,&b)==ok);v[35]^=0xff;C(read(b,0,out.data(),8192)==ok);C(!memcmp(out.data(),snap.data(),8192));
 Activation act{};int owner,other;C(!activate(&act,nullptr));C(!is_active(&act));C(activate(&act,&owner));C(is_active(&act));C(for_provider(&act,&owner));C(!for_provider(&act,&other));C(!for_provider(&act,nullptr));C(!activate(&act,&other));C(for_provider(&act,&owner));
 for(unsigned j=0;j<100;j++){Activation a{};std::atomic<bool>begin{false};std::atomic<unsigned>seen{0};std::thread reader([&]{while(!begin.load()){};while(!is_active(&a)){};if(for_provider(&a,&owner)&&!for_provider(&a,&other))seen++;});begin=true;C(activate(&a,&owner));reader.join();C(seen==1);}
 printf("core_checks=%u\n",checks);
}

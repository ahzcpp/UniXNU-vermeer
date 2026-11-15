int main(void) {
 assert(np_model_bytes_ok("a",2,256));assert(!np_model_bytes_ok("",1,256));
 assert(!np_model_bytes_ok("a",0,256));assert(!np_model_bytes_ok("a",257,256));
 char b[4]={'a',0,'b',0};assert(!np_model_bytes_ok(b,4,4));
 char c[3]={'a','b','c'};assert(!np_model_bytes_ok(c,3,3));assert(!np_model_bytes_ok(0,2,256));
 assert(argnum("12",12)==12);assert(argnum("30",30)==30);assert(!argnum("13",12));assert(!argnum("31",30));assert(!argnum("0",12));assert(!argnum("-1",12));assert(!argnum("",12));assert(!argnum("2x",12));assert(!argnum("999999999999999999999999",12));return 0;
}

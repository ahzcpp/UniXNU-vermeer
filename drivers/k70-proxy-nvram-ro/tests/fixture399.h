#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include "ProxyBank399.h"
static void put16(uint8_t*p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static void hdr(std::vector<uint8_t>&v,size_t at,const char*n,size_t len){auto*p=v.data()+at;memset(p,0,16);p[0]=0x70;put16(p+2,len/16);memcpy(p+4,n,strlen(n));p[1]=k70ro399::header_sum(p);}
static void sum(std::vector<uint8_t>&v){put32(v.data()+16,k70ro399::adler(v.data()+20,v.size()-20));}
// Synthetic fixture derived from UniXNU devicetree.py empty_nvram_proxy geometry.
// NEVER used as a production replacement for missing /chosen bytes.
static std::vector<uint8_t> fixture(){std::vector<uint8_t>v(8192);hdr(v,0,"nvram",32);hdr(v,32,"common",2064);hdr(v,2096,"free",6096);sum(v);return v;}

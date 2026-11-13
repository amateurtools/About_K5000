// psweep <os> <sram> <prep_script> <tests> <steps_per_test> : boot, run prep script (dump + program change), snapshot, then fork one child per test.
// tests file: "<id> <hexmsg>" per line (messages are fed as MIDI in).  Child prints: "<id> TX=<hex> D=<addr:old>new,..." for changed DRAM bytes (0x300000..) and K=<kram addr> for changed kram bytes.
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/wait.h>
#include <unistd.h>
#include "../core/board.h"
struct Ev { long step; std::vector<uint8_t> bytes; };
int main(int c,char**v){
  K5000Board b; FILE*f=fopen(v[1],"rb"); b.os.resize(0x100000); b.os.resize(fread(b.os.data(),1,0x100000,f)); fclose(f);
  for(int i=0;i<0x17;i++) b.io_pull[i]=0xFF;
  { FILE*g=fopen(v[2],"rb"); if(fread(b.sram.data(),1,b.sram.size(),g)){} fclose(g); }
  std::vector<Ev> evs; { std::ifstream in(v[3]); std::string line; while(std::getline(in,line)){ if(line.empty()||line[0]=='#') continue; std::istringstream ss(line); Ev e; std::string hx; ss>>e.step>>hx; for(size_t i=0;i+1<hx.size();i+=2) e.bytes.push_back(strtol(hx.substr(i,2).c_str(),0,16)); evs.push_back(e);} }
  long STEPS=atol(v[5]);
  b.rx_icr=11; Fr30 cpu(&b); b.cpu=&cpu; cpu.pc=0x100410; cpu.tbr=0xFFD00; cpu.r[15]=cpu.ssp=0x380000;
  const long T0=150000000; size_t ei=0; long last=0; for(auto&e:evs) last=std::max(last,e.step);
  for(long i=0;i<T0+last+10000000;i++){ b.service(); b.feed_midi(); if((i%2000)==0) b.raise(14); long t=i-T0;
    while(ei<evs.size() && t==evs[ei].step){ b.midi_in=evs[ei].bytes; b.midi_in_pos=0; ei++; }
    cpu.step(); b.tick(1); }
  std::vector<uint8_t> d0=b.dram, k0=b.kram; fflush(stdout);
  std::ifstream tin(v[4]); std::string line;
  while(std::getline(tin,line)){ if(line.empty()||line[0]=='#') continue; std::istringstream ss(line); std::string id,hx; ss>>id>>hx; std::vector<uint8_t> m; for(size_t i=0;i+1<hx.size();i+=2) m.push_back(strtol(hx.substr(i,2).c_str(),0,16));
    pid_t p=fork(); if(p==0){
      b.midi_out.clear(); b.midi_in=m; b.midi_in_pos=0;
      for(long i=0;i<STEPS;i++){ b.service(); b.feed_midi(); if((i%2000)==0) b.raise(14); cpu.step(); b.tick(1); }
      printf("%s TX=",id.c_str()); for(auto x:b.midi_out) printf("%02x",x);
      printf(" D="); for(size_t a=0;a<b.dram.size();a++) if(b.dram[a]!=d0[a]) printf("%x:%02x>%02x,",(unsigned)(0x300000+a),d0[a],b.dram[a]);
      printf(" K="); for(size_t a=0;a<b.kram.size();a++) if(b.kram[a]!=k0[a]) printf("%x:%02x>%02x,",(unsigned)a,k0[a],b.kram[a]);
      printf("\n"); fflush(stdout); _exit(0); }
    int st; waitpid(p,&st,0); }
}

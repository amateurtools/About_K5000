// px <os> <sram> <script> <dram_out> : boot, then play a script of "<step_after_boot> <hexbytes>" MIDI-in messages; print every TX byte with its step; dump DRAM
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "../core/board.h"
struct Ev { long step; std::vector<uint8_t> bytes; };
int main(int c,char**v){
  K5000Board b; FILE*f=fopen(v[1],"rb"); b.os.resize(0x100000); b.os.resize(fread(b.os.data(),1,0x100000,f)); fclose(f);
  for(int i=0;i<0x17;i++) b.io_pull[i]=0xFF;
  { FILE*g=fopen(v[2],"rb"); if(fread(b.sram.data(),1,b.sram.size(),g)){} fclose(g); }
  std::vector<Ev> evs; std::ifstream in(v[3]); std::string line; long last=0;
  while(std::getline(in,line)){ if(line.empty()||line[0]=='#') continue; std::istringstream ss(line); Ev e; std::string hx; ss>>e.step>>hx; for(size_t i=0;i+1<hx.size();i+=2) e.bytes.push_back(strtol(hx.substr(i,2).c_str(),0,16)); evs.push_back(e); last=std::max(last,e.step); }
  b.rx_icr=11; Fr30 cpu(&b); b.cpu=&cpu; cpu.pc=0x100410; cpu.tbr=0xFFD00; cpu.r[15]=cpu.ssp=0x380000;
  const long T0=150000000; size_t ei=0; size_t txseen=0;
  for(long i=0;i<T0+last+12000000;i++){ b.service(); b.feed_midi(); if((i%2000)==0) b.raise(14); long t=i-T0;
    while(ei<evs.size() && t==evs[ei].step){ b.midi_in=evs[ei].bytes; b.midi_in_pos=0; ei++; }
    cpu.step(); b.tick(1);
    if(b.midi_out.size()!=txseen){ printf("TX @%ld:",t); for(;txseen<b.midi_out.size();txseen++) printf(" %02x",b.midi_out[txseen]); printf("\n"); } }
  FILE*o=fopen(v[4],"wb"); fwrite(b.dram.data(),1,b.dram.size(),o); fclose(o);
  printf("done, midi_out=%zu\n",b.midi_out.size());
}

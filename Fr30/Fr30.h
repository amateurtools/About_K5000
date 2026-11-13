// FR30 CPU core. Big-endian bus, 16-bit instruction words.
// Opcode tables per Fujitsu MB91F109 manual Appendix E.
// A branch (or any control-flow instruction) in a delay slot raises the
// undefined-instruction exception (vector 14) instead of recursing.
#pragma once
#include <cstdint>
#include <cstring>

struct Fr30Bus {
    virtual uint8_t  r8 (uint32_t a) = 0;
    virtual uint16_t r16(uint32_t a) = 0;   // address already aligned by core
    virtual uint32_t r32(uint32_t a) = 0;
    virtual void w8 (uint32_t a, uint8_t v)  = 0;
    virtual void w16(uint32_t a, uint16_t v) = 0;
    virtual void w32(uint32_t a, uint32_t v) = 0;
    virtual ~Fr30Bus() = default;
};

class Fr30 {
public:
    enum { CS = 1<<5, CI = 1<<4, CN = 1<<3, CZ = 1<<2, CV = 1<<1, CC = 1<<0 };
    uint32_t r[16] = {};          // r[15] = active stack pointer
    uint32_t pc = 0, tbr = 0xFFC00, rp = 0, ssp = 0, usp = 0, mdh = 0, mdl = 0;
    uint32_t ilm = 15;            // 5 bits
    uint8_t  ccr = 0;             // S I N Z V C
    bool d0 = false, d1 = false;  // SCR step-division flags (PS bits 9, 10)
    uint32_t res[16] = {};          // LDRES/STRES resource channels (no hardware attached)
    uint64_t cycles = 0;          // one per instruction (timing refinement later)
    Fr30Bus* bus = nullptr;

    explicit Fr30(Fr30Bus* b) : bus(b) {}

    // --- PS access ---
    uint32_t ps() const { return (ilm << 16) | (d1 << 10) | (d0 << 9) | ccr; }
    void set_ps(uint32_t v) {
        set_ilm(v >> 16); d1 = (v >> 10) & 1; d0 = (v >> 9) & 1;
        set_ccr(v & 0xFF);
    }
    void set_ccr(uint8_t v) {
        if ((v ^ ccr) & CS) {                 // swap active stack
            if (ccr & CS) usp = r[15]; else ssp = r[15];
            r[15] = (v & CS) ? usp : ssp;
        }
        ccr = v;
    }
    uint32_t get_sp_ssp() const { return (ccr & CS) ? ssp : r[15]; }
    uint32_t get_sp_usp() const { return (ccr & CS) ? r[15] : usp; }

    void reset() {
        ccr = 0; ilm = 15; d0 = d1 = false; tbr = 0xFFC00;
        pc = bus->r32(tbr + 0x3FC);
    }

    // ILM write rule: if current ILM >= 16, new value forced into 16..31
    void set_ilm(uint32_t v) { v &= 0x1F; if (ilm >= 16) v |= 0x10; ilm = v; }

    // Single step. Returns false if the instruction was undefined (exception already taken).
    // Handles delay slots internally; interrupts must be tested by the caller between steps.
    bool step() {
        in_delay = false; branch_taken = false; delayed = false; undefined = false;
        uint32_t start = pc;
        exec(start);
        if (undefined) { take_undef(start); return false; }
        if (delayed) {
            uint32_t tgt = br_target, retaddr = br_ret; bool call = br_call;
            uint32_t slot = pc;
            in_delay = true; branch_taken = false; delayed = false;
            exec(slot);
            in_delay = false;
            if (undefined) { take_undef(slot); return false; }
            if (call) rp = retaddr;
            pc = tgt & ~1u;
        }
        return true;
    }

    // Accept an interrupt/EIT. Vector number, level to put in ILM (or -1 to keep).
    void eit(unsigned vct, int level, uint32_t retpc, bool clear_i = true) {
        uint32_t p = ps();
        set_ccr(ccr & ~CS);                   // S=0 -> SSP
        r[15] -= 4; bus->w32(r[15], p);
        r[15] -= 4; bus->w32(r[15], retpc);
        if (clear_i) ccr &= ~CI;
        if (level >= 0) ilm = level;
        pc = bus->r32(tbr + 0x3FC - 4 * vct) & ~1u;
    }

    // Can a user interrupt of ICR level `lvl` (0..31) be taken now?
    bool irq_ok(unsigned lvl) const { return (ccr & CI) && lvl < ilm; }
    bool nmi_ok() const { return true; }

    // --- delay-slot state exposed for tracing ---
    uint32_t last_pc = 0;

private:
    bool in_delay = false, branch_taken = false, delayed = false, undefined = false;
    uint32_t br_target = 0, br_ret = 0; bool br_call = false;

    void take_undef(uint32_t at) { eit(14, -1, at); }
    uint16_t fetch(uint32_t a) { return bus->r16(a & ~1u); }

    // flag helpers
    void nz(uint32_t v) { ccr = (ccr & ~(CN|CZ)) | ((v >> 31) ? CN : 0) | (v == 0 ? CZ : 0); }
    void nz8(uint8_t v)  { ccr = (ccr & ~(CN|CZ)) | ((v >> 7)  ? CN : 0) | (v == 0 ? CZ : 0); }
    void nz16(uint16_t v){ ccr = (ccr & ~(CN|CZ)) | ((v >> 15) ? CN : 0) | (v == 0 ? CZ : 0); }
    uint32_t add(uint32_t a, uint32_t b, uint32_t cin, bool setf) {
        uint64_t s = (uint64_t)a + b + cin; uint32_t v = (uint32_t)s;
        if (setf) {
            nz(v);
            ccr = (ccr & ~(CV|CC)) | ((s >> 32) ? CC : 0)
                | ((~(a ^ b) & (a ^ v)) >> 31 ? CV : 0);
        }
        return v;
    }
    uint32_t sub(uint32_t a, uint32_t b, uint32_t bin, bool setf) {   // a - b - bin
        uint64_t s = (uint64_t)a - b - bin; uint32_t v = (uint32_t)s;
        if (setf) {
            nz(v);
            ccr = (ccr & ~(CV|CC)) | ((s >> 32) ? CC : 0)
                | (((a ^ b) & (a ^ v)) >> 31 ? CV : 0);
        }
        return v;
    }
    static int32_t sx(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }
    void bad() { undefined = true; }

    // register for Rs field (0..5)
    bool get_rs(unsigned i, uint32_t& v) {
        switch (i) { case 0: v = tbr; return true; case 1: v = rp; return true;
                     case 2: v = get_sp_ssp(); return true; case 3: v = get_sp_usp(); return true;
                     case 4: v = mdh; return true; case 5: v = mdl; return true; }
        v = 0; return false;
    }
    bool set_rs(unsigned i, uint32_t v) {
        switch (i) { case 0: tbr = v; return true; case 1: rp = v; return true;
                     case 2: if (ccr & CS) ssp = v; else r[15] = v; return true;
                     case 3: if (ccr & CS) r[15] = v; else usp = v; return true;
                     case 4: mdh = v; return true; case 5: mdl = v; return true; }
        return false;
    }

    void branch(uint32_t tgt, bool d, bool call, uint32_t ret) {
        if (in_delay) { bad(); return; }
        if (call && !d) rp = ret;
        if (d) { delayed = true; br_target = tgt; br_ret = ret; br_call = call; }
        else pc = tgt & ~1u;
    }
    bool cond(unsigned c) const {
        bool N = ccr & CN, Z = ccr & CZ, V = ccr & CV, C = ccr & CC;
        switch (c) {
            case 0: return true;  case 1: return false;
            case 2: return Z;     case 3: return !Z;
            case 4: return C;     case 5: return !C;
            case 6: return N;     case 7: return !N;
            case 8: return V;     case 9: return !V;
            case 10: return N != V;           case 11: return N == V;
            case 12: return Z || (N != V);    case 13: return !(Z || (N != V));
            case 14: return C || Z;           default: return !(C || Z);
        }
    }

    void exec(uint32_t a) {
        last_pc = a;
        uint16_t h = fetch(a);
        unsigned op = h >> 8, rj = (h >> 4) & 15, ri = h & 15, u8 = h & 0xFF;
        uint32_t next = a + 2;
        pc = next; cycles++;

        // ---- E-type groups ----
        if (op == 0x97 || op == 0x9F || op == 0x07 || op == 0x17) {
            unsigned sub = rj;
            if (op == 0x97) switch (sub) {
                case 0: branch(r[ri], false, false, 0); return;
                case 1: branch(r[ri], false, true, next); return;
                case 2: if (in_delay) return bad(); pc = rp; return;
                case 3: { if (in_delay) return bad();
                          uint32_t np = bus->r32(r[15]); r[15] += 4;
                          uint32_t p = bus->r32(r[15]); r[15] += 4;
                          set_ps(p); pc = np & ~1u; return; }
                case 4: d0 = mdl >> 31; d1 = d0 ^ (r[ri] >> 31); mdh = d0 ? 0xFFFFFFFF : 0; return;
                case 5: d0 = d1 = false; mdh = 0; return;
                case 6: { // DIV1: {MDH,MDL}<<=1; D1 ? MDH+Ri : MDH-Ri
                    mdh = (mdh << 1) | (mdl >> 31); mdl <<= 1;
                    uint32_t t; bool c;
                    if (d1) { uint64_t s = (uint64_t)mdh + r[ri]; t = (uint32_t)s; c = s >> 32; }
                    else    { t = mdh - r[ri]; c = mdh < r[ri]; }
                    if ((d0 ^ d1 ^ c) == 0) { mdh = t; mdl |= 1; }
                    ccr = (ccr & ~(CZ|CC)) | (t == 0 ? CZ : 0) | (c ? CC : 0);
                    return; }
                case 7: { // DIV2: same add/sub rule; Z from result, then MDH=0 if Z
                    uint32_t t; bool c;
                    if (d1) { uint64_t s = (uint64_t)mdh + r[ri]; t = (uint32_t)s; c = s >> 32; }
                    else    { t = mdh - r[ri]; c = mdh < r[ri]; }
                    ccr = (ccr & ~(CZ|CC)) | (t == 0 ? CZ : 0) | (c ? CC : 0);
                    if (t == 0) mdh = 0;
                    return; }
                case 8:  r[ri] = (int32_t)(int8_t)r[ri]; return;
                case 9:  r[ri] &= 0xFF; return;
                case 10: r[ri] = (int32_t)(int16_t)r[ri]; return;
                case 11: r[ri] &= 0xFFFF; return;
                default: return bad();
            }
            if (op == 0x9F) switch (sub) {
                case 0: branch(r[ri], true, false, 0); return;
                case 1: branch(r[ri], true, true, next); return;
                case 2: if (in_delay) return bad();
                        delayed = true; br_target = rp; br_call = false; return;
                case 3: if (in_delay) return bad();   // INTE
                        eit(9, 4, next, false); return;   // INTE: I unchanged, ILM=4
                case 6: if (ccr & CZ) mdl += 1; return;              // DIV3
                case 7: if (d1) mdl = -mdl; return;                                  // DIV4S
                case 8: { uint32_t v = ((uint32_t)fetch(next) << 16) | fetch(next + 2);
                          r[ri] = v; pc = next + 4; return; }                        // LDI:32
                case 9: r[15] = r[14] + 4; r[14] = bus->r32(r[15] - 4); return;      // LEAVE
                case 10: return;                                                      // NOP
                case 12: case 13: case 14: case 15: pc = next + 2; return;           // COP*: skipped
                default: return bad();
            }
            if (op == 0x07) switch (sub) {
                case 0: { uint32_t v = bus->r32(r[15]); r[15] += 4; r[ri] = v; return; }
                case 1: { // MOV Ri, PS
                    uint32_t v = r[ri]; set_ccr(v & 0xFF); d0 = (v >> 9) & 1; d1 = (v >> 10) & 1; set_ilm(v >> 16); return; }
                case 8: { uint32_t v = bus->r32(r[15]); r[15] += 4; set_rs(ri, v); return; }
                case 9: { uint32_t v = bus->r32(r[15]); r[15] += 4; set_ps(v); return; }
                default: return bad();
            }
            // op == 0x17
            switch (sub) {
                case 0: { uint32_t v = r[ri]; r[15] -= 4; bus->w32(r[15], v); return; }
                case 1: r[ri] = ps(); return;
                case 8: { uint32_t v; get_rs(ri, v); r[15] -= 4; bus->w32(r[15], v); return; }
                case 9: { r[15] -= 4; bus->w32(r[15], ps()); return; }
                default: return bad();
            }
        }

        // ---- calls and branches ----
        if ((op >> 3) == 0x1A) { branch(next + sx(h & 0x7FF, 11) * 2, false, true, next); return; }
        if ((op >> 3) == 0x1B) { branch(next + sx(h & 0x7FF, 11) * 2, true,  true, next); return; }
        if ((op >> 4) == 0xE)  { if (cond(op & 15)) branch(next + sx(u8, 8) * 2, false, false, 0); else if (in_delay) bad(); return; }
        if ((op >> 4) == 0xF)  { // Bcc:D; delay slot is always executed
            if (in_delay) return bad();
            if (cond(op & 15)) branch(next + sx(u8, 8) * 2, true, false, 0);
            else { delayed = true; br_target = next + 2; br_call = false; }   // slot runs, then falls through
            return; }

        // ---- 4-bit opcode families ----
        switch (op >> 4) {
            case 0x2: { uint32_t ad = (r[14] + sx(h >> 4 & 0xFF, 8) * 4) & ~3u; r[ri] = bus->r32(ad); return; }
            case 0x3: { uint32_t ad = (r[14] + sx(h >> 4 & 0xFF, 8) * 4) & ~3u; bus->w32(ad, r[ri]); return; }
            case 0x4: { uint32_t ad = (r[14] + sx(h >> 4 & 0xFF, 8) * 2) & ~1u; r[ri] = bus->r16(ad); return; }
            case 0x5: { uint32_t ad = (r[14] + sx(h >> 4 & 0xFF, 8) * 2) & ~1u; bus->w16(ad, (uint16_t)r[ri]); return; }
            case 0x6: { uint32_t ad = r[14] + sx(h >> 4 & 0xFF, 8); r[ri] = bus->r8(ad); return; }
            case 0x7: { uint32_t ad = r[14] + sx(h >> 4 & 0xFF, 8); bus->w8(ad, (uint8_t)r[ri]); return; }
            case 0xC: r[ri] = (h >> 4) & 0xFF; return;                       // LDI:8
        }

        // ---- 8-bit opcodes ----
        uint32_t &Ri = r[ri], &Rj = r[rj];
        switch (op) {
            case 0xA6: Ri = add(Ri, Rj, 0, true); return;
            case 0xA4: Ri = add(Ri, rj, 0, true); return;
            case 0xA5: Ri = add(Ri, rj | 0xFFFFFFF0u, 0, true); return;
            case 0xA7: Ri = add(Ri, Rj, (ccr & CC) ? 1 : 0, true); return;
            case 0xA2: Ri = Ri + Rj; return;
            case 0xA0: Ri = Ri + rj; return;
            case 0xA1: Ri = Ri + (rj | 0xFFFFFFF0u); return;
            case 0xAC: Ri = sub(Ri, Rj, 0, true); return;
            case 0xAD: Ri = sub(Ri, Rj, (ccr & CC) ? 1 : 0, true); return;
            case 0xAE: Ri = Ri - Rj; return;
            case 0xAA: sub(Ri, Rj, 0, true); return;
            case 0xA8: sub(Ri, rj, 0, true); return;
            case 0xA9: sub(Ri, rj | 0xFFFFFFF0u, 0, true); return;
            case 0x82: Ri &= Rj; nz(Ri); return;
            case 0x92: Ri |= Rj; nz(Ri); return;
            case 0x9A: Ri ^= Rj; nz(Ri); return;
            case 0x84: { uint32_t ad = Ri & ~3u; uint32_t v = bus->r32(ad) & Rj; bus->w32(ad, v); nz(v); return; }
            case 0x94: { uint32_t ad = Ri & ~3u; uint32_t v = bus->r32(ad) | Rj; bus->w32(ad, v); nz(v); return; }
            case 0x9C: { uint32_t ad = Ri & ~3u; uint32_t v = bus->r32(ad) ^ Rj; bus->w32(ad, v); nz(v); return; }
            case 0x85: { uint32_t ad = Ri & ~1u; uint16_t v = bus->r16(ad) & (uint16_t)Rj; bus->w16(ad, v); nz16(v); return; }
            case 0x95: { uint32_t ad = Ri & ~1u; uint16_t v = bus->r16(ad) | (uint16_t)Rj; bus->w16(ad, v); nz16(v); return; }
            case 0x9D: { uint32_t ad = Ri & ~1u; uint16_t v = bus->r16(ad) ^ (uint16_t)Rj; bus->w16(ad, v); nz16(v); return; }
            case 0x86: { uint8_t v = bus->r8(Ri) & (uint8_t)Rj; bus->w8(Ri, v); nz8(v); return; }
            case 0x96: { uint8_t v = bus->r8(Ri) | (uint8_t)Rj; bus->w8(Ri, v); nz8(v); return; }
            case 0x9E: { uint8_t v = bus->r8(Ri) ^ (uint8_t)Rj; bus->w8(Ri, v); nz8(v); return; }
            // bit ops: low nibble L / high nibble H (u4 in rj field)
            case 0x80: { uint8_t v = bus->r8(Ri) & (rj | 0xF0); bus->w8(Ri, v); nz8(v); return; }
            case 0x81: { uint8_t v = bus->r8(Ri) & ((rj << 4) | 0x0F); bus->w8(Ri, v); nz8(v); return; }
            case 0x90: { uint8_t v = bus->r8(Ri) | rj; bus->w8(Ri, v); nz8(v); return; }
            case 0x91: { uint8_t v = bus->r8(Ri) | (rj << 4); bus->w8(Ri, v); nz8(v); return; }
            case 0x98: { uint8_t v = bus->r8(Ri) ^ rj; bus->w8(Ri, v); nz8(v); return; }
            case 0x99: { uint8_t v = bus->r8(Ri) ^ (rj << 4); bus->w8(Ri, v); nz8(v); return; }
            case 0x88: { uint8_t v = bus->r8(Ri) & rj; ccr = (ccr & ~(CN|CZ)) | (v ? 0 : CZ); return; }
            case 0x89: { uint8_t v = bus->r8(Ri) & (rj << 4); ccr = (ccr & ~(CN|CZ)) | (v ? 0 : CZ) | ((v & 0x80) ? CN : 0); return; }
            // multiply
            case 0xAF: { int64_t p = (int64_t)(int32_t)Ri * (int32_t)Rj; mdh = p >> 32; mdl = (uint32_t)p;
                         nz(mdl); ccr = (ccr & ~(CV|CZ)) | ((p != (int32_t)mdl) ? CV : 0) | (p == 0 ? CZ : 0); return; }
            case 0xAB: { uint64_t p = (uint64_t)Ri * Rj; mdh = p >> 32; mdl = (uint32_t)p;
                         nz(mdl); ccr = (ccr & ~CV) | (mdh ? CV : 0); return; }
            case 0xBF: { int32_t p = (int32_t)(int16_t)Ri * (int16_t)Rj; mdl = p; nz(mdl); return; }
            case 0xBB: { uint32_t p = (uint32_t)(uint16_t)Ri * (uint16_t)Rj; mdl = p; nz(mdl); return; }
            // shifts. count from Rj (low 5 bits), #u4, or #u4+16
            case 0xB6: case 0xB4: case 0xB5: case 0xB2: case 0xB0: case 0xB1: case 0xBA: case 0xB8: case 0xB9: {
                unsigned cnt = (op & 3) == 2 ? (Rj & 31) : ((op & 3) == 0 ? rj : rj + 16);
                // low bits: x6/x2/xA: reg; x4/x0/x8: imm; x5/x1/x9: imm+16
                if (op == 0xB6 || op == 0xB2 || op == 0xBA) cnt = Rj & 31;
                else if (op == 0xB4 || op == 0xB0 || op == 0xB8) cnt = rj; else cnt = rj + 16;
                uint32_t v = Ri; bool c = ccr & CC;
                if (cnt) {
                    if (op >= 0xB4 && op <= 0xB6)      { c = (v >> (32 - cnt)) & 1; v <<= cnt; }
                    else if (op >= 0xB0 && op <= 0xB2) { c = (v >> (cnt - 1)) & 1; v >>= cnt; }
                    else                               { c = ((int32_t)v >> (cnt - 1)) & 1; v = (int32_t)v >> cnt; }
                }
                Ri = v; nz(v); ccr = (ccr & ~CC) | (c ? CC : 0); return; }
            case 0x9B: Ri = (rj << 16) | fetch(next); pc = next + 2; return;  // LDI:20
            // loads
            case 0x04: Ri = bus->r32(Rj & ~3u); return;
            case 0x00: Ri = bus->r32((r[13] + Rj) & ~3u); return;
            case 0x03: Ri = bus->r32((r[15] + rj * 4) & ~3u); return;
            case 0x05: Ri = bus->r16(Rj & ~1u); return;
            case 0x01: Ri = bus->r16((r[13] + Rj) & ~1u); return;
            case 0x06: Ri = bus->r8(Rj); return;
            case 0x02: Ri = bus->r8(r[13] + Rj); return;
            // stores
            case 0x14: bus->w32(Rj & ~3u, Ri); return;
            case 0x10: bus->w32((r[13] + Rj) & ~3u, Ri); return;
            case 0x13: bus->w32((r[15] + rj * 4) & ~3u, Ri); return;
            case 0x15: bus->w16(Rj & ~1u, (uint16_t)Ri); return;
            case 0x11: bus->w16((r[13] + Rj) & ~1u, (uint16_t)Ri); return;
            case 0x16: bus->w8(Rj, (uint8_t)Ri); return;
            case 0x12: bus->w8(r[13] + Rj, (uint8_t)Ri); return;
            // transfers
            case 0x8B: Ri = Rj; return;
            case 0xB7: { uint32_t v; get_rs(rj, v); Ri = v; return; }
            case 0xB3: { set_rs(rj, Ri); return; }
            case 0x8A: { uint32_t t = Ri; Ri = bus->r8(Rj); bus->w8(Rj, (uint8_t)t); return; }
            case 0xBC: res[rj] = bus->r32(Ri & ~3u); Ri += 4; return;          // LDRES @Ri+, #u4
            case 0xBD: bus->w32(Ri & ~3u, res[rj]); Ri += 4; return;          // STRES #u4, @Ri+
            // misc
            case 0x1F: if (in_delay) return bad(); eit(u8, -1, next); return;
            case 0x83: set_ccr(ccr & u8); return;
            case 0x93: set_ccr(ccr | u8); return;
            case 0x87: set_ilm(u8 | 0x10 * (u8 < 16)); return;
            case 0xA3: r[15] += sx(u8, 8) * 4; return;
            case 0x0F: { uint32_t sp = r[15] - 4; bus->w32(sp, r[14]); r[14] = sp; r[15] = r[15] - u8 * 4; return; }
            case 0x8C: { for (int i = 0; i < 8; i++) if (u8 & (1 << i)) { uint32_t v = bus->r32(r[15]); r[15] += 4; r[i] = v; } return; }
            case 0x8D: { for (int i = 0; i < 8; i++) if (u8 & (1 << i)) { uint32_t v = bus->r32(r[15]); r[15] += 4; r[8 + i] = v; } return; }
            case 0x8E: { for (int i = 0; i < 8; i++) if (u8 & (1 << i)) { r[15] -= 4; bus->w32(r[15], r[7 - i]); } return; }
            case 0x8F: { uint32_t orig = r[15]; for (int i = 0; i < 8; i++) if (u8 & (1 << i)) { r[15] -= 4; bus->w32(r[15], i == 0 ? orig : r[15 - i]); } return; }
            // DMOV family (u8 * scale is a direct address in I/O area)
            case 0x08: r[13] = bus->r32(u8 * 4); return;
            case 0x18: bus->w32(u8 * 4, r[13]); return;
            case 0x0C: { uint32_t v = bus->r32(u8 * 4); bus->w32(r[13], v); r[13] += 4; return; }
            case 0x1C: { uint32_t v = bus->r32(r[13]); r[13] += 4; bus->w32(u8 * 4, v); return; }
            case 0x0B: { uint32_t v = bus->r32(u8 * 4); r[15] -= 4; bus->w32(r[15], v); return; }
            case 0x1B: { uint32_t v = bus->r32(r[15]); r[15] += 4; bus->w32(u8 * 4, v); return; }
            case 0x09: r[13] = bus->r16(u8 * 2); return;
            case 0x19: bus->w16(u8 * 2, (uint16_t)r[13]); return;
            case 0x0D: { uint16_t v = bus->r16(u8 * 2); bus->w16(r[13], v); r[13] += 2; return; }
            case 0x1D: { uint16_t v = bus->r16(r[13]); r[13] += 2; bus->w16(u8 * 2, v); return; }
            case 0x0A: r[13] = bus->r8(u8); return;
            case 0x1A: bus->w8(u8, (uint8_t)r[13]); return;
            case 0x0E: { uint8_t v = bus->r8(u8); bus->w8(r[13], v); r[13] += 1; return; }
            case 0x1E: { uint8_t v = bus->r8(r[13]); r[13] += 1; bus->w8(u8, v); return; }
            default: return bad();
        }
    }
};
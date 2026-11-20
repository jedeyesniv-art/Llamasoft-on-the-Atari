// The Atari Falcon030, as Llamazap uses it (included by machine.cpp inside namespace st): VIDEL through Vsetmode
// (8 planes, overscan), the 256-colour palette at $FF9800, the video counter, line offset and fine scroll (split
// screens and a scrolling playfield), the blitter, the Jaguar pad on the enhanced joystick port, the DSP's host
// port and its XBIOS calls (high level: what the 68030 sends is kept for our own mixer), the sound calls.

void Machine::SetFalcon(bool on) {
  falcon_ = on;
  ram_size_ = on ? kFalconRam : kRamSize;
  ram_.assign(ram_size_, 0);
  frame_bytes_ = on ? 8 : 6;  // the 68030's frames carry a format / vector word
  cpu_mul_ = on ? 2 : 1;      // 16 MHz
  speed_ = cpu_mul_;
}

// Vsetmode's mode word: bits 0-2 colours (1 / 2 / 4 / 8 bits, 4 = true colour), 3 80 columns, 4 VGA, 5 PAL,
// 6 overscan (TV: x 1.2 both ways), 7 ST compatible, 8 vertical flag (TV: interlace; VGA: lines doubled).
void Machine::SetVideoMode(uint16_t mode) {
  static const int kBpp[8] = {1, 2, 4, 8, 16, 16, 16, 16};
  vmode_ = mode;
  bpp_ = kBpp[mode & 7];
  const bool col80 = mode & 8, vga = mode & 0x10, overscan = mode & 0x40, st_compat = mode & 0x80, vert = mode & 0x100;
  sync_ = !vga && (mode & 0x20) ? 2 : 0;  // a PAL TV: 50 Hz; NTSC and VGA: 60
  hscroll_ = 0, line_off_ = 0;
  if (st_compat) {
    videl_ = false;
    res_ = bpp_ >= 4 ? 0 : bpp_ == 2 ? 1 : 2;
    frame_w_ = kScreenW, frame_h_ = kScreenH, line_words_ = 80;
  } else {
    int w = col80 ? 640 : 320, h = vga ? (vert ? 240 : 480) : (vert ? 400 : 200);
    if (overscan && !vga) w = w * 6 / 5, h = h * 6 / 5;
    h = std::min(h, kFrameCapacity / w);
    videl_ = true;
    frame_w_ = w, frame_h_ = h;
    line_words_ = uint16_t(w * bpp_ / 16);
  }
  LOG("falcon: Vsetmode $%03X: %dx%d, %d bits a pixel%s, %d Hz", mode, frame_w_, frame_h_, bpp_,
      videl_ ? "" : " (ST compatible)", Is60Hz() ? 60 : 50);
}

void Machine::RenderFalconLine(int y) {
  if (y == 0) frame_base_ = vbase_latched_, frame_hscroll_ = hscroll_;
  if (trace_tos) {  // (tests) the line layout as it changes
    static uint32_t last = ~0u;
    static int logged = 0;
    uint32_t now = uint32_t(line_off_) << 16 | uint32_t(line_words_) << 4 | hscroll_;
    if (now != last && logged < 200) ++logged, LOG("videl: line %d: counter $%X, line words %d, offset %d, hscroll %d", y, vcount_, line_words_, line_off_, hscroll_);
    last = now;
  }
  if (y >= frame_h_) return;
  const int w = frame_w_;
  uint32_t* out = &frame_[size_t(y) * size_t(w)];
  const uint32_t a = vcount_, mask = (ram_size_ - 1) & ~1u;
  auto word = [&](uint32_t off) { return Get16(&ram_[(a + off) & mask]); };
  if (bpp_ == 16) {
    for (int x = 0; x < w; ++x) {
      uint16_t c = word(uint32_t(x + hscroll_) * 2);
      uint32_t r = c >> 11, g = (c >> 5) & 63, b = c & 31;
      out[x] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
    }
  } else {
    uint32_t pal[256];
    const int colours = 1 << bpp_;
    auto ex = [](uint32_t v) { return (v & 0xFC) | (v >> 6 & 3); };
    for (int i = 0; i < colours; ++i) {
      uint32_t c = pal256_[size_t(i)];
      pal[i] = 0xFF000000u | ex(c >> 24) << 16 | ex(c >> 16 & 0xFF) << 8 | ex(c & 0xFF);
    }
    // Planes interleaved by words: 16 pixels = bpp_ words. One group more for the fine scroll.
    static thread_local uint8_t idx[kMaxFrameW + 32];
    const int planes = bpp_, groups = (w + 15) / 16 + 1;
    for (int g = 0; g < groups; ++g) {
      uint16_t p[8];
      for (int k = 0; k < planes; ++k) p[k] = word(uint32_t((g * planes + k) * 2));
      for (int x = 0; x < 16; ++x) {
        int s = 15 - x, v = 0;
        for (int k = 0; k < planes; ++k) v |= ((p[k] >> s) & 1) << k;
        idx[g * 16 + x] = uint8_t(v);
      }
    }
    for (int x = 0; x < w; ++x) out[x] = pal[idx[x + hscroll_]];
    if (clean_margin_ > 0) {  // the clean picture's line (Llamazap's remaster)
      const int m = clean_margin_ & ~15, cw = w + 2 * m;
      if (clean_.size() != size_t(cw) * size_t(frame_h_)) clean_.assign(size_t(cw) * size_t(frame_h_), 0), clean_line_.assign(size_t(frame_h_), 0);
      uint32_t* cout = &clean_[size_t(y) * size_t(cw)];
      const CleanSource* src = nullptr;
      for (const auto& c : clean_src_)
        if (c.hi > c.lo && a >= c.lo && a < c.hi) src = &c;
      clean_line_[size_t(y)] = src ? 1 : 0;
      if (!src) {
        for (int x = 0; x < cw; ++x) cout[x] = out[std::clamp(x - m, 0, w - 1)];
      } else {
        // From the clean buffer, `m` pixels (m / 16 groups) to the left of where the line starts.
        static thread_local uint8_t cidx[kMaxFrameW + 96];
        const uint32_t ca = a + uint32_t(src->shift) - uint32_t(m / 16 * planes * 2);
        const int cgroups = groups + 2 * (m / 16);
        for (int g = 0; g < cgroups && g * 16 + 16 <= int(sizeof(cidx)); ++g) {
          uint16_t p[8];
          for (int k = 0; k < planes; ++k) p[k] = Get16(&ram_[(ca + uint32_t((g * planes + k) * 2)) & mask]);
          for (int x = 0; x < 16; ++x) {
            int s = 15 - x, v = 0;
            for (int k = 0; k < planes; ++k) v |= ((p[k] >> s) & 1) << k;
            cidx[g * 16 + x] = uint8_t(v);
          }
        }
        for (int x = 0; x < cw; ++x) cout[x] = pal[cidx[x + hscroll_]];
      }
    }
  }
  // With a fine scroll the VIDEL fetches one 16-pixel group more a line (programs take it off the line offset:
  // Llamazap's playfield is 128 words over the line, 120 while scrolled).
  const uint32_t extra = hscroll_ && bpp_ < 16 ? uint32_t(bpp_) : 0;
  vcount_ = a + (uint32_t(line_words_) + extra + uint32_t(line_off_)) * 2;
}

namespace {
// The Jaguar pad's matrix, port A: for each of the 4 select rows, the button read on the fire bit 1 (row 0 also
// has Pause on bit 0) and the four read on the direction bits (bits 8-11 of $FF9202).
constexpr uint32_t kJagRowFire[4] = {kJagA, kJagB, kJagC, kJagOption};
constexpr uint32_t kJagRowDirs[4][4] = {{kJagUp, kJagDown, kJagLeft, kJagRight},
                                        {kJagStar, kJagKey0 << 7, kJagKey0 << 4, kJagKey0 << 1},
                                        {kJagKey0, kJagKey0 << 8, kJagKey0 << 5, kJagKey0 << 2},
                                        {kJagHash, kJagKey0 << 9, kJagKey0 << 6, kJagKey0 << 3}};
}  // namespace

bool Machine::FalconIoRead(uint32_t a, uint8_t* v) {
  if (a >= 0xFF9800 && a < 0xFF9C00) return *v = uint8_t(pal256_[(a - 0xFF9800) / 4] >> (8 * (3 - (a & 3)))), true;
  if (a >= 0xFF8A00 && a < 0xFF8A40) return *v = blit_[a - 0xFF8A00], true;
  if (a >= 0xFF8282 && a < 0xFF82C4) return *v = 0, true;  // VIDEL's timing registers
  if (a >= 0xFFA200 && a < 0xFFA208) {  // the DSP's host port: always ready to take a word, never one to give
    *v = a == 0xFFA202 ? 0x06 : 0;      // ISR: TXDE, TRDY
    return true;
  }
  switch (a) {
    case 0xFF9200:
    case 0xFF9203: *v = 0xFF; return true;
    case 0xFF9201:    // fire buttons (active low): port A bit 0 Pause (row 0), bit 1 the row's button
    case 0xFF9202: {  // directions: port A bits 0-3 (of the high byte)
      uint8_t r = 0xFF;
      for (int row = 0; row < 4; ++row) {
        if (jag_select_ >> row & 1) continue;
        if (a == 0xFF9201) {
          if (row == 0 && (jagpad_ & kJagPause)) r &= uint8_t(~1);
          if (jagpad_ & kJagRowFire[row]) r &= uint8_t(~2);
        } else {
          for (int b = 0; b < 4; ++b)
            if (jagpad_ & kJagRowDirs[row][b]) r &= uint8_t(~(1 << b));
        }
      }
      *v = r;
      return true;
    }
    case 0xFF8205: *v = uint8_t(vcount_ >> 16); return true;
    case 0xFF8207: *v = uint8_t(vcount_ >> 8); return true;
    case 0xFF8209: *v = uint8_t(vcount_); return true;
    case 0xFF820D: *v = vbase_lo_; return true;
    case 0xFF820E: *v = uint8_t(line_off_ >> 8); return true;
    case 0xFF820F: *v = uint8_t(line_off_); return true;
    case 0xFF8210: *v = uint8_t(line_words_ >> 8); return true;
    case 0xFF8211: *v = uint8_t(line_words_); return true;
    case 0xFF8264: *v = 0; return true;
    case 0xFF8265: *v = hscroll_; return true;
    case 0xFF8006: *v = 0x40; return true;  // an RGB monitor (a TV through SCART)
    case 0xFF8007: *v = 0; return true;
    default: return false;
  }
}

bool Machine::FalconIoWrite(uint32_t a, uint8_t v) {
  if (a >= 0xFF9800 && a < 0xFF9C00) {
    uint32_t& c = pal256_[(a - 0xFF9800) / 4];
    int sh = 8 * (3 - int(a & 3));
    c = (c & ~(0xFFu << sh)) | uint32_t(v) << sh;
    return true;
  }
  if (a >= 0xFF8A00 && a < 0xFF8A40) {
    blit_[a - 0xFF8A00] = v;
    if (a == 0xFF8A3C && (v & 0x80)) Blit();  // (a word write to $FF8A3C sets the skew first: Write16)
    return true;
  }
  if (a >= 0xFF8282 && a < 0xFF82C4) return true;
  if (a >= 0xFFA200 && a < 0xFFA208) {
    if (a >= 0xFFA204) dsp_tx_[a - 0xFFA204] = v;
    if (a == 0xFFA207) {
      dsp_packet_.push_back(uint32_t(dsp_tx_[1]) << 16 | uint32_t(dsp_tx_[2]) << 8 | v);
      if (dsp_packet_.size() == 7 * 8) DspPacket();
    }
    return true;
  }
  switch (a) {
    case 0xFF9202: return true;
    case 0xFF9203: jag_select_ = v; return true;
    case 0xFF8205: vcount_ = (vcount_ & 0x00FFFF) | uint32_t(v & 0x3F) << 16; return true;
    case 0xFF8207: vcount_ = (vcount_ & 0xFF00FF) | uint32_t(v) << 8; return true;
    case 0xFF8209: vcount_ = (vcount_ & 0xFFFF00) | (v & 0xFE); return true;
    case 0xFF820D: vbase_lo_ = v & 0xFE; return true;
    case 0xFF820E: line_off_ = uint16_t((line_off_ & 0xFF) | (v & 1) << 8); return true;
    case 0xFF820F: line_off_ = uint16_t((line_off_ & 0x100) | v); return true;
    case 0xFF8210: line_words_ = uint16_t((line_words_ & 0xFF) | (v & 3) << 8); return true;
    case 0xFF8211: line_words_ = uint16_t((line_words_ & 0x300) | v); return true;
    case 0xFF8264: return true;
    case 0xFF8265: hscroll_ = v & 15; return true;
    case 0xFF8006:
    case 0xFF8007: return true;
    case 0xFF8260:
    case 0xFF8266:
    case 0xFF8267: return videl_;  // (the mode is Vsetmode's)
    default: return false;
  }
}

// The blitter, a whole job at once: x count words a line (end masks on the first and last), y count lines, the
// source through the skew and the halftone (HOP), combined with the destination (OP).
void Machine::Blit() {
  auto r16 = [&](int o) { return uint16_t(blit_[o] << 8 | blit_[o + 1]); };
  auto w16 = [&](int o, uint32_t x) { blit_[o] = uint8_t(x >> 8), blit_[o + 1] = uint8_t(x); };
  const int32_t sxi = int16_t(r16(0x20)), syi = int16_t(r16(0x22)), dxi = int16_t(r16(0x2E)), dyi = int16_t(r16(0x30));
  uint32_t src = (uint32_t(r16(0x24)) << 16 | r16(0x26)) & 0xFFFFFE, dst = (uint32_t(r16(0x32)) << 16 | r16(0x34)) & 0xFFFFFE;
  const uint16_t em1 = r16(0x28), em2 = r16(0x2A), em3 = r16(0x2C);
  const int xcount = r16(0x36) ? r16(0x36) : 65536;
  int ycount = r16(0x38) ? r16(0x38) : 65536;
  const int hop = blit_[0x3A] & 3, op = blit_[0x3B] & 15;
  const uint8_t ctrl = blit_[0x3C], sk = blit_[0x3D];
  int line = ctrl & 15;
  const bool smudge = ctrl & 0x20, fxsr = sk & 0x80, nfsr = sk & 0x40;
  const int skew = sk & 15;
  if (g_blit_watch) g_blit_watch(dst, src, xcount, ycount, cpu::Get(cpu::PPC));
  if (trace_tos) {
    static int logged = 0;
    if (logged++ < 60)
      LOG("blit: src $%X (%d, %d) dst $%X (%d, %d) %d x %d hop %d op %d ctrl $%02X skew $%02X masks %04X %04X %04X pc $%X",
          src, sxi, syi, dst, dxi, dyi, xcount, ycount, hop, op, ctrl, sk, em1, em2, em3, cpu::Get(cpu::PPC));
  }
  uint32_t buffer = 0;
  auto shift = [&]() { buffer = sxi < 0 ? buffer >> 16 : buffer << 16; };
  auto fetch = [&]() {
    uint32_t w = Read16(src);
    buffer |= sxi < 0 ? w << 16 : w;
  };
  for (; ycount > 0; --ycount) {
    for (int x = xcount; x > 0; --x) {
      const bool first = x == xcount, last = x == 1;
      if (first && fxsr) shift(), fetch(), src += uint32_t(sxi);
      shift();
      if (!(last && nfsr)) fetch(), src += uint32_t(last ? syi : sxi);
      else src += uint32_t(syi - sxi);  // no final read: nor the step to it (the y step replaces the last x step)
      const uint16_t s = uint16_t(buffer >> skew);
      const uint16_t ht = r16(int(smudge ? (s & 15) : line) * 2);
      const uint16_t sw = hop == 0 ? 0xFFFF : hop == 1 ? ht : hop == 2 ? s : uint16_t(s & ht);
      const uint16_t d = uint16_t(Read16(dst));
      uint16_t res = 0;
      switch (op) {
        case 0: res = 0; break;
        case 1: res = sw & d; break;
        case 2: res = sw & ~d; break;
        case 3: res = sw; break;
        case 4: res = ~sw & d; break;
        case 5: res = d; break;
        case 6: res = sw ^ d; break;
        case 7: res = sw | d; break;
        case 8: res = ~sw & ~d; break;
        case 9: res = ~sw ^ d; break;
        case 10: res = ~d; break;
        case 11: res = sw | ~d; break;
        case 12: res = ~sw; break;
        case 13: res = ~sw | d; break;
        case 14: res = ~sw | ~d; break;
        default: res = 0xFFFF; break;
      }
      const uint16_t m = first ? em1 : last ? em3 : em2;
      Write16(dst, uint16_t((d & ~m) | (res & m)));
      dst += uint32_t(last ? dyi : dxi);
    }
    line = (line + (dyi >= 0 ? 1 : -1)) & 15;
  }
  w16(0x24, src >> 16), w16(0x26, src), w16(0x32, dst >> 16), w16(0x34, dst), w16(0x38, 0);
  blit_[0x3C] = uint8_t((ctrl & 0x70) | line);  // not busy
}

// ---- The DSP's mixer (high level) ----
// Llamazap's DSP program mixes 7 sample voices - 6 for the music (8-bit: MODs, the 68030 plays the module and sends
// each voice's state), the last for the effects (4-bit, LLAMAFX.BIN: 2 a byte, high nibble first, silence = 8) -
// and plays them through the DAC (Devconnect: 24.6 kHz).
// Twice a frame the 68030 sends a packet through the host port: for each voice 8 words - flags (bit 0 on, bit 1
// start the sample now), its address in DSP memory (3 samples a word; the music's from $300, the effects' from
// $4803), which byte of the word (as a shift: music 16 / 8 / 0 - the byte to the top; effects 20 / 12 / 4 - the
// high nibble's bit), the length in samples, the pitch (the ProTracker period x 450.11), left and right volume
// (0..64), one unused. Started, a voice plays to its end, then goes on from the address and length the latest packet
// holds (the loop: as the Amiga). Music words hold 3 samples, the first lowest (the 68030 swapped each 3 bytes);
// effects words 6, the first highest.

int Machine::DspByte(bool fx, uint32_t k) const {  // sample k of a region, -128..127
  uint32_t a = fx ? 0x4803u + k / 6 : 0x300u + k / 3;
  if (a >= dsp_mem_.size()) return 0;
  uint32_t w = dsp_mem_[a];
  if (fx) return (int((w >> (20 - 4 * (k % 6))) & 15) - 8) * 16;
  return int(int8_t(uint8_t(w >> (8 * (k % 3)))));
}

void Machine::DspPacket() {
  Sync();  // the sound so far with the voices as they were
  constexpr double kPaulaHz = 3546895.0, kPeriodScale = 450.11;
  for (size_t i = 0; i < dsp_voice_.size(); ++i) {
    const uint32_t* w = &dsp_packet_[i * 8];
    DspVoice& v = dsp_voice_[i];
    v.fx = i + 1 == dsp_voice_.size();
    const uint32_t base = v.fx ? 0x4803 : 0x300, shift = w[2] & 0xFF;
    const uint32_t r = std::min<uint32_t>(2, v.fx ? (20 - std::min<uint32_t>(shift, 20)) / 8 : 2 - std::min<uint32_t>(shift, 16) / 8);
    const uint32_t word = std::max(w[1], base) - base;
    const double start = v.fx ? double(word * 6 + r * 2) : double(word * 3 + r), len = double(w[3]);
    v.next = start, v.next_len = len;
    if (w[0] & 2) v.pos = start, v.end = start + len;  // the sample starts now
    v.on = w[0] & 1;
    const uint32_t pitch = w[4];
    v.step = pitch ? kPaulaHz * kPeriodScale / double(pitch) / sample_rate_ : 0;
    v.left = float(std::min<uint32_t>(w[5] & 0xFFFF, 64)) / 64, v.right = float(std::min<uint32_t>(w[6] & 0xFFFF, 64)) / 64;
  }
  dsp_packet_.clear();
}

void Machine::MixDsp(size_t from) {
  for (DspVoice& v : dsp_voice_) {
    if (!v.on || v.step <= 0 || (v.left == 0 && v.right == 0)) continue;
    const float gain = (v.left + v.right) * 0.5f * 0.22f;
    for (size_t i = from; i < audio_.size(); ++i) {
      if (v.pos >= v.end) {
        if (v.next_len < 1) break;
        v.pos = v.next + std::fmod(v.pos - v.end, v.next_len), v.end = v.next + v.next_len;
      }
      audio_[i] += float(DspByte(v.fx, uint32_t(v.pos))) / 128.0f * gain;
      v.pos += v.step;
    }
  }
}

// The Falcon's XBIOS: video, the DSP and the sound system. false = not one of them (the ST's then).
bool Machine::FalconXbios(uint32_t args, uint32_t* d0, bool* handled) {
  const uint8_t* p = &ram_[args];
  const uint16_t fn = Get16(p);
  *handled = true;
  *d0 = 0;
  auto first = [&](int code) { return gem_seen_.insert(0x60000 + code).second; };
  switch (fn) {
    case 5: {  // Setscreen(log, phys, rez, mode): rez 3 = mode is a Vsetmode word
      if (int16_t(Get16(p + 10)) != 3) return false;
      uint32_t log = Get32(p + 2), phys = Get32(p + 6);
      if (log != 0xFFFFFFFF && log) logbase_ = log & 0xFFFFFF;
      if (phys != 0xFFFFFFFF && phys) vbase_hi_ = uint8_t(phys >> 16), vbase_mid_ = uint8_t(phys >> 8), vbase_lo_ = uint8_t(phys);
      SetVideoMode(Get16(p + 12));
      return true;
    }
    case 88: {  // Vsetmode(mode): the old one; -1 = ask
      *d0 = vmode_;
      int16_t mode = int16_t(Get16(p + 2));
      if (mode != -1) SetVideoMode(uint16_t(mode));
      return true;
    }
    case 89: *d0 = 1; return true;  // mon_type: RGB
    case 91: {                      // VgetSize(mode): bytes a screen
      uint16_t keep = vmode_;
      int w = frame_w_, h = frame_h_, b = bpp_;
      bool v = videl_;
      SetVideoMode(Get16(p + 2));
      *d0 = uint32_t(frame_w_ * frame_h_ * bpp_ / 8);
      vmode_ = keep, frame_w_ = w, frame_h_ = h, bpp_ = b, videl_ = v;
      return true;
    }
    case 64: *d0 = 3; return true;  // Blitmode(-1): a blitter, used
    case 23: *d0 = uint32_t((2026 - 1980) << 25 | 11 << 21 | 19 << 16 | 12 << 11); return true;  // Gettime
    case 22:                                                                                    // Settime
    case 24: return true;                                                                       // Bioskeys
    // The DSP. Blocks the 68030 sends through the XBIOS: Llamazap's program takes two parameter words, then
    // its samples (the MOD's, then the effects') into its memory from $300.
    case 96:    // Dsp_DoBlock(in, size_in, out, size_out)
    case 97:    // Dsp_BlkHandShake
    case 123: {  // Dsp_BlkWords (16-bit words)
      uint32_t in = Get32(p + 2) & 0xFFFFFF, n = Get32(p + 6);
      auto word = [&](uint32_t i) {
        return fn == 123 ? uint32_t(int32_t(int16_t(Get16(&ram_[in + i * 2])))) & 0xFFFFFF
                         : uint32_t(ram_[in + i * 3]) << 16 | uint32_t(ram_[in + i * 3 + 1]) << 8 | ram_[in + i * 3 + 2];
      };
      if (n == 1 && dsp_params_ < 2) {
        ++dsp_params_;
        if (!sandbox) LOG("dsp: parameter %06X", word(0));
        return true;
      }
      uint32_t at = dsp_load_;
      for (uint32_t i = 0; i < n && in + i * 3 + 2 < ram_size_ && dsp_load_ < dsp_mem_.size(); ++i)
        dsp_mem_[dsp_load_++] = word(i);
      if (!sandbox) LOG("dsp: %u words loaded at $%X (XBIOS %d from $%X)", n, at, fn, in);
      return true;
    }
    case 103: *d0 = 3; return true;  // Dsp_GetWordSize
    case 104:                        // Dsp_Lock
    case 105:                        // Dsp_Unlock
    case 107:                        // Dsp_Reserve
    case 102:                        // Dsp_RemoveInterrupts
    case 115:                        // Dsp_FlushSubroutines
    case 119: case 120: case 121: case 122: case 125:  // Dsp_Hf0..3, Dsp_HStat
      if (first(fn)) LOG("dsp: XBIOS %d", fn);
      return true;
    case 106: {  // Dsp_Available(&xavail, &yavail)
      Put32(&ram_[Get32(p + 2) & 0xFFFFFF], 0x3EFF), Put32(&ram_[Get32(p + 6) & 0xFFFFFF], 0x3EFF);
      return true;
    }
    case 109: {  // Dsp_ExecProg(code, words, ability)
      uint32_t code = Get32(p + 2) & 0xFFFFFF, n = Get32(p + 6);
      if (!sandbox) LOG("dsp: Dsp_ExecProg($%X, %u words) - our mixer stands in for it", code, n);
      Sync();
      dsp_load_ = 0x300, dsp_params_ = 0, dsp_packet_.clear(), dsp_voice_ = {}, dsp_running_ = true;
      return true;
    }
    case 113: *d0 = ++dsp_ability_; return true;  // Dsp_RequestUniqueAbility
    case 114: *d0 = dsp_ability_; return true;    // Dsp_GetProgAbility
    // The sound system (the DSP plays through the DAC: Devconnect).
    case 128: *d0 = 1; return true;  // Locksnd
    case 129: case 130: case 131: case 132: case 133: case 134: case 135: case 136: case 137: case 138: case 140:
    case 141:
      if (first(fn)) LOG("sound: XBIOS %d (%04X %04X %04X)", fn, Get16(p + 2), Get16(p + 4), Get16(p + 6));
      return true;
    case 139:  // Devconnect(source, destination, clock, prescale, protocol)
      LOG("sound: Devconnect(%d, %d, clock %d, prescale %d, protocol %d)", Get16(p + 2), Get16(p + 4), Get16(p + 6),
          Get16(p + 8), Get16(p + 10));
      return true;
    default: return false;
  }
}

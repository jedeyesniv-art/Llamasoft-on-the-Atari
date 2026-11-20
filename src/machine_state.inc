// Savestates: the machine's state as bytes (included by machine.cpp). One field list (StateFields) serves both
// ways: a writer appends each field, a reader fills it back. Containers carry their size; plain structs go as
// their bytes (the same exe reads them: the file's version and the machine's sizes are checked first).

namespace state_io {

template <class T>
constexpr bool kRaw = std::is_trivially_copyable_v<T>;

struct Writer {
  std::vector<uint8_t>* out;
  static constexpr bool kReading = false;
  void Bytes(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    out->insert(out->end(), b, b + n);
  }
  void Size(size_t n) {
    uint64_t v = n;
    Bytes(&v, sizeof v);
  }
};

struct Reader {
  const uint8_t* p;
  size_t n, at = 0;
  bool ok = true;
  static constexpr bool kReading = true;
  void Bytes(void* dst, size_t len) {
    if (!ok || at + len > n) {
      ok = false;
      return;
    }
    std::memcpy(dst, p + at, len);
    at += len;
  }
  size_t Size() {
    uint64_t v = 0;
    Bytes(&v, sizeof v);
    if (v > (1u << 28)) ok = false;  // (a damaged file: no container is that big)
    return ok ? size_t(v) : 0;
  }
};

template <class Io, class T>
void F(Io& io, T& v);

template <class Io>
void F(Io& io, std::string& s) {
  if constexpr (Io::kReading) {
    s.resize(io.Size());
    if (!s.empty()) io.Bytes(s.data(), s.size());
  } else {
    io.Size(s.size());
    io.Bytes(s.data(), s.size());
  }
}

template <class Io, class T>
void F(Io& io, std::vector<T>& v) {
  if constexpr (Io::kReading) v.resize(io.Size());
  else io.Size(v.size());
  if constexpr (kRaw<T>) {
    if (!v.empty()) io.Bytes(v.data(), v.size() * sizeof(T));
  } else {
    for (T& e : v) F(io, e);
  }
}

template <class Io, class T>
void F(Io& io, std::deque<T>& d) {
  if constexpr (Io::kReading) d.resize(io.Size());
  else io.Size(d.size());
  for (T& e : d) F(io, e);
}

template <class Io, class K, class V>
void F(Io& io, std::map<K, V>& m) {
  if constexpr (Io::kReading) {
    size_t n = io.Size();
    m.clear();
    for (size_t i = 0; i < n && io.ok; ++i) {
      K k{};
      V v{};
      F(io, k), F(io, v);
      m.emplace(std::move(k), std::move(v));
    }
  } else {
    io.Size(m.size());
    for (auto& [k, v] : m) {
      K key = k;
      F(io, key), F(io, v);
    }
  }
}

template <class Io, class K>
void F(Io& io, std::set<K>& s) {
  if constexpr (Io::kReading) {
    size_t n = io.Size();
    s.clear();
    for (size_t i = 0; i < n && io.ok; ++i) {
      K k{};
      F(io, k);
      s.insert(std::move(k));
    }
  } else {
    io.Size(s.size());
    for (K k : s) F(io, k);
  }
}

template <class Io, class T>
void F(Io& io, T& v) {
  if constexpr (requires { v.State(io); }) {
    v.State(io);  // a struct with containers in it lists its own fields
  } else {
    static_assert(kRaw<T>, "a field the state can't write as bytes: give it an overload or a State()");
    io.Bytes(&v, sizeof v);
  }
}

}  // namespace state_io

// Every member that makes up the machine's state, in one place. Left out on purpose: hooks_ / hooks_pending_ (the
// live ones are kept), files_ / save_dir_ (the live ones), ram_ (written separately: without the hooks' patches),
// audio_ / audio_side_ (cleared), sample_rate_ / dsp_fx_gain_ / dsp_stereo_ (settings), io_logged_ / gem_seen_
// (logging), falcon_ / ram_size_ (checked against the file's header), the public flags (sandbox, ahead, draw_in_sandbox, trace_tos). Tools/state_fields.py checks that
// every member of machine.h is either here or in that list.
template <class Io>
void Machine::StateFields(Io& io) {
  using state_io::F;
  // Console, GEM / VDI.
  F(io, con_x_), F(io, con_y_), F(io, con_esc_), F(io, con_row_), F(io, con_fg_), F(io, con_bg_), F(io, con_sx_);
  F(io, con_sy_), F(io, con_rev_), F(io, con_wrap_), F(io, logbase_), F(io, gem_mouse_x_), F(io, gem_mouse_y_);
  F(io, vdi_text_colour_), F(io, vdi_wmode_), F(io, vdi_halign_), F(io, vdi_valign_), F(io, vdi_font_);
  // Processes and memory.
  F(io, parents_), F(io, cur_bp_), F(io, cur_end_), F(io, malloc_top_), F(io, mallocs_), F(io, trap_wait_);
  F(io, dta_), F(io, search_);
  // The Falcon.
  F(io, frame_bytes_), F(io, cpu_mul_), F(io, videl_), F(io, vmode_), F(io, bpp_), F(io, vbase_lo_), F(io, vcount_);
  F(io, line_off_), F(io, line_words_), F(io, hscroll_), F(io, pal256_), F(io, clean_src_), F(io, clean_margin_);
  F(io, frame_hscroll_), F(io, clean_), F(io, clean_line_), F(io, blit_), F(io, jag_select_), F(io, jagpad_);
  F(io, dsp_tx_), F(io, dsp_ability_), F(io, dsp_mem_), F(io, dsp_mix_), F(io, dsp_lp_), F(io, dsp_lp_side_);
  F(io, dsp_mix_side_), F(io, dsp_load_), F(io, dsp_params_), F(io, dsp_packet_), F(io, dsp_voice_);
  F(io, dsp_running_);
  // ROM, the picture, timing.
  F(io, rom_), F(io, frame_), F(io, frame_w_), F(io, frame_h_), F(io, frame_res_), F(io, speed_), F(io, cpu_rem_);
  F(io, swaps_), F(io, vbls_taken_), F(io, frame_start_), F(io, cycles_done_), F(io, audio_synced_), F(io, ended_);
  // MFP, VBL.
  F(io, mfp_), F(io, timers_), F(io, line_), F(io, vbl_pending_);
  // ACIA / IKBD, TOS's keyboard.
  F(io, ikbd_out_), F(io, acia_next_), F(io, acia_data_), F(io, acia_ctrl_), F(io, acia_rx_full_), F(io, ikbd_cmd_);
  F(io, joy_events_), F(io, joy_), F(io, mouse_on_), F(io, mouse_y_up_), F(io, mouse_thr_x_), F(io, mouse_thr_y_);
  F(io, mouse_dx_), F(io, mouse_dy_), F(io, mouse_buttons_), F(io, mouse_sent_buttons_), F(io, packet_);
  F(io, packet_need_), F(io, packet_joy_), F(io, console_keys_), F(io, modifiers_);
  // Shifter, STE.
  F(io, vbase_hi_), F(io, vbase_mid_), F(io, res_), F(io, sync_), F(io, palette_), F(io, line_palette_);
  F(io, ste_snd_), F(io, dma_on_), F(io, dma_pos_), F(io, dma_end_), F(io, ste_colours_), F(io, ste_);
  F(io, vbase_latched_), F(io, frame_base_), F(io, calm_), F(io, calm_rgb_), F(io, calm_hist_), F(io, calm_at_);
  F(io, calm_hold_), F(io, calm_cap_), F(io, calm_release_);
  // YM2149.
  static_assert(std::is_trivially_copyable_v<Ym2149>);
  F(io, ym_), F(io, ym_sel_);
  // TOS's files (open handles: the files themselves are the live ones), the rest.
  F(io, handles_), F(io, kbdvbase_), F(io, random_);
}

namespace {
constexpr uint32_t kMachineStateVersion = 1;
}

void Machine::Hook(uint32_t addr, std::function<void(Machine&)> fn) {
  HookEntry e;
  e.fn = std::move(fn);
  auto old = hooks_.find(addr);
  const bool patched = addr + 8 <= ram_size_ && ram_[addr] == 0x4A && ram_[addr + 1] == 0xFC;
  if (patched && old != hooks_.end() && old->second.known) {
    std::memcpy(e.orig, old->second.orig, sizeof e.orig);  // (hooked again: the instruction is the one kept)
    e.known = true;
  } else if (!patched && addr + 8 <= ram_size_) {
    std::memcpy(e.orig, &ram_[addr], sizeof e.orig);
    e.known = true;
  }
  if (addr + 2 <= ram_size_) ram_[addr] = 0x4A, ram_[addr + 1] = 0xFC;
  hooks_[addr] = std::move(e);
  hooks_pending_.erase(addr);
}

void Machine::PlacePendingHooks() {
  for (auto it = hooks_pending_.begin(); it != hooks_pending_.end();) {
    auto h = hooks_.find(*it);
    if (h == hooks_.end()) {
      it = hooks_pending_.erase(it);
    } else if (std::memcmp(&ram_[*it], h->second.orig, sizeof h->second.orig) == 0) {
      ram_[*it] = 0x4A, ram_[*it + 1] = 0xFC;
      it = hooks_pending_.erase(it);
    } else {
      ++it;
    }
  }
}

void Machine::SaveState(std::vector<uint8_t>* out) {
  state_io::Writer w{out};
  uint32_t version = kMachineStateVersion, ram_size = ram_size_;
  uint8_t falcon = falcon_ ? 1 : 0;
  w.Bytes(&version, sizeof version), w.Bytes(&ram_size, sizeof ram_size), w.Bytes(&falcon, 1);
  // RAM without the hooks' patches (each hooked instruction's first word as it was).
  const size_t ram_at = out->size();
  w.Bytes(ram_.data(), ram_size_);
  for (const auto& [addr, h] : hooks_)
    if (h.known && addr + 2 <= ram_size_ && ram_[addr] == 0x4A && ram_[addr + 1] == 0xFC)
      (*out)[ram_at + addr] = h.orig[0], (*out)[ram_at + addr + 1] = h.orig[1];
  StateFields(w);
}

bool Machine::LoadState(const std::vector<uint8_t>& in, std::string* error) {
  state_io::Reader r{in.data(), in.size()};
  uint32_t version = 0, ram_size = 0;
  uint8_t falcon = 0;
  r.Bytes(&version, sizeof version), r.Bytes(&ram_size, sizeof ram_size), r.Bytes(&falcon, 1);
  if (!r.ok || version != kMachineStateVersion) return *error = "made by another version", false;
  if (ram_size != ram_size_ || (falcon != 0) != falcon_) return *error = "made on another machine", false;
  // Into a copy first: a damaged file changes nothing.
  Machine copy = *this;
  r.Bytes(copy.ram_.data(), ram_size_);
  copy.StateFields(r);
  if (!r.ok || r.at != r.n) return *error = "damaged", false;
  copy.audio_.clear(), copy.audio_side_.clear();
  *this = std::move(copy);
  // The hooks back where their instructions are (the rest when they are).
  hooks_pending_.clear();
  for (const auto& [addr, h] : hooks_) {
    if (!h.known || addr + 8 > ram_size_) continue;
    if (std::memcmp(&ram_[addr], h.orig, sizeof h.orig) == 0) ram_[addr] = 0x4A, ram_[addr + 1] = 0xFC;
    else hooks_pending_.insert(addr);
  }
  return true;
}

template <class Io>
void Machine::OpenFile::State(Io& io) {
  using state_io::F;
  F(io, name), F(io, pos), F(io, written);
}

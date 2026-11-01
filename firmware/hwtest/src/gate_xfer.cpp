// G-A1: sustained HWCDC throughput (docs/gates/m3-device-gates.md).
//   !xfer <b64|bin> <len> <depth> <txring KB> <ram|sd|sdkeep> [secs]      (defaults: secs 10)
//       Frames shaped like the app's file.data line (11 §5.3; bin = header line, raw bytes,
//       "\n"), each written only when the whole frame fits in the TX ring (11 §5.4), with
//       the app's 20 ms kick + re-arm (UsbTransport::tick). depth 0 pushes freely; depth N
//       sends one frame per "!g" line and the host keeps N requests in flight. src ram: a
//       pattern in RAM; sd: /sd/cpa_hwtest/xfer.bin, opened, read and closed per frame (the
//       audio task's one-open-file rule, 11 §4.5); sdkeep: the same file opened once and
//       read sequentially (still one open file). Meanwhile "!ping <id>" -> pong (ahead of
//       the next frame), "!stop" ends early. Busy % per core comes from idle-hook counts
//       against a 500 ms unloaded reference. CRC-32/IEEE from the ROM table routine
//       (zlib-compatible); the bitwise sdt::crc32 costs ~0.35 ms per 1380 B frame. Base64 is
//       the plain table encoder of gate_core: mbedtls_base64_encode (constant-time) costs
//       ~1.6 ms more; with both, the first run was CPU-bound (docs/gates/m3-device-gates.md §3.2).
//       -> {"t":"xfer_start"}, frames, {"t":"xs"} every second, {"t":"xfer_end"}
//   !xfermk <KB> [pace_ms] [sync_KB]   writes /sd/cpa_hwtest/xfer.bin with a pattern in 16 KB
//                  writes, pausing pace_ms after each and fsyncing every sync_KB (0 KB deletes
//                  it) -> {"t":"xfermk",wrote,errno}
//   !gateclean     deletes only what the gates write (xfer.bin, mic.wav) and /sd/cpa_hwtest
//                  when it is then empty; never touches /cpa -> {"t":"gateclean"}
#include <M5Cardputer.h>
#include <esp_freertos_hooks.h>
#include <esp_rom_crc.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "gate_cmds.h"
#include "gate_core.h"
#include "sd_common.h"

namespace {

using gate::Enc;
using gate::Src;

constexpr const char* kXferPath = "/sd/cpa_hwtest/xfer.bin";
constexpr size_t kSrcWindow = 12288;  // RAM payloads start below 12 KB, so len <= 4 KB fits
constexpr size_t kSrcBytes = 16384;   // scratch [0, 16K): RAM pattern / SD read buffer
constexpr size_t kFrameCap = 8192;    // scratch [16K, 24K): one whole frame
constexpr uint32_t kSegSize = 9600044;  // file.data "size" of a 5-minute segment

volatile uint32_t s_idle[2] = {0, 0};
bool idleHook0() { s_idle[0] = s_idle[0] + 1; return false; }  // false: keep calling, no WAITI
bool idleHook1() { s_idle[1] = s_idle[1] + 1; return false; }

struct Sec {
  uint32_t frames, bytes, wait_us, cred_us, work_us, rd_max_us;
};

struct Run {
  gate::XferArgs a;
  int empty_free;
  uint32_t credits, seq, src_off, src_size, t0, ref[2], idle_at[2];
  uint32_t frames, bytes, pings, rearms, over_credit, rd_fail, sec_idx, sec_t0;
  int fd;          // sdkeep: the open source file
  size_t pending;  // bytes of a built frame waiting for ring space
  bool stop;
  Sec sec;
  char pong[64];   // sent first
  size_t pong_n;
  char prio[200];  // a stats line, sent ahead of the next frame
  size_t prio_n;
};

Run g_run;

uint8_t* frameBuf() { return sdt::scratch() + kSrcBytes; }

void handleLine(Run& r, const char* s) {
  if (!std::strncmp(s, "!g", 2)) {
    if (r.a.depth == 0 || r.credits >= r.a.depth) r.over_credit++;
    else r.credits++;
  } else if (!std::strncmp(s, "!ping", 5) && r.pong_n == 0) {
    r.pings++;
    r.pong_n = snprintf(r.pong, sizeof r.pong, "{\"t\":\"pong\",\"id\":%ld,\"up_ms\":%lu}\n", atol(s + 5),
                        static_cast<unsigned long>(millis()));
  } else if (!std::strcmp(s, "!stop")) {
    r.stop = true;
  }
}

void serviceRx(Run& r) {
  static char line[40];
  static size_t n = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c != '\n') {
      if (n + 1 < sizeof line) line[n++] = c;
      continue;
    }
    line[n] = '\0';
    n = 0;
    handleLine(r, line);
  }
}

bool readSd(Run& r, uint8_t* dst) {
  const bool wrap = r.src_off + r.a.len > r.src_size;
  if (wrap) r.src_off = 0;
  const uint32_t t = micros();
  const bool keep = r.a.src == Src::SdKeep;
  const int fd = keep ? r.fd : open(kXferPath, O_RDONLY);
  const bool seek = !keep || wrap;
  const bool ok = fd >= 0 && (!seek || lseek(fd, r.src_off, SEEK_SET) == static_cast<off_t>(r.src_off)) &&
                  read(fd, dst, r.a.len) == static_cast<ssize_t>(r.a.len);
  if (fd >= 0 && !keep) close(fd);
  r.sec.rd_max_us = std::max(r.sec.rd_max_us, static_cast<uint32_t>(micros() - t));
  r.src_off += r.a.len;
  if (!ok) r.rd_fail++;
  return ok;
}

const uint8_t* payload(Run& r) {
  uint8_t* src = sdt::scratch();
  if (r.a.src == Src::Ram) return src + (r.seq * r.a.len) % kSrcWindow;
  return readSd(r, src) ? src : nullptr;
}

size_t buildFrame(Run& r, const uint8_t* p) {
  uint8_t* f = frameBuf();
  const gate::FrameMeta m = {r.seq, r.bytes, r.a.len, kSegSize, esp_rom_crc32_le(0, p, r.a.len)};
  size_t n = gate::frame_head(reinterpret_cast<char*>(f), kFrameCap, r.a.enc, m);
  if (n == 0) return 0;
  if (r.a.enc == Enc::B64) {
    const size_t olen = gate::b64_encode(p, r.a.len, reinterpret_cast<char*>(f + n), kFrameCap - n - 3);
    if (olen == 0) return 0;
    n += olen;
    memcpy(f + n, "\"}\n", 3);
    return n + 3;
  }
  memcpy(f + n, p, r.a.len);
  f[n + r.a.len] = '\n';
  return n + r.a.len + 1;
}

// One unit of progress: a pong, else the stats line, else a frame. False when blocked.
bool pump(Run& r) {
  if (r.pong_n) {
    if (!gate_io::writeWhole(r.pong, r.pong_n)) return false;
    r.pong_n = 0;
    return true;
  }
  if (r.prio_n) {
    if (!gate_io::writeWhole(r.prio, r.prio_n)) return false;
    r.prio_n = 0;
    return true;
  }
  if (r.pending == 0) {
    if (r.a.depth && r.credits == 0) return false;
    const uint32_t t = micros();
    const uint8_t* p = payload(r);
    r.pending = p ? buildFrame(r, p) : 0;
    r.sec.work_us += micros() - t;
    if (r.pending == 0) return false;
  }
  if (!gate_io::writeWhole(reinterpret_cast<const char*>(frameBuf()), r.pending)) return false;
  r.pending = 0;
  r.seq++;
  r.frames++;
  r.bytes += r.a.len;
  r.sec.frames++;
  r.sec.bytes += r.a.len;
  if (r.a.depth) r.credits--;
  return true;
}

void secondTick(Run& r) {
  if (millis() - r.sec_t0 < 1000 || r.prio_n) return;
  const uint32_t i0 = s_idle[0], i1 = s_idle[1];
  const uint32_t span = millis() - r.sec_t0;
  const uint32_t ref0 = r.ref[0] * span / 1000, ref1 = r.ref[1] * span / 1000;
  r.prio_n = snprintf(
      r.prio, sizeof r.prio,
      "{\"t\":\"xs\",\"s\":%u,\"ms\":%u,\"frames\":%u,\"bytes\":%u,\"wait_ms\":%u,\"cred_ms\":%u,\"work_ms\":%u,"
      "\"rd_max_us\":%u,\"cpu0\":%u,\"cpu1\":%u}\n",
      (unsigned)r.sec_idx, (unsigned)span, (unsigned)r.sec.frames, (unsigned)r.sec.bytes,
      (unsigned)(r.sec.wait_us / 1000), (unsigned)(r.sec.cred_us / 1000), (unsigned)(r.sec.work_us / 1000),
      (unsigned)r.sec.rd_max_us, gate::busy_pct(i0 - r.idle_at[0], ref0), gate::busy_pct(i1 - r.idle_at[1], ref1));
  r.idle_at[0] = i0;
  r.idle_at[1] = i1;
  r.sec = Sec{0, 0, 0, 0, 0, 0};
  r.sec_idx++;
  r.sec_t0 = millis();
}

void blockTick(Run& r) {
  const bool starved = r.pong_n == 0 && r.prio_n == 0 && r.pending == 0 && r.a.depth && r.credits == 0;
  const uint32_t t = micros();
  vTaskDelay(1);
  (starved ? r.sec.cred_us : r.sec.wait_us) += micros() - t;
}

void calibrate(Run& r) {
  esp_register_freertos_idle_hook_for_cpu(idleHook0, 0);
  esp_register_freertos_idle_hook_for_cpu(idleHook1, 1);
  const uint32_t a0 = s_idle[0], a1 = s_idle[1];
  vTaskDelay(pdMS_TO_TICKS(500));
  r.ref[0] = (s_idle[0] - a0) * 2;
  r.ref[1] = (s_idle[1] - a1) * 2;
}

bool openSource(Run& r, const char*& err) {
  if (r.a.src == Src::Ram) {
    sdt::fillPattern(sdt::scratch(), kSrcBytes, 0xC0FFEE);
    r.src_size = kSrcBytes;
    return true;
  }
  struct stat st;
  if (sdt::ffDrive() < 0 && !sdt::mount(sdt::g_hz)) err = "no_fat";
  else if (stat(kXferPath, &st) != 0 || st.st_size < 4096) err = "no_file";
  else r.src_size = static_cast<uint32_t>(st.st_size);
  if (!err && r.a.src == Src::SdKeep && (r.fd = open(kXferPath, O_RDONLY)) < 0) err = "open";
  return err == nullptr;
}

void sendLine(const char* p, size_t n) {
  const uint32_t t0 = millis();
  while (!gate_io::writeWhole(p, n) && millis() - t0 < 3000) {
    gate_io::tickUsb(millis(), g_run.empty_free);
    delay(1);
  }
}

void sendStart(const Run& r, size_t frame_max) {
  char b[256];
  const int n = snprintf(b, sizeof b,
                         "{\"t\":\"xfer_start\",\"enc\":\"%s\",\"len\":%u,\"depth\":%u,\"ring_kb\":%u,\"empty_free\":%d,"
                         "\"src\":\"%s\",\"src_size\":%u,\"secs\":%u,\"frame_max\":%u,\"ref0\":%u,\"ref1\":%u}\n",
                         r.a.enc == Enc::B64 ? "b64" : "bin", r.a.len, r.a.depth, r.a.ring_kb, r.empty_free,
                         r.a.src == Src::Ram ? "ram" : r.a.src == Src::Sd ? "sd" : "sdkeep", (unsigned)r.src_size, r.a.secs, (unsigned)frame_max,
                         (unsigned)r.ref[0], (unsigned)r.ref[1]);
  sendLine(b, static_cast<size_t>(n));
}

void sendEnd(const Run& r, const char* err) {
  char b[320];
  const int n = snprintf(b, sizeof b,
                         "{\"t\":\"xfer_end\",\"err\":\"%s\",\"frames\":%u,\"bytes\":%u,\"ms\":%u,\"pings\":%u,"
                         "\"rearms\":%u,\"over_credit\":%u,\"rd_fail\":%u,\"ring_kb\":%u,\"empty_free\":%d}\n",
                         err ? err : "", (unsigned)r.frames, (unsigned)r.bytes, (unsigned)(millis() - r.t0),
                         (unsigned)r.pings, (unsigned)r.rearms, (unsigned)r.over_credit, (unsigned)r.rd_fail,
                         r.a.ring_kb, r.empty_free);
  sendLine(b, static_cast<size_t>(n));
}

void runLoop(Run& r) {
  r.t0 = r.sec_t0 = millis();
  r.idle_at[0] = s_idle[0];
  r.idle_at[1] = s_idle[1];
  while (!r.stop && millis() - r.t0 < r.a.secs * 1000u) {
    serviceRx(r);
    if (gate_io::tickUsb(millis(), r.empty_free)) r.rearms++;
    secondTick(r);
    if (!pump(r)) blockTick(r);
  }
  while ((r.pong_n || r.prio_n) && millis() - r.t0 < r.a.secs * 1000u + 3000) {
    if (!pump(r)) blockTick(r);
  }
}

void cmdXfer(const gate::XferArgs& a) {
  g_run = Run();
  Run& r = g_run;
  r.a = a;
  r.fd = -1;
  const char* err = nullptr;
  M5Cardputer.Mic.end();  // as in the app: no audio while transferring (11 §4.7)
  Serial.setTxTimeoutMs(0);
  r.empty_free = gate_io::setTxRing(a.ring_kb * 1024u);
  char probe[256];
  const gate::FrameMeta worst = {999999999u, 4294967295u, a.len, 4294967295u, 4294967295u};
  const size_t frame_max = gate::frame_size(a.enc, gate::frame_head(probe, sizeof probe, a.enc, worst), a.len);
  if (static_cast<int>(frame_max) > r.empty_free || frame_max > kFrameCap) err = "frame>ring";
  if (!err) openSource(r, err);
  calibrate(r);
  sendStart(r, frame_max);
  if (!err) runLoop(r);
  sendEnd(r, err);
  esp_deregister_freertos_idle_hook_for_cpu(idleHook0, 0);
  esp_deregister_freertos_idle_hook_for_cpu(idleHook1, 1);
  if (r.fd >= 0) close(r.fd);
  if (a.src != Src::Ram) sdt::unmount();
  gate_io::setTxRing(4096);
  Serial.setTxTimeoutMs(100);
  M5Cardputer.Mic.begin();
}

// One 16 KB pattern chunk; returns 0 or the errno of a failed / short write.
int writeChunk(int fd, uint32_t i, size_t n) {
  sdt::fillPattern(sdt::scratch(), kSrcBytes, i * 4096u);
  const ssize_t w = write(fd, sdt::scratch(), n);
  sdt::kickUsb();
  if (w == static_cast<ssize_t>(n)) return 0;
  return w < 0 ? errno : -1;
}

// Pattern file for the sd sources; returns bytes written, *err = errno of a failed call.
size_t writeXferFile(long kb, uint32_t pace_ms, uint32_t sync_kb, int* err) {
  const int fd = open(kXferPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  *err = fd >= 0 ? 0 : errno;
  const size_t total = static_cast<size_t>(kb) * 1024u, sync_bytes = sync_kb * 1024u;
  size_t done = 0;
  for (uint32_t i = 0; *err == 0 && done < total; ++i) {
    const size_t n = std::min(kSrcBytes, total - done);
    *err = writeChunk(fd, i, n);
    done += *err == 0 ? n : 0;
    if (pace_ms) delay(pace_ms);
    if (sync_bytes && *err == 0 && done % sync_bytes < n && fsync(fd) != 0) *err = errno;
  }
  if (fd >= 0 && (fsync(fd) != 0 || close(fd) != 0) && *err == 0) *err = errno;
  return done;
}

void cmdMake(long kb, long pace_ms, long sync_kb) {
  const uint32_t t0 = millis();
  int err = 0;
  size_t done = 0;
  const bool args_ok = kb >= 0 && kb <= 16384 && pace_ms >= 0 && pace_ms <= 1000 && sync_kb >= 0;
  bool ok = args_ok && sdt::mount(sdt::g_hz) && sdt::mkdirs(sdt::kScratch);
  if (ok && kb == 0) {
    ok = unlink(kXferPath) == 0 || errno == ENOENT;
  } else if (ok) {
    done = writeXferFile(kb, static_cast<uint32_t>(pace_ms), static_cast<uint32_t>(sync_kb), &err);
    ok = done == static_cast<size_t>(kb) * 1024u && err == 0;
  }
  sdt::unmount();
  Serial.printf("{\"t\":\"xfermk\",\"kb\":%ld,\"ok\":%d,\"wrote\":%u,\"errno\":%d,\"ms\":%u}\n", kb, ok ? 1 : 0,
                (unsigned)done, err, (unsigned)(millis() - t0));
}

void cmdClean() {
  const bool mounted = sdt::mount(sdt::g_hz);
  const int xfer = mounted ? unlink(kXferPath) : -1;
  const int mic = mounted ? unlink("/sd/cpa_hwtest/mic.wav") : -1;
  const int dir = mounted ? rmdir(sdt::kScratch) : -1;
  sdt::unmount();
  Serial.printf("{\"t\":\"gateclean\",\"mounted\":%d,\"xfer\":%d,\"mic\":%d,\"dir\":%d}\n", mounted ? 1 : 0,
                xfer == 0 ? 1 : 0, mic == 0 ? 1 : 0, dir == 0 ? 1 : 0);
}

}  // namespace

bool gateXferCommand(const String& line) {
  if (line.startsWith("!xfer ")) {
    const gate::Parsed<gate::XferArgs> p = gate::parse_xfer(line.c_str() + 6);
    if (!p.ok) Serial.printf("{\"t\":\"xfer_end\",\"err\":\"args\",\"field\":\"%s\"}\n", p.err);
    else cmdXfer(p.args);
  } else if (line.startsWith("!xfermk ")) {
    long v[3] = {0, 0, 0};  // KB, pace_ms, sync_kb
    int from = 8;
    for (int i = 0; i < 3 && from > 0; ++i) {
      const int sp = line.indexOf(' ', from);
      v[i] = line.substring(from, sp < 0 ? line.length() : sp).toInt();
      from = sp < 0 ? -1 : sp + 1;
    }
    cmdMake(v[0], v[1], v[2]);
  } else if (line == "!gateclean") {
    cmdClean();
  } else {
    return false;
  }
  return true;
}

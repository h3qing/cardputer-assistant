// The microphone of `cpa-sim serve` (gh #32): silence, or the PCM of a WAV file played
// in a loop. Only the audio task's own format is accepted, 16 kHz mono 16-bit PCM
// (little-endian), so the bytes go to the recorder unchanged; convert other files first,
// for example `afconvert -f WAVE -d LEI16@16000 -c 1 in.m4a out.wav`.
#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace cpasim {

struct Wav {
  std::string pcm;
  std::string error;  // empty when the file is usable
};

Wav parse_wav(const std::string& bytes);

class MicSource {
 public:
  explicit MicSource(std::string pcm = std::string()) : pcm_(std::move(pcm)) {}  // empty: silence
  std::string block(size_t n);  // the next n bytes, from the start again at the end

 private:
  std::string pcm_;
  size_t at_ = 0;
};

}  // namespace cpasim

#pragma once

#include "radiff/audioio/WavFile.h"

#include <filesystem>

namespace radiff::audioio {

/// Reads a WAV (RIFF/WAVE) or an MP3 file into de-interleaved float samples. The container is
/// detected from the content (RIFF header, ID3 tag or MPEG frame sync) with the extension as a
/// hint, so a mislabelled file still loads. Output is always written as WAV (see writeWav).
IoError readAudio(const std::filesystem::path& file, WavInfo& info, core::AudioBuffer& audio);

/// MP3 decoding (MPEG-1/2/2.5 Layer III, mono or stereo, any bitrate, CBR/VBR) with minimp3.
/// Encoder delay and padding are removed when the file carries a LAME/Xing tag. info.format is
/// set to Pcm24 (recommended output encoding) and info.container to Container::Mp3.
IoError readMp3(const std::filesystem::path& file, WavInfo& info, core::AudioBuffer& audio);

/// True if the file has an .mp3 extension (any case) or starts with an ID3 tag / MPEG sync word.
[[nodiscard]] bool looksLikeMp3(const std::filesystem::path& file);

} // namespace radiff::audioio

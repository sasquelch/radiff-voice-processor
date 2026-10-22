#pragma once

#include "radiff/core/AudioBuffer.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace radiff::audioio {

/// Sample encodings supported for reading and writing.
enum class SampleFormat { Pcm16, Pcm24, Pcm32, Float32 };

/// Where the audio came from. The engine only ever writes WAV; MP3 is accepted as input.
enum class Container { Wav, Mp3 };

struct WavInfo {
    double sampleRate = 0.0;
    std::uint16_t numChannels = 0;
    std::uint64_t numFrames = 0;
    /// Sample encoding of a WAV file. For MP3 input this is the recommended output encoding
    /// (Pcm24), so "keep the input format" produces a sensible WAV.
    SampleFormat format = SampleFormat::Pcm16;
    Container container = Container::Wav;
    [[nodiscard]] std::uint16_t bitsPerSample() const noexcept;
};

struct IoError {
    std::string message; ///< empty = success
    [[nodiscard]] bool ok() const noexcept { return message.empty(); }
    static IoError success() { return {}; }
    static IoError failure(std::string m) { return {std::move(m)}; }
};

/// Reads the header only. Accepts canonical PCM, IEEE float and WAVE_FORMAT_EXTENSIBLE
/// files, with chunk ordering that is not fixed (fmt/data may appear after LIST etc).
IoError readWavInfo(const std::filesystem::path& file, WavInfo& info);

/// Reads the entire file into de-interleaved float samples in [-1, 1].
IoError readWav(const std::filesystem::path& file, WavInfo& info, core::AudioBuffer& audio);

/// Writes a complete file. Samples are clipped to [-1, 1] before quantisation.
/// 16/24/32-bit PCM files are written as canonical WAVE_FORMAT_PCM when mono/stereo,
/// float as WAVE_FORMAT_IEEE_FLOAT, so that every DAW and playout system can read them.
/// The caller is responsible for making the write transactional (see radiff::app).
IoError writeWav(const std::filesystem::path& file, double sampleRate, SampleFormat format,
                 const core::AudioBuffer& audio);

} // namespace radiff::audioio

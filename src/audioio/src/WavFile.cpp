#include "radiff/audioio/WavFile.h"
#include "radiff/platform/Paths.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace radiff::audioio {

namespace {

constexpr std::uint16_t kFormatPcm = 0x0001;
constexpr std::uint16_t kFormatIeeeFloat = 0x0003;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

// WAV is always little-endian; these helpers make the code endian-independent.
std::uint16_t rd16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t rd32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
void wr16(unsigned char* p, std::uint16_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFF);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
}
void wr32(unsigned char* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
}

struct ParsedHeader {
    WavInfo info;
    std::uint64_t dataOffset = 0;
    std::uint64_t dataBytes = 0;
    std::uint16_t blockAlign = 0;
};

IoError parseHeader(std::ifstream& in, const std::filesystem::path& file, ParsedHeader& out) {
    const std::string name = platform::pathToUtf8(file);
    unsigned char riff[12];
    if (!in.read(reinterpret_cast<char*>(riff), 12))
        return IoError::failure("Not a WAV file (too short): " + name);
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
        return IoError::failure("Not a RIFF/WAVE file: " + name);

    bool haveFmt = false, haveData = false;
    std::uint16_t formatTag = 0, bits = 0;
    while (!haveData) {
        unsigned char ch[8];
        if (!in.read(reinterpret_cast<char*>(ch), 8))
            break;
        const std::uint32_t size = rd32(ch + 4);
        const std::streamoff chunkStart = in.tellg();
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            if (size < 16)
                return IoError::failure("Malformed fmt chunk: " + name);
            std::vector<unsigned char> fmt(size);
            if (!in.read(reinterpret_cast<char*>(fmt.data()), static_cast<std::streamsize>(size)))
                return IoError::failure("Truncated fmt chunk: " + name);
            formatTag = rd16(fmt.data());
            out.info.numChannels = rd16(fmt.data() + 2);
            out.info.sampleRate = static_cast<double>(rd32(fmt.data() + 4));
            out.blockAlign = rd16(fmt.data() + 12);
            bits = rd16(fmt.data() + 14);
            if (formatTag == kFormatExtensible) {
                if (size < 40)
                    return IoError::failure("Malformed WAVE_FORMAT_EXTENSIBLE chunk: " + name);
                // Sub-format GUID: first two bytes hold the actual format tag.
                formatTag = rd16(fmt.data() + 24);
            }
            haveFmt = true;
        } else if (std::memcmp(ch, "data", 4) == 0) {
            out.dataOffset = static_cast<std::uint64_t>(chunkStart);
            out.dataBytes = size;
            haveData = true;
            break;
        }
        // Chunks are word-aligned.
        in.seekg(chunkStart + static_cast<std::streamoff>(size + (size & 1u)), std::ios::beg);
        if (!in)
            break;
    }
    if (!haveFmt)
        return IoError::failure("WAV file has no fmt chunk: " + name);
    if (!haveData)
        return IoError::failure("WAV file has no data chunk: " + name);
    if (out.info.numChannels == 0 || out.info.sampleRate <= 0.0)
        return IoError::failure("WAV file has invalid channel count or sample rate: " + name);

    if (formatTag == kFormatPcm) {
        switch (bits) {
        case 16: out.info.format = SampleFormat::Pcm16; break;
        case 24: out.info.format = SampleFormat::Pcm24; break;
        case 32: out.info.format = SampleFormat::Pcm32; break;
        default: return IoError::failure("Unsupported PCM bit depth " + std::to_string(bits) + ": " + name);
        }
    } else if (formatTag == kFormatIeeeFloat) {
        if (bits != 32)
            return IoError::failure("Unsupported float bit depth " + std::to_string(bits) + ": " + name);
        out.info.format = SampleFormat::Float32;
    } else {
        return IoError::failure("Unsupported WAV format tag " + std::to_string(formatTag) + ": " + name);
    }

    const std::uint16_t expectedAlign = static_cast<std::uint16_t>(out.info.numChannels * (bits / 8));
    if (out.blockAlign == 0)
        out.blockAlign = expectedAlign;
    if (out.blockAlign != expectedAlign)
        return IoError::failure("Inconsistent block alignment: " + name);

    // Some writers (streaming encoders) leave the data size as 0 or 0xFFFFFFFF; clamp to file size.
    in.seekg(0, std::ios::end);
    const auto fileSize = static_cast<std::uint64_t>(in.tellg());
    if (out.dataOffset > fileSize)
        return IoError::failure("Truncated WAV file: " + name);
    const std::uint64_t available = fileSize - out.dataOffset;
    if (out.dataBytes == 0 || out.dataBytes == 0xFFFFFFFFu || out.dataBytes > available)
        out.dataBytes = available;
    out.info.numFrames = out.dataBytes / out.blockAlign;
    return IoError::success();
}

} // namespace

std::uint16_t WavInfo::bitsPerSample() const noexcept {
    switch (format) {
    case SampleFormat::Pcm16: return 16;
    case SampleFormat::Pcm24: return 24;
    case SampleFormat::Pcm32: return 32;
    case SampleFormat::Float32: return 32;
    }
    return 0;
}

IoError readWavInfo(const std::filesystem::path& file, WavInfo& info) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return IoError::failure("Cannot open input file: " + platform::pathToUtf8(file));
    ParsedHeader h;
    if (auto e = parseHeader(in, file, h); !e.ok())
        return e;
    info = h.info;
    return IoError::success();
}

IoError readWav(const std::filesystem::path& file, WavInfo& info, core::AudioBuffer& audio) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return IoError::failure("Cannot open input file: " + platform::pathToUtf8(file));
    ParsedHeader h;
    if (auto e = parseHeader(in, file, h); !e.ok())
        return e;
    info = h.info;

    in.seekg(static_cast<std::streamoff>(h.dataOffset), std::ios::beg);
    const std::size_t frames = static_cast<std::size_t>(h.info.numFrames);
    const std::size_t channels = h.info.numChannels;
    const std::size_t bytesPerSample = h.blockAlign / channels;
    audio.resize(channels, frames);

    constexpr std::size_t kChunkFrames = 65536;
    std::vector<unsigned char> raw(kChunkFrames * h.blockAlign);
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t n = std::min(kChunkFrames, frames - done);
        const std::streamsize bytes = static_cast<std::streamsize>(n * h.blockAlign);
        if (!in.read(reinterpret_cast<char*>(raw.data()), bytes))
            return IoError::failure("Unexpected end of audio data: " + platform::pathToUtf8(file));
        const unsigned char* p = raw.data();
        for (std::size_t f = 0; f < n; ++f) {
            for (std::size_t c = 0; c < channels; ++c) {
                float v = 0.0f;
                switch (h.info.format) {
                case SampleFormat::Pcm16: {
                    const auto s = static_cast<std::int16_t>(rd16(p));
                    v = static_cast<float>(s) / 32768.0f;
                    break;
                }
                case SampleFormat::Pcm24: {
                    std::int32_t s = static_cast<std::int32_t>(p[0]) | (static_cast<std::int32_t>(p[1]) << 8) |
                                     (static_cast<std::int32_t>(p[2]) << 16);
                    if (s & 0x800000)
                        s |= ~0xFFFFFF; // sign-extend
                    v = static_cast<float>(s) / 8388608.0f;
                    break;
                }
                case SampleFormat::Pcm32: {
                    const auto s = static_cast<std::int32_t>(rd32(p));
                    v = static_cast<float>(static_cast<double>(s) / 2147483648.0);
                    break;
                }
                case SampleFormat::Float32: {
                    const std::uint32_t bitsU = rd32(p);
                    std::memcpy(&v, &bitsU, sizeof(v));
                    break;
                }
                }
                audio.channel(c)[done + f] = v;
                p += bytesPerSample;
            }
        }
        done += n;
    }
    return IoError::success();
}

IoError writeWav(const std::filesystem::path& file, double sampleRate, SampleFormat format,
                 const core::AudioBuffer& audio) {
    const std::string name = platform::pathToUtf8(file);
    const std::size_t channels = audio.numChannels();
    const std::size_t frames = audio.numFrames();
    if (channels == 0 || channels > 65535)
        return IoError::failure("Invalid channel count for WAV output: " + name);
    if (sampleRate <= 0.0 || sampleRate > 4294967295.0 || std::floor(sampleRate) != sampleRate)
        return IoError::failure("WAV output requires an integer sample rate: " + name);

    WavInfo tmp;
    tmp.format = format;
    const std::uint16_t bits = tmp.bitsPerSample();
    const std::uint16_t bytesPerSample = static_cast<std::uint16_t>(bits / 8);
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * bytesPerSample);
    const std::uint64_t dataBytes = static_cast<std::uint64_t>(frames) * blockAlign;
    if (dataBytes + 44 > 0xFFFFFFFFull)
        return IoError::failure("Audio is too long for a standard WAV file (>4 GiB): " + name);

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return IoError::failure("Cannot create output file: " + name);

    const std::uint16_t formatTag = format == SampleFormat::Float32 ? kFormatIeeeFloat : kFormatPcm;
    const bool writeFact = formatTag == kFormatIeeeFloat;
    const std::uint32_t factSize = writeFact ? 12u : 0u;
    unsigned char hdr[12 + 24 + 12 + 8];
    std::size_t pos = 0;
    std::memcpy(hdr + pos, "RIFF", 4); pos += 4;
    wr32(hdr + pos, static_cast<std::uint32_t>(4 + 24 + factSize + 8 + dataBytes + (dataBytes & 1))); pos += 4;
    std::memcpy(hdr + pos, "WAVE", 4); pos += 4;
    std::memcpy(hdr + pos, "fmt ", 4); pos += 4;
    wr32(hdr + pos, 16); pos += 4;
    wr16(hdr + pos, formatTag); pos += 2;
    wr16(hdr + pos, static_cast<std::uint16_t>(channels)); pos += 2;
    wr32(hdr + pos, static_cast<std::uint32_t>(sampleRate)); pos += 4;
    wr32(hdr + pos, static_cast<std::uint32_t>(sampleRate) * blockAlign); pos += 4;
    wr16(hdr + pos, blockAlign); pos += 2;
    wr16(hdr + pos, bits); pos += 2;
    if (writeFact) {
        std::memcpy(hdr + pos, "fact", 4); pos += 4;
        wr32(hdr + pos, 4); pos += 4;
        wr32(hdr + pos, static_cast<std::uint32_t>(frames)); pos += 4;
    }
    std::memcpy(hdr + pos, "data", 4); pos += 4;
    wr32(hdr + pos, static_cast<std::uint32_t>(dataBytes)); pos += 4;
    out.write(reinterpret_cast<const char*>(hdr), static_cast<std::streamsize>(pos));

    constexpr std::size_t kChunkFrames = 65536;
    std::vector<unsigned char> raw(kChunkFrames * blockAlign);
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t n = std::min(kChunkFrames, frames - done);
        unsigned char* p = raw.data();
        for (std::size_t f = 0; f < n; ++f) {
            for (std::size_t c = 0; c < channels; ++c) {
                const float v = std::clamp(audio.channel(c)[done + f], -1.0f, 1.0f);
                switch (format) {
                case SampleFormat::Pcm16: {
                    // Symmetric power-of-two scaling (matches the reader) so that a write/read
                    // round trip reproduces the input to within half an LSB.
                    const auto s = static_cast<std::int16_t>(std::clamp(std::lrint(v * 32768.0f), -32768L, 32767L));
                    wr16(p, static_cast<std::uint16_t>(s));
                    break;
                }
                case SampleFormat::Pcm24: {
                    const auto s = static_cast<std::int32_t>(std::clamp(std::lrint(v * 8388608.0f), -8388608L, 8388607L));
                    p[0] = static_cast<unsigned char>(s & 0xFF);
                    p[1] = static_cast<unsigned char>((s >> 8) & 0xFF);
                    p[2] = static_cast<unsigned char>((s >> 16) & 0xFF);
                    break;
                }
                case SampleFormat::Pcm32: {
                    const double scaled = static_cast<double>(v) * 2147483648.0;
                    const auto s = static_cast<std::int32_t>(std::clamp(std::llrint(scaled), -2147483648LL, 2147483647LL));
                    wr32(p, static_cast<std::uint32_t>(s));
                    break;
                }
                case SampleFormat::Float32: {
                    std::uint32_t bitsU;
                    std::memcpy(&bitsU, &v, sizeof(v));
                    wr32(p, bitsU);
                    break;
                }
                }
                p += bytesPerSample;
            }
        }
        out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(n * blockAlign));
        done += n;
    }
    if (dataBytes & 1)
        out.put('\0');
    out.flush();
    if (!out)
        return IoError::failure("Write failed (disk full?): " + name);
    return IoError::success();
}

} // namespace radiff::audioio

#include "radiff/audioio/AudioFile.h"
#include "radiff/platform/Paths.h"

#include "minimp3_ex.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace radiff::audioio {

namespace {

IoError readAll(const std::filesystem::path& file, std::vector<unsigned char>& bytes) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return IoError::failure("Cannot open input file: " + platform::pathToUtf8(file));
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0)
        return IoError::failure("Cannot read input file: " + platform::pathToUtf8(file));
    in.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(bytes.data()), size))
        return IoError::failure("Cannot read input file: " + platform::pathToUtf8(file));
    return IoError::success();
}

bool hasMp3Extension(const std::filesystem::path& file) {
    std::string ext = platform::pathToUtf8(file.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp3";
}

bool startsLikeMp3(const unsigned char* p, std::size_t n) {
    if (n >= 3 && p[0] == 'I' && p[1] == 'D' && p[2] == '3')
        return true;
    return n >= 2 && p[0] == 0xFF && (p[1] & 0xE0) == 0xE0;
}

bool startsLikeRiff(const unsigned char* p, std::size_t n) {
    return n >= 12 && p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F' && p[8] == 'W' && p[9] == 'A' &&
           p[10] == 'V' && p[11] == 'E';
}

} // namespace

bool looksLikeMp3(const std::filesystem::path& file) {
    if (hasMp3Extension(file))
        return true;
    std::ifstream in(file, std::ios::binary);
    unsigned char head[12] = {};
    if (!in)
        return false;
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    const auto n = static_cast<std::size_t>(in.gcount());
    return !startsLikeRiff(head, n) && startsLikeMp3(head, n);
}

IoError readMp3(const std::filesystem::path& file, WavInfo& info, core::AudioBuffer& audio) {
    std::vector<unsigned char> bytes;
    if (auto e = readAll(file, bytes); !e.ok())
        return e;
    if (bytes.empty())
        return IoError::failure("MP3 file is empty: " + platform::pathToUtf8(file));

    mp3dec_t dec{};
    mp3dec_file_info_t fi{};
    const int rc = mp3dec_load_buf(&dec, bytes.data(), bytes.size(), &fi, nullptr, nullptr);
    struct Free {
        void* p;
        ~Free() { std::free(p); }
    } guard{fi.buffer};
    if (rc != 0)
        return IoError::failure("MP3 decoding failed (code " + std::to_string(rc) + "): " + platform::pathToUtf8(file));
    if (fi.samples == 0 || fi.channels <= 0 || fi.hz <= 0)
        return IoError::failure("No MP3 audio frames found in: " + platform::pathToUtf8(file));
    if (fi.channels > 2)
        return IoError::failure("MP3 with more than two channels is not supported: " + platform::pathToUtf8(file));

    const auto channels = static_cast<std::size_t>(fi.channels);
    const std::size_t frames = fi.samples / channels;
    audio.resize(channels, frames);
    const mp3d_sample_t* src = fi.buffer; // float, interleaved (MINIMP3_FLOAT_OUTPUT)
    for (std::size_t i = 0; i < frames; ++i)
        for (std::size_t c = 0; c < channels; ++c)
            audio.channel(c)[i] = static_cast<float>(src[i * channels + c]);

    info = WavInfo{};
    info.sampleRate = static_cast<double>(fi.hz);
    info.numChannels = static_cast<std::uint16_t>(channels);
    info.numFrames = frames;
    info.format = SampleFormat::Pcm24;
    info.container = Container::Mp3;
    info.layout = "mp3(layer=" + std::to_string(fi.layer) + " ch=" + std::to_string(fi.channels) + " rate=" +
                  std::to_string(fi.hz) + " avg_kbps=" + std::to_string(fi.avg_bitrate_kbps) + ") file=" +
                  std::to_string(bytes.size());
    return IoError::success();
}

IoError readAudio(const std::filesystem::path& file, WavInfo& info, core::AudioBuffer& audio) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return IoError::failure("Cannot open input file: " + platform::pathToUtf8(file));
    unsigned char head[12] = {};
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    const auto n = static_cast<std::size_t>(in.gcount());
    in.close();
    if (startsLikeRiff(head, n))
        return readWav(file, info, audio);
    if (startsLikeMp3(head, n) || hasMp3Extension(file))
        return readMp3(file, info, audio);
    return IoError::failure("Unsupported input (not a RIFF/WAVE or MP3 file): " + platform::pathToUtf8(file));
}

} // namespace radiff::audioio

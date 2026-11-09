#include "mp3_writer.h"

#include <lame.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace ap::test {

Mp3Info writeMp3(const std::filesystem::path& path, std::uint32_t sampleRate, std::uint16_t channels,
                 const std::vector<float>& interleaved, const Mp3Options& options)
{
    if (channels < 1 || channels > 2 || interleaved.size() % channels != 0)
        throw std::invalid_argument("writeMp3: 1 or 2 channels");
    const std::size_t frames = interleaved.size() / channels;

    lame_global_flags* gf = lame_init();
    if (!gf)
        throw std::runtime_error("LAME: lame_init failed");
    struct Guard
    {
        lame_global_flags* gf;
        ~Guard() { lame_close(gf); }
    } guard{gf};

    lame_set_in_samplerate(gf, static_cast<int>(sampleRate));
    lame_set_out_samplerate(gf, static_cast<int>(sampleRate));
    lame_set_num_channels(gf, channels);
    lame_set_mode(gf, channels == 1 ? MONO : JOINT_STEREO);
    if (options.vbr)
    {
        lame_set_VBR(gf, vbr_default);
        lame_set_VBR_quality(gf, options.vbrQuality);
    }
    else
    {
        lame_set_VBR(gf, vbr_off);
        lame_set_brate(gf, options.bitrateKbps);
    }
    lame_set_bWriteVbrTag(gf, options.lameTag ? 1 : 0);
    lame_set_quality(gf, 2);
    if (lame_init_params(gf) < 0)
        throw std::runtime_error("LAME: lame_init_params failed (unsupported rate or bit rate?)");

    std::vector<float> left(frames), right(frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        left[i] = interleaved[i * channels];
        right[i] = channels == 2 ? interleaved[i * channels + 1] : 0.0f;
    }

    std::vector<unsigned char> out;
    std::vector<unsigned char> buf(static_cast<std::size_t>(1.25 * 8192 + 7200));
    constexpr std::size_t kChunk = 8192;
    for (std::size_t done = 0; done < frames; done += kChunk)
    {
        const int n = static_cast<int>(std::min(kChunk, frames - done));
        buf.resize(static_cast<std::size_t>(1.25 * n + 7200));
        const int bytes = lame_encode_buffer_ieee_float(gf, left.data() + done, right.data() + done, n, buf.data(),
                                                        static_cast<int>(buf.size()));
        if (bytes < 0)
            throw std::runtime_error("LAME: encode failed " + std::to_string(bytes));
        out.insert(out.end(), buf.begin(), buf.begin() + bytes);
    }
    buf.resize(7200);
    const int tail = lame_encode_flush(gf, buf.data(), static_cast<int>(buf.size()));
    if (tail < 0)
        throw std::runtime_error("LAME: flush failed");
    out.insert(out.end(), buf.begin(), buf.begin() + tail);

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w+b") != 0 || !f)
        throw std::runtime_error("cannot create " + path.string());
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    if (ok && options.lameTag)
        lame_mp3_tags_fid(gf, f); // rewrites the first frame with the Xing/LAME tag
    std::fclose(f);
    if (!ok)
        throw std::runtime_error("cannot write " + path.string());
    return {lame_get_encoder_delay(gf), lame_get_encoder_padding(gf)};
}

} // namespace ap::test

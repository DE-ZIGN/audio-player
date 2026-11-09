#include "mf_audio.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>

namespace ap::test {

using Microsoft::WRL::ComPtr;

namespace {

void check(HRESULT hr, const char* what)
{
    if (FAILED(hr))
        throw std::runtime_error(std::format("Media Foundation: {} failed (0x{:08X})", what, static_cast<unsigned long>(hr)));
}

// COM + Media Foundation for the calling thread, for the duration of a call.
struct MfScope
{
    bool com = false;
    MfScope()
    {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        check(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET), "MFStartup");
    }
    ~MfScope()
    {
        MFShutdown();
        if (com)
            CoUninitialize();
    }
};

ComPtr<IMFMediaType> pcmType(std::uint32_t rate, std::uint16_t channels, std::uint16_t bits)
{
    ComPtr<IMFMediaType> t;
    check(MFCreateMediaType(&t), "MFCreateMediaType");
    const UINT32 blockAlign = channels * bits / 8u;
    check(t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "SetGUID(major)");
    check(t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "SetGUID(PCM)");
    check(t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels), "channels");
    check(t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate), "rate");
    check(t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bits), "bits");
    check(t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign), "block align");
    check(t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * blockAlign), "bytes per second");
    check(t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE), "independent");
    return t;
}

} // namespace

namespace {

void writeM4a(const std::filesystem::path& path, const MfPcm& pcm, const GUID& codec);

} // namespace

void mfWriteAlacM4a(const std::filesystem::path& path, const MfPcm& pcm)
{
    writeM4a(path, pcm, MFAudioFormat_ALAC);
}

void mfWriteAacM4a(const std::filesystem::path& path, const MfPcm& pcm)
{
    writeM4a(path, pcm, MFAudioFormat_AAC);
}

namespace {

void writeM4a(const std::filesystem::path& path, const MfPcm& pcm, const GUID& codec)
{
    MfScope mf;
    const UINT32 blockAlign = pcm.channels * pcm.bitsPerSample / 8u;
    if (blockAlign == 0 || pcm.data.size() % blockAlign != 0)
        throw std::invalid_argument("mfWriteAlacM4a: bad PCM layout");

    ComPtr<IMFAttributes> attrs;
    check(MFCreateAttributes(&attrs, 1), "MFCreateAttributes");
    check(attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4), "container");
    ComPtr<IMFSinkWriter> writer;
    check(MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs.Get(), &writer), "MFCreateSinkWriterFromURL");

    ComPtr<IMFMediaType> out;
    check(MFCreateMediaType(&out), "MFCreateMediaType");
    check(out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "SetGUID(major)");
    check(out->SetGUID(MF_MT_SUBTYPE, codec), "SetGUID(codec)");
    check(out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, pcm.channels), "channels");
    check(out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, pcm.sampleRate), "rate");
    if (codec == MFAudioFormat_AAC)
        check(out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000), "AAC bit rate");
    else
        check(out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, pcm.bitsPerSample), "bits");
    DWORD stream = 0;
    check(writer->AddStream(out.Get(), &stream), "AddStream");
    check(writer->SetInputMediaType(stream, pcmType(pcm.sampleRate, pcm.channels, pcm.bitsPerSample).Get(), nullptr),
          "SetInputMediaType (PCM)");
    check(writer->BeginWriting(), "BeginWriting");

    const std::size_t frames = pcm.data.size() / blockAlign;
    const std::size_t chunk = 4096;
    for (std::size_t done = 0; done < frames;)
    {
        const std::size_t n = std::min(chunk, frames - done);
        ComPtr<IMFMediaBuffer> buffer;
        check(MFCreateMemoryBuffer(static_cast<DWORD>(n * blockAlign), &buffer), "MFCreateMemoryBuffer");
        BYTE* p = nullptr;
        check(buffer->Lock(&p, nullptr, nullptr), "Lock");
        std::memcpy(p, pcm.data.data() + done * blockAlign, n * blockAlign);
        check(buffer->Unlock(), "Unlock");
        check(buffer->SetCurrentLength(static_cast<DWORD>(n * blockAlign)), "SetCurrentLength");

        ComPtr<IMFSample> sample;
        check(MFCreateSample(&sample), "MFCreateSample");
        check(sample->AddBuffer(buffer.Get()), "AddBuffer");
        check(sample->SetSampleTime(static_cast<LONGLONG>(done * 10'000'000ull / pcm.sampleRate)), "SetSampleTime");
        check(sample->SetSampleDuration(static_cast<LONGLONG>(n * 10'000'000ull / pcm.sampleRate)), "SetSampleDuration");
        check(writer->WriteSample(stream, sample.Get()), "WriteSample");
        done += n;
    }
    check(writer->Finalize(), "Finalize");
}

} // namespace

MfFloat mfDecodeFloat(const std::filesystem::path& path)
{
    MfScope mf;
    ComPtr<IMFSourceReader> reader;
    check(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader), "MFCreateSourceReaderFromURL");
    const DWORD audio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    ComPtr<IMFMediaType> native;
    check(reader->GetNativeMediaType(audio, 0, &native), "GetNativeMediaType");
    UINT32 rate = 0, channels = 0;
    check(native->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate), "native rate");
    check(native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels), "native channels");

    ComPtr<IMFMediaType> want;
    check(MFCreateMediaType(&want), "MFCreateMediaType");
    check(want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "SetGUID(major)");
    check(want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float), "SetGUID(float)");
    check(reader->SetCurrentMediaType(audio, nullptr, want.Get()), "SetCurrentMediaType (float)");

    MfFloat result{rate, static_cast<std::uint16_t>(channels), {}};
    for (;;)
    {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        check(reader->ReadSample(audio, 0, nullptr, &flags, nullptr, &sample), "ReadSample");
        if (sample)
        {
            ComPtr<IMFMediaBuffer> buffer;
            check(sample->ConvertToContiguousBuffer(&buffer), "ConvertToContiguousBuffer");
            BYTE* p = nullptr;
            DWORD length = 0;
            check(buffer->Lock(&p, nullptr, &length), "Lock");
            const auto* f = reinterpret_cast<const float*>(p);
            result.samples.insert(result.samples.end(), f, f + length / sizeof(float));
            buffer->Unlock();
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
    }
    return result;
}

MfPcm mfDecode(const std::filesystem::path& path, std::uint16_t bitsPerSample)
{
    MfScope mf;
    ComPtr<IMFSourceReader> reader;
    check(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader), "MFCreateSourceReaderFromURL");
    const DWORD audio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

    ComPtr<IMFMediaType> native;
    check(reader->GetNativeMediaType(audio, 0, &native), "GetNativeMediaType");
    UINT32 rate = 0, channels = 0;
    check(native->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate), "native rate");
    check(native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels), "native channels");
    check(reader->SetCurrentMediaType(audio, nullptr,
                                      pcmType(rate, static_cast<std::uint16_t>(channels), bitsPerSample).Get()),
          "SetCurrentMediaType (PCM)");

    MfPcm result{rate, static_cast<std::uint16_t>(channels), bitsPerSample, {}};
    for (;;)
    {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        check(reader->ReadSample(audio, 0, nullptr, &flags, nullptr, &sample), "ReadSample");
        if (sample)
        {
            ComPtr<IMFMediaBuffer> buffer;
            check(sample->ConvertToContiguousBuffer(&buffer), "ConvertToContiguousBuffer");
            BYTE* p = nullptr;
            DWORD length = 0;
            check(buffer->Lock(&p, nullptr, &length), "Lock");
            result.data.insert(result.data.end(), reinterpret_cast<std::byte*>(p), reinterpret_cast<std::byte*>(p) + length);
            buffer->Unlock();
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
    }
    return result;
}

} // namespace ap::test

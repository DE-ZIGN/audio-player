// ap_play: step S1 prototype. Plays a WAV file bit-perfect through exclusive,
// event-driven WASAPI, or writes the engine output to a file (dummy output).
//
//   ap_play --list
//   ap_play FILE [--device N|ID] [--exact-only] [--check]
//   ap_play FILE --dummy OUT.raw [--exact-only]

#include <ap/engine/output/dummy_output.h>
#include <ap/engine/output/wasapi_exclusive_output.h>
#include <ap/engine/playback.h>
#include <ap/engine/sha256.h>
#include <ap/engine/wav_file_source.h>

#include "environment_probe.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace ap::engine;

namespace {

constexpr const char* kUsage =
    "使い方:\n"
    "  ap_play --list                         出力デバイスの一覧\n"
    "  ap_play --formats [--device N|ID]      デバイスが排他モードで受け付けるレートと形式の一覧\n"
    "  ap_play FILE [--device N|ID]           排他 WASAPI で再生（既定のデバイス、または一覧の番号か ID）\n"
    "               [--exact-only]            コンテナの詰め替え（24bit → 32bit コンテナなど）もしない\n"
    "               [--check]                 再生せず、送る形式だけを確かめる\n"
    "  ap_play FILE --dummy OUT.raw           デバイスに送るはずのバイト列をファイルに書き、ハッシュを比べる\n"
    "  ap_play --env                          実行環境（MSIX パッケージの中かどうか）\n"
    "  ap_play --asio-list                    ASIO ドライバーの一覧（レジストリ）\n"
    "  ap_play --asio-load N|all              ASIO ドライバーを読み込めるか試す（再生はしない）\n"
    "\n"
    "原音経路です。音量、変換、ディザはかけません。デバイスが受け付けない形式は再生せず、理由を表示します。\n";

void print(const std::string& s)
{
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

void printErr(const std::string& s)
{
    std::fwrite(s.data(), 1, s.size(), stderr);
}

std::string duration(std::uint64_t frames, std::uint32_t rate)
{
    const double seconds = static_cast<double>(frames) / rate;
    const int m = static_cast<int>(seconds / 60);
    return std::format("{}:{:04.1f}", m, seconds - m * 60);
}

std::string rejectionText(const FormatCheck& c, const PcmFormat& f)
{
    const std::string rate = std::format("{:g} kHz", f.sampleRate / 1000.0);
    switch (c.reason)
    {
        case FormatRejection::SampleRate:
            return std::format("このデバイスは排他モードで {} に対応していません。レートの変換はしません。", rate);
        case FormatRejection::BitDepth:
            return std::format("このデバイスは {} には対応していますが、この形式（{}）には対応していません。",
                               rate, describe(f));
        case FormatRejection::Channels:
            return std::format("このデバイスは {} チャンネルに対応していません。", f.channels);
        case FormatRejection::SampleType:
            return "このデバイスは浮動小数点の形式に対応していません。";
        default:
            return std::format("このデバイスはこの形式に対応していません（0x{:08X}）。",
                               static_cast<unsigned long>(c.nativeError));
    }
}

std::string outputErrorText(const OutputException& e)
{
    switch (e.code())
    {
        case OutputError::DeviceNotFound:
            return "デバイスが見つかりません。--list で番号か ID を確かめてください。";
        case OutputError::DeviceInUse:
            return "他のアプリがこのデバイスを排他モードで使っています。そのアプリを止めてから試してください。";
        case OutputError::ExclusiveModeNotAllowed:
            return "このデバイスは排他モードが許可されていません。サウンドの設定 → このデバイスのプロパティ → "
                   "詳細 で「アプリケーションによりこのデバイスを排他的に制御できるようにする」をオンにしてください。";
        case OutputError::FormatNotSupported:
            return "デバイスがこの形式を受け付けませんでした。";
        case OutputError::DeviceInvalidated:
            return "デバイスが外されたか、設定が変わりました。";
        case OutputError::Timeout:
            return "デバイスがデータを要求しなくなりました。";
        default:
            return std::format("デバイスのエラーです（{}）。", e.what());
    }
}

std::string repackText(const PlaybackPlan& plan)
{
    switch (plan.repack)
    {
        case Repack::None:
            return "なし（ファイルのバイト列をそのまま送ります）";
        case Repack::WiderContainer:
            return std::format("コンテナの詰め替え：{}bit → {}bit コンテナ（有効 {}bit、下位は 0。値は変わりません）",
                               plan.source.containerBits, plan.output.containerBits, plan.output.validBits);
        case Repack::ZeroPadded:
            return std::format("ゼロ詰め：{}bit → {}bit（下位 {}bit は 0。値は変わりません）", plan.source.validBits,
                               plan.output.validBits, plan.output.validBits - plan.source.validBits);
    }
    return {};
}

void printVolume(const EndpointVolumeInfo& v)
{
    if (!v.available)
    {
        print("  Windows の音量: 取得できませんでした\n");
        return;
    }
    const int percent = static_cast<int>(v.scalar * 100.0f + 0.5f);
    print(std::format("  Windows の音量: {}%{}（{}）\n", percent, v.muted ? "、ミュート" : "",
                      v.hardwareVolume ? "デバイス側の音量" : "ソフトウェア音量"));
    if (percent >= 100 && !v.muted)
        return;
    if (v.hardwareVolume)
        print("  警告: このデバイスは音量をデバイス側で処理します。100% 以外だと、DAC に届いた後で信号が変わります"
              "（アプリからドライバーまではビットパーフェクトのままです）。\n");
    else
        print("  注意: 排他モードでは Windows のソフトウェア音量はかかりません。この設定は再生に影響しません。\n");
}

AudioOutput* g_output = nullptr;

BOOL WINAPI onConsoleCtrl(DWORD type)
{
    if ((type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) && g_output)
    {
        g_output->requestStop();
        return TRUE;
    }
    return FALSE;
}

struct Options
{
    std::string file;
    std::string device;
    std::string dummy;
    bool exactOnly = false;
    bool checkOnly = false;
    bool list = false;
    bool formats = false;
    bool env = false;
    bool asioList = false;
    std::string asioLoad;
};

std::optional<Options> parse(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error(a + " に値がありません");
            return argv[++i];
        };
        if (a == "--list")
            o.list = true;
        else if (a == "--formats")
            o.formats = true;
        else if (a == "--env")
            o.env = true;
        else if (a == "--asio-list")
            o.asioList = true;
        else if (a == "--asio-load")
            o.asioLoad = value();
        else if (a == "--device")
            o.device = value();
        else if (a == "--dummy")
            o.dummy = value();
        else if (a == "--exact-only")
            o.exactOnly = true;
        else if (a == "--check")
            o.checkOnly = true;
        else if (a == "--help" || a == "-h")
            return std::nullopt;
        else if (a.starts_with("--"))
            throw std::runtime_error("不明なオプション: " + a);
        else if (o.file.empty())
            o.file = a;
        else
            throw std::runtime_error("ファイルは1つだけ指定してください");
    }
    if (!o.list && !o.formats && !o.env && !o.asioList && o.asioLoad.empty() && o.file.empty())
        return std::nullopt;
    return o;
}

int listDevices()
{
    const auto devices = WasapiExclusiveOutput::listDevices();
    if (devices.empty())
    {
        print("出力デバイスが見つかりません。\n");
        return 1;
    }
    for (std::size_t i = 0; i < devices.size(); ++i)
        print(std::format("{:2}. {}{}\n    ID: {}\n", i + 1, devices[i].name, devices[i].isDefault ? "（既定）" : "",
                          devices[i].id));
    return 0;
}

std::string resolveDeviceId(const std::string& arg)
{
    if (arg.empty())
        return {};
    if (arg.find_first_not_of("0123456789") == std::string::npos)
    {
        const auto devices = WasapiExclusiveOutput::listDevices();
        const auto index = std::stoul(arg);
        if (index == 0 || index > devices.size())
            throw OutputException(OutputError::DeviceNotFound, "device index out of range");
        return devices[index - 1].id;
    }
    return arg;
}

int showEnvironment()
{
    const auto package = ap::play::packageFullName();
    print(std::format("実行ファイル: {}\n", ap::play::executablePath()));
    print(std::format("MSIX パッケージ: {}\n", package.empty() ? "なし（通常の exe として実行中）" : package));
    return 0;
}

int asioList()
{
    const auto drivers = ap::play::listAsioDrivers();
    print(std::format("MSIX パッケージ: {}\n", ap::play::packageFullName().empty() ? "なし" : "あり"));
    if (drivers.empty())
    {
        print("ASIO ドライバーが見つかりません（HKLM\\SOFTWARE\\ASIO）。\n");
        return 1;
    }
    for (std::size_t i = 0; i < drivers.size(); ++i)
    {
        const auto& d = drivers[i];
        print(std::format("{:2}. {}\n    CLSID: {}\n    DLL: {}\n", i + 1, d.name, d.clsid,
                          d.dllPath.empty() ? "（登録が見つかりません）" : d.dllPath));
    }
    return 0;
}

int asioLoad(const std::string& which)
{
    const auto drivers = ap::play::listAsioDrivers();
    print(std::format("MSIX パッケージ: {}\n", ap::play::packageFullName().empty() ? "なし" : "あり"));
    std::vector<std::size_t> targets;
    if (which == "all")
    {
        for (std::size_t i = 0; i < drivers.size(); ++i)
            targets.push_back(i);
    }
    else
    {
        const auto index = std::stoul(which);
        if (index == 0 || index > drivers.size())
            throw std::runtime_error("ASIO ドライバーの番号が範囲外です（--asio-list で確かめてください）");
        targets.push_back(index - 1);
    }

    int failures = 0;
    for (const auto i : targets)
    {
        const auto r = ap::play::tryLoadAsioDriver(drivers[i]);
        print(std::format("{:2}. {}: {}（0x{:08X}）\n", i + 1, drivers[i].name, r.loaded ? "読み込めた" : "読み込めない",
                          static_cast<unsigned long>(r.hresult)));
        if (!r.loadedModulePath.empty())
            print(std::format("    読み込まれた DLL: {}\n", r.loadedModulePath));
        failures += r.loaded ? 0 : 1;
    }
    return failures == 0 ? 0 : 1;
}

void printWindowsDefault(const WasapiExclusiveOutput& out)
{
    const auto f = out.windowsDefaultFormat();
    print(std::format("  Windows の既定の形式（共有モード）: {}\n", f ? describe(*f) : "取得できませんでした"));
}

int listFormats(const Options& o)
{
    WasapiExclusiveOutput out(resolveDeviceId(o.device));
    print(std::format("出力: {}\n", out.deviceName()));
    printWindowsDefault(out);
    print("排他モードで受け付ける形式（○ = 可、- = 不可）:\n");
    print("              16bit  24bit  24bit(32bit コンテナ)  32bit\n");

    static constexpr std::pair<std::uint16_t, std::uint16_t> kLayouts[] = {{16, 16}, {24, 24}, {32, 24}, {32, 32}};
    for (std::uint32_t rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u})
    {
        std::string row = std::format("  {:>7g} kHz", rate / 1000.0);
        for (const auto& [container, valid] : kLayouts)
        {
            const PcmFormat f{rate, 2, container, valid, SampleType::Int, 0};
            const bool ok = out.checkFormat(f).supported;
            row += container == 32 && valid == 24 ? std::format("  {:^21}", ok ? "○" : "-")
                                                   : std::format("  {:^5}", ok ? "○" : "-");
        }
        print(row + "\n");
    }
    return 0;
}

void printSource(const fs::path& path, const PcmSource& src, const std::string& hash)
{
    print(std::format("ファイル: {}\n", reinterpret_cast<const char*>(path.u8string().c_str())));
    print(std::format("  形式: {}\n", describe(src.format())));
    print(std::format("  長さ: {}（{} フレーム）\n", duration(src.totalFrames(), src.format().sampleRate),
                      src.totalFrames()));
    if (!hash.empty())
        print(std::format("  PCM の SHA-256: {}\n", hash));
}

int runDummy(const Options& o, const fs::path& in)
{
    WavFileSource src(in);
    const auto inputHash = toHex(sha256File(in, static_cast<WavFileSource&>(src).dataOffset(),
                                            src.totalFrames() * src.format().bytesPerFrame()));
    printSource(in, src, inputHash);

    const fs::path outPath = fs::path(o.dummy);
    DummyOutput out({.path = outPath, .periodFrames = {4096}, .check = {}});
    print(std::format("出力: {}\n", out.deviceName()));

    const auto report = play(src, out, PlaybackOptions{.allowRepack = !o.exactOnly});
    const auto outputHash = toHex(sha256File(outPath));
    print(std::format("  送った形式: {}\n  詰め替え: {}\n", describe(report.plan.output), repackText(report.plan)));
    print(std::format("  出力の SHA-256: {}\n", outputHash));
    print(std::format("  {}\n", outputHash == inputHash ? "一致（ビットパーフェクト）"
                                : report.plan.repack != Repack::None
                                    ? "不一致（詰め替えたため。詰め替え前の値は保たれています）"
                                    : "不一致"));
    return outputHash == inputHash || report.plan.repack != Repack::None ? 0 : 1;
}

int runWasapi(const Options& o, const fs::path& in)
{
    WavFileSource file(in);
    printSource(in, file, {});

    WasapiExclusiveOutput out(resolveDeviceId(o.device));
    print(std::format("出力: {}（排他 WASAPI、イベント駆動）\n", out.deviceName()));
    printWindowsDefault(out);
    printVolume(out.endpointVolume());

    const auto negotiation = negotiate(file.format(), out, !o.exactOnly);
    if (!negotiation.plan)
    {
        print("  再生できません: " + rejectionText(negotiation.exactCheck, file.format()) + "\n");
        return 3;
    }
    print(std::format("  送る形式: {}\n  詰め替え: {}\n", describe(negotiation.plan->output),
                      repackText(*negotiation.plan)));
    if (o.checkOnly)
        return 0;

    // Read the whole file first so the render thread never waits for the disk
    // (dropout prevention only; proper prefetching comes in task 1-3).
    auto memory = MemoryPcmSource::loadAll(file);

    std::atomic<std::uint64_t> played{0};
    struct Counting final : PcmSource
    {
        PcmSource& inner;
        std::atomic<std::uint64_t>& count;
        Counting(PcmSource& i, std::atomic<std::uint64_t>& c) : inner(i), count(c) {}
        const PcmFormat& format() const noexcept override { return inner.format(); }
        std::uint64_t totalFrames() const noexcept override { return inner.totalFrames(); }
        std::size_t read(std::span<std::byte> dst) override
        {
            const auto n = inner.read(dst);
            count += n;
            return n;
        }
    } counting(memory, played);

    g_output = &out;
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);

    std::atomic<bool> done{false};
    const auto total = memory.totalFrames();
    const auto rate = memory.format().sampleRate;
    std::thread progress([&] {
        while (!done)
        {
            print(std::format("\r  再生中 {} / {}（Ctrl+C で停止）   ", duration(played, rate), duration(total, rate)));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    PlaybackReport report;
    try
    {
        report = play(counting, out, PlaybackOptions{.allowRepack = !o.exactOnly});
    }
    catch (...)
    {
        done = true;
        progress.join();
        g_output = nullptr;
        print("\n");
        throw;
    }
    done = true;
    progress.join();
    g_output = nullptr;

    const auto& s = report.stats;
    print(std::format("\n{}: 送ったフレーム {} / {}、周期 {:.2f} ms（{} フレーム）、デバイス待ちのタイムアウト {} 回\n",
                      s.stoppedByRequest ? "停止" : "完了", s.framesRendered, total, s.periodMilliseconds,
                      s.periodFrames, s.lateWakeups));
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    try
    {
        const auto o = parse(argc, argv);
        if (!o)
        {
            print(kUsage);
            return 2;
        }

        if (o->env)
            return showEnvironment();
        if (o->asioList)
            return asioList();
        if (!o->asioLoad.empty())
            return asioLoad(o->asioLoad);

        ScopedComInit com;
        if (o->list)
            return listDevices();
        if (o->formats)
            return listFormats(*o);

        const fs::path in{o->file}; // argv is in the ANSI code page, as is path(std::string)
        return o->dummy.empty() ? runWasapi(*o, in) : runDummy(*o, in);
    }
    catch (const FormatNotSupportedError& e)
    {
        printErr("再生できません: " + rejectionText(e.check(), e.source()) + "\n");
        return 3;
    }
    catch (const OutputException& e)
    {
        printErr("エラー: " + outputErrorText(e) + "\n");
        return 4;
    }
    catch (const WavFormatError& e)
    {
        printErr(std::string("このファイルは読めません: ") + e.what() + "\n");
        return 5;
    }
    catch (const std::exception& e)
    {
        printErr(std::string("エラー: ") + e.what() + "\n\n" + kUsage);
        return 1;
    }
}

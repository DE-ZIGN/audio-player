// ap_play: prototype player for steps S1/S2 and tasks 1-1/1-2. Plays WAV
// files bit-perfect through exclusive, event-driven WASAPI as a gapless
// queue, or writes the engine output to a file (dummy output).
//
//   ap_play --list | --formats | --watch
//   ap_play FILE... [--device N|ID] [--exact-only] [--check]
//   ap_play FILE... --dummy OUT.raw [--exact-only]

#include <ap/engine/aac_file_source.h>
#include <ap/engine/aiff_file_source.h>
#include <ap/engine/alac_file_source.h>
#include <ap/engine/audio_file.h>
#include <ap/engine/dsd_source.h>
#include <ap/engine/flac_file_source.h>
#include <ap/engine/mp3_file_source.h>
#include <ap/engine/mp4_reader.h>
#include <ap/engine/output/asio_output.h>
#include <ap/engine/output/asio_probe.h>
#include <ap/engine/output/asio_sample_types.h>
#include <ap/engine/output/device_watcher.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/output/format_probe.h>
#include <ap/engine/output/wasapi_exclusive_output.h>
#include <ap/engine/player.h>
#include <ap/engine/playback.h>
#include <ap/engine/prefetching_source.h>
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
#include <mutex>
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
    "  ap_play --watch                        出力デバイスの変化（抜き差し、既定の変更など）を表示する\n"
    "  ap_play FILE... --stress N             N 個のスレッドで CPU に負荷をかけながら再生し、音切れの回数を表示する\n"
    "  ap_play FILE... --dop                  DSD（.dsf/.dff）を DoP で送る（DoP に対応した DAC のときだけ。対応していないとノイズになる）\n"
    "  ap_play FILE... --inject-stall S:MS    1 曲目の S 秒の時点で再生スレッドを MS ミリ秒止める（音切れの検出の確認用）\n"
    "  ap_play FILE... [--device N|ID]        排他 WASAPI で順に再生（既定のデバイス、または一覧の番号か ID）\n"
    "               [--exact-only]            コンテナの詰め替え（24bit → 32bit コンテナなど）もしない\n"
    "               [--check]                 再生せず、送る形式だけを確かめる\n"
    "  ap_play FILE... --dummy OUT.raw        デバイスに送るはずのバイト列をファイルに書き、ハッシュを比べる\n"
    "\n"
    "複数のファイルは順に再生します。形式が同じなら出力を開き直さずにつなげ（ギャップレス）、\n"
    "形式が違えば開き直して、切り替えにかかった時間を表示します。\n"
    "  ap_play --env                          実行環境（MSIX パッケージの中かどうか）\n"
    "  ap_play --asio-list                    ASIO ドライバーの一覧（別のプロセスで読み込んで、使えるレートなども表示）\n"
    "  ap_play FILE... --asio N|NAME          ASIO で順に再生（--asio-list の番号か名前。--check、--dummy 以外の\n"
    "                                         オプションも使える。レートは曲ごとにドライバーへ設定する）\n"
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
        {
            std::string now;
            for (const auto r : c.acceptedRates)
                now += std::format("{}{:g} kHz", now.empty() ? "" : "、", r / 1000.0);
            return std::format("このデバイスは排他モードで {} に対応していません。レートの変換はしません。{}"
                               "デバイスの設定でレートを {} にすると再生できる場合があります。",
                               rate, now.empty() ? "" : std::format("このデバイスが今受け付けるのは {} です。", now),
                               rate);
        }
        case FormatRejection::BitDepth:
            return std::format("このデバイスは {} には対応していますが、この形式（{}）には対応していません。",
                               rate, describe(f));
        case FormatRejection::Channels:
            return std::format("このデバイスは {} チャンネルに対応していません。", f.channels);
        case FormatRejection::SampleType:
            return f.type == SampleType::Float
                       ? "このデバイスは浮動小数点の形式に対応していません。MP3・AAC など非可逆の形式は浮動小数点で出てくるため、"
                         "ディザをかけて整数にする処理経路（M3）ができるまで、このデバイスでは再生できません（勝手に変換はしません）。"
                       : "このデバイスはこの形式（整数 / 浮動小数点）に対応していません。";
        default:
            return std::format("このデバイスはこの形式に対応していません（0x{:08X}）。",
                               static_cast<unsigned long>(c.nativeError));
    }
}

std::string outputErrorText(OutputError code, const std::string& detail)
{
    switch (code)
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
            return std::format("デバイスのエラーです（{}）。", detail);
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

Player* g_player = nullptr;
std::atomic<bool> g_interrupted{false};

BOOL WINAPI onConsoleCtrl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT)
    {
        g_interrupted = true;
        if (g_player)
            g_player->stop();
        return TRUE;
    }
    return FALSE;
}

struct Options
{
    std::vector<std::string> files;
    bool watch = false;
    int stressThreads = 0;
    bool allowDop = false;     // --dop
    double stallAtSeconds = 0.0; // --inject-stall S:MS (first track only)
    int stallMilliseconds = 0;
    std::string device;
    std::string dummy;
    bool exactOnly = false;
    bool checkOnly = false;
    bool list = false;
    bool formats = false;
    bool env = false;
    bool asioList = false;
    std::string asioLoad;
    std::string asio;          // --asio N|NAME
    std::string asioProbeOne;  // --asio-probe-one CLSID (child process mode)
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
        else if (a == "--watch")
            o.watch = true;
        else if (a == "--dop")
            o.allowDop = true;
        else if (a == "--stress")
            o.stressThreads = std::max(0, std::stoi(value()));
        else if (a == "--inject-stall")
        {
            const auto v = value();
            const auto colon = v.find(':');
            if (colon == std::string::npos)
                throw std::runtime_error("--inject-stall は 秒:ミリ秒 で指定してください（例 3:50）");
            o.stallAtSeconds = std::stod(v.substr(0, colon));
            o.stallMilliseconds = std::max(0, std::stoi(v.substr(colon + 1)));
        }
        else if (a == "--asio-list")
            o.asioList = true;
        else if (a == "--asio")
            o.asio = value();
        else if (a == "--asio-probe-one")
            o.asioProbeOne = value();
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
        else
            o.files.push_back(a);
    }
    if (!o.list && !o.formats && !o.env && !o.watch && !o.asioList && o.asioLoad.empty() && o.asioProbeOne.empty()
        && o.files.empty())
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

// Loads the driver in a child process (this exe with --asio-probe-one), so a
// driver that crashes its host does not take this process down.
AsioProbeResult probeIsolated(const AsioDriverInfo& d)
{
    const std::wstring self = std::filesystem::path(ap::play::executablePath()).wstring();
    const std::wstring clsid(d.clsid.begin(), d.clsid.end()); // CLSID text is ASCII
    return probeAsioDriverIsolated(self, L"--asio-probe-one " + clsid);
}

std::string ratesText(const std::vector<std::uint32_t>& rates)
{
    std::string s;
    for (const auto r : rates)
        s += std::format("{}{:g}", s.empty() ? "" : "、", r / 1000.0);
    return s.empty() ? "なし" : s + " kHz";
}

int asioProbeOne(const std::string& clsid)
{
    for (const auto& d : listAsioDrivers())
        if (_stricmp(d.clsid.c_str(), clsid.c_str()) == 0)
        {
            print(formatProbeLine(probeAsioDriverInProcess(d)) + "\n");
            return 0;
        }
    AsioProbeResult r;
    r.message = "driver not registered";
    print(formatProbeLine(r) + "\n");
    return 0;
}

int asioList()
{
    const auto drivers = listAsioDrivers();
    print(std::format("MSIX パッケージ: {}\n", ap::play::packageFullName().empty() ? "なし" : "あり"));
    if (drivers.empty())
    {
        print("ASIO ドライバーが見つかりません（HKLM\\SOFTWARE\\ASIO）。\n");
        return 1;
    }
    print("（各ドライバーは別のプロセスで読み込んで確かめています）\n");
    for (std::size_t i = 0; i < drivers.size(); ++i)
    {
        const auto& d = drivers[i];
        const auto r = probeIsolated(d);
        print(std::format("{:2}. {}\n", i + 1, d.name));
        switch (r.status)
        {
            case AsioProbeResult::Status::Ok:
            {
                const auto& x = r.details;
                print(std::format("    ドライバー名: {}（版 {}）、出力 {} ch、入力 {} ch\n", x.driverName, x.driverVersion,
                                  x.outputChannels, x.inputChannels));
                print(std::format("    サンプル形式: {}、バッファー {} フレーム（{}〜{}）、今のレート {:g} kHz\n",
                                  asio::sampleTypeName(x.sampleType), x.preferredBufferSize, x.minBufferSize,
                                  x.maxBufferSize, x.currentSampleRate / 1000.0));
                print(std::format("    使えるレート: {}\n", ratesText(r.rates)));
                break;
            }
            case AsioProbeResult::Status::LoadFailed:
                print(std::format("    使えません: ドライバーが初期化に失敗しました（{}）\n", r.message));
                break;
            case AsioProbeResult::Status::Crashed:
                print(std::format("    使えません: このドライバーは読み込むと異常終了します（{}）\n", r.message));
                break;
            case AsioProbeResult::Status::TimedOut:
                print("    使えません: ドライバーが応答しませんでした\n");
                break;
        }
    }
    return 0;
}

// --asio N|NAME → the driver, from the 1-based number in --asio-list or its name.
AsioDriverInfo resolveAsioDriver(const std::string& arg)
{
    const auto drivers = listAsioDrivers();
    if (!arg.empty() && arg.find_first_not_of("0123456789") == std::string::npos)
    {
        const auto index = std::stoul(arg);
        if (index >= 1 && index <= drivers.size())
            return drivers[index - 1];
    }
    for (const auto& d : drivers)
        if (d.name == arg)
            return d;
    throw OutputException(OutputError::DeviceNotFound, "ASIO driver not found: " + arg);
}

int asioLoad(const std::string& which)
{
    const auto drivers = listAsioDrivers();
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
    print("              16bit  24bit  24bit(32bit コンテナ)  32bit  32bit 浮動小数点\n");

    for (const auto rate : standardSampleRates())
    {
        std::string row = std::format("  {:>7g} kHz", rate / 1000.0);
        for (const auto& f : standardIntLayouts(rate, 2))
        {
            const bool ok = out.checkFormat(f).supported;
            row += f.containerBits == 32 && f.validBits == 24 ? std::format("  {:^21}", ok ? "○" : "-")
                                                               : std::format("  {:^5}", ok ? "○" : "-");
        }
        const PcmFormat f32{rate, 2, 32, 32, SampleType::Float, 0};
        row += std::format("  {:^15}", out.checkFormat(f32).supported ? "○" : "-");
        print(row + "\n");
    }
    print("（MP3・AAC など非可逆の形式は 32bit 浮動小数点で出てくる。浮動小数点を受け付けないデバイスでは、"
          "ディザをかけて整数にする処理経路（M3）ができるまで再生できない）\n");
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

// --inject-stall: blocks the playback thread once, inside read(), when the
// position reaches `frame`. Used to check on real hardware that a dropout is
// both heard and counted.
class RenderStallSource final : public PcmSource
{
public:
    RenderStallSource(std::unique_ptr<PcmSource> inner, std::uint64_t frame, std::chrono::milliseconds stall)
        : inner_(std::move(inner)), frame_(frame), stall_(stall)
    {
    }
    const PcmFormat& format() const noexcept override { return inner_->format(); }
    std::uint64_t totalFrames() const noexcept override { return inner_->totalFrames(); }
    std::uint64_t position() const noexcept override { return inner_->position(); }
    bool seek(std::uint64_t f) override { return inner_->seek(f); }
    SourceHealth health() const noexcept override { return inner_->health(); }
    void waitUntilReady() override { inner_->waitUntilReady(); }
    std::size_t read(std::span<std::byte> dst) override
    {
        if (!done_ && inner_->position() >= frame_)
        {
            done_ = true;
            std::this_thread::sleep_for(stall_);
        }
        return inner_->read(dst);
    }

private:
    std::unique_ptr<PcmSource> inner_;
    std::uint64_t frame_;
    std::chrono::milliseconds stall_;
    bool done_ = false;
};

std::string transitionText(const TrackTransition& t)
{
    if (t.kind == TransitionKind::Gapless)
        return std::format("{} → {} 曲目: ギャップレス（形式が同じなので出力を開き直さずにつなげた）", t.fromIndex + 1,
                           t.toIndex + 1);
    return std::format("{} → {} 曲目: 形式の切り替え {} → {}（出力を開き直した。データが途切れてから次の曲のデータを"
                       "送り始めるまで約 {:.0f} ms）",
                       t.fromIndex + 1, t.toIndex + 1, describe(t.fromFormat), describe(t.toFormat),
                       t.switchMilliseconds);
}

std::string integrityText(const IntegrityCheck& c)
{
    switch (c.state)
    {
        case IntegrityState::Passed:  return "MD5 一致（デコードした音声データが、ファイルに記録された値と同じ）";
        case IntegrityState::Pending: return "照合中（最後まで読んでいない）";
        case IntegrityState::Skipped: return "照合できない（途中にシークした）";
        case IntegrityState::Failed:
            return c.decodeErrors > 0
                       ? std::format("デコーダーがファイルの中の {} か所でデータの誤りを見つけた（ファイルが壊れている可能性がある）",
                                     c.decodeErrors)
                       : "MD5 が一致しない（ファイルが壊れている可能性がある）";
        case IntegrityState::NotAvailable: return "照合用の値（MD5）がファイルにない";
    }
    return {};
}

// The output part of the path, as the output reported it after opening.
std::string outputModeText(const SignalPath& p)
{
    const auto& o = p.output;
    const auto period = std::format("{} フレーム（{:.2f} ms）", o.periodFrames, o.periodMilliseconds);
    switch (o.api)
    {
        case OutputApi::WasapiExclusive:
            return std::format("排他 WASAPI{}・1 回に {}", o.eventDriven ? "（イベント駆動）" : "", period);
        case OutputApi::Asio:
        {
            std::string s = "ASIO";
            if (o.asio)
            {
                s += std::format(" {}（版 {}）・ドライバーの形式 {}", o.asio->driverName, o.asio->driverVersion,
                                 o.asio->sampleTypeName);
                if (o.layout == SampleLayout::PerChannelRightAligned)
                    s += "（32bit の枠の下位に右詰めで入れて送る。値は変わらない）";
            }
            s += "・バッファー " + period;
            if (o.asio && o.asio->rateChanged)
                s += std::format("・ドライバーのレートを {:g} → {:g} kHz に切り替えた", o.asio->rateBefore / 1000.0,
                                 o.format.sampleRate / 1000.0);
            return s;
        }
        case OutputApi::Dummy:
            return "ダミー出力（ファイルに書き出すだけ）・1 回に " + period;
    }
    return {};
}

std::string playerErrorText(const PlayerStatus& s)
{
    const auto& e = s.error;
    switch (e.kind)
    {
        case PlayerErrorKind::FormatNotSupported:
            return "再生できません: " + rejectionText(e.formatCheck, s.sourceFormat.value_or(PcmFormat{}));
        case PlayerErrorKind::Output:
            return "エラー: " + outputErrorText(e.outputError, e.detail);
        case PlayerErrorKind::Load:
            return std::format("{} 曲目を開けません: {}", s.queueIndex + 1, e.detail);
        case PlayerErrorKind::DopNotAllowed:
            return std::format("{} 曲目は DSD です。DoP で送るには --dop を付けてください。DoP に対応していない DAC では大きなノイズになるので、"
                               "対応している DAC のときだけ付けてください。", s.queueIndex + 1);
        default:
            return "エラー: " + e.detail;
    }
}

int exitCodeFor(PlayerErrorKind kind)
{
    switch (kind)
    {
        case PlayerErrorKind::None:               return 0;
        case PlayerErrorKind::FormatNotSupported: return 3;
        case PlayerErrorKind::Output:             return 4;
        case PlayerErrorKind::Load:               return 5;
        case PlayerErrorKind::DopNotAllowed:      return 3;
        default:                                  return 1;
    }
}

// Plays the files as a queue (WASAPI, or the dummy output when o.dummy is set).
int runQueue(const Options& o)
{
    const bool dummy = !o.dummy.empty();
    std::vector<QueueItem> items;
    Sha256 inputHash; // dummy only: hash of all data chunks in order

    for (std::size_t i = 0; i < o.files.size(); ++i)
    {
        const fs::path path{o.files[i]}; // argv is in the ANSI code page, as is path(std::string)
        const auto opened = openAudioFile(path);
        auto& src = *opened;
        print(std::format("{} 曲目: {}\n", i + 1, reinterpret_cast<const char*>(path.filename().u8string().c_str())));
        if (const auto dop = src.dop())
        {
            print(std::format("  形式: {}（{:g} MHz、{}）/ {}ch → DoP で {} にして送る、長さ {}\n",
                              dsdRateName(dop->dsdRate), dop->dsdRate / 1e6, dop->container, src.format().channels,
                              describe(src.format()), duration(src.totalFrames(), src.format().sampleRate)));
            print("  注意: DoP に対応していない DAC では、大きなノイズになります。DoP の経路では処理をかけられません。\n");
        }
        else
        {
            print(std::format("  形式: {}（{}）、長さ {}\n", describe(src.format()), src.containerName(),
                              duration(src.totalFrames(), src.format().sampleRate)));
        }
        if (dummy)
        {
            std::vector<std::byte> buf(65536 * static_cast<std::size_t>(src.format().bytesPerFrame()));
            while (const auto n = src.read(buf))
                inputHash.update(std::span(buf).first(n * src.format().bytesPerFrame()));
        }
        // Devices: read ahead on a separate thread so the playback thread never
        // waits for the disk (dropout prevention only). Dummy output: read the
        // file directly. The dummy takes data as fast as it can, not at the
        // playback rate, so it would outrun the read-ahead, which then fills
        // silence (as it must for a device); the byte check needs every byte.
        const bool stallHere = i == 0 && o.stallMilliseconds > 0;
        items.push_back({reinterpret_cast<const char*>(path.filename().u8string().c_str()),
                         [path, stallHere, dummy, at = o.stallAtSeconds, ms = o.stallMilliseconds]() -> std::unique_ptr<PcmSource> {
                             std::unique_ptr<PcmSource> src =
                                 dummy ? openAudioFile(path) : std::make_unique<PrefetchingSource>(openAudioFile(path));
                             if (!stallHere)
                                 return src;
                             const auto frame = static_cast<std::uint64_t>(at * src->format().sampleRate);
                             return std::make_unique<RenderStallSource>(std::move(src), frame,
                                                                        std::chrono::milliseconds(ms));
                         }});
    }

    Player::OutputFactory factory;
    if (dummy)
    {
        const fs::path outPath{o.dummy};
        factory = [outPath] {
            return std::make_unique<DummyOutput>(DummyOutput::Options{.path = outPath, .periodFrames = {4096}});
        };
        print(std::format("出力: ダミー出力（ファイル）: {}\n", o.dummy));
    }
    else if (!o.asio.empty())
    {
        const auto driver = resolveAsioDriver(o.asio);
        // Load it in a child process first: some drivers crash the process that loads them.
        const auto probe = probeIsolated(driver);
        if (probe.status != AsioProbeResult::Status::Ok)
        {
            printErr(std::format("ASIO ドライバー「{}」は使えません: {}\n", driver.name,
                                 probe.status == AsioProbeResult::Status::Crashed ? "読み込むと異常終了します"
                                 : probe.status == AsioProbeResult::Status::TimedOut ? "応答しませんでした"
                                                                                     : probe.message));
            return 4;
        }
        const auto& x = probe.details;
        print(std::format("出力: ASIO: {}（ドライバー名 {}、サンプル形式 {}）\n", driver.name, x.driverName,
                          asio::sampleTypeName(x.sampleType)));
        // The preferred buffer size belongs to the driver's current rate; some drivers (SSL, FiiO) change it
        // with the rate, so the size actually used is printed per track ("出力の方式").
        print(std::format("  使えるレート: {}（曲ごとにドライバーへ設定します）、今のレート {:g} kHz"
                          "（このレートでのバッファー {} フレーム。レートを変えると変わることがあります）\n",
                          ratesText(probe.rates), x.currentSampleRate / 1000.0, x.preferredBufferSize));

        if (o.checkOnly)
        {
            AsioOutput out("ASIO: " + driver.name, AsioOutput::factoryFor(driver));
            int worst = 0;
            for (std::size_t i = 0; i < o.files.size(); ++i)
            {
                const auto opened = openAudioFile(fs::path{o.files[i]});
                const auto& src = *opened;
                const auto n = negotiate(src.format(), out, !o.exactOnly);
                if (n.plan)
                    print(std::format("  {} 曲目: 送る形式 {}、詰め替え {}\n", i + 1, describe(n.plan->output),
                                      repackText(*n.plan)));
                else
                {
                    print(std::format("  {} 曲目: 再生できません: {}\n", i + 1, rejectionText(n.exactCheck, src.format())));
                    worst = 3;
                }
            }
            return worst;
        }
        factory = [driver] {
            return std::make_unique<AsioOutput>("ASIO: " + driver.name, AsioOutput::factoryFor(driver));
        };
    }
    else
    {
        const auto id = resolveDeviceId(o.device);
        WasapiExclusiveOutput probe(id);
        print(std::format("出力: {}（排他 WASAPI、イベント駆動）\n", probe.deviceName()));
        printWindowsDefault(probe);
        printVolume(probe.endpointVolume());

        if (o.checkOnly)
        {
            int worst = 0;
            for (std::size_t i = 0; i < o.files.size(); ++i)
            {
                const auto opened = openAudioFile(fs::path{o.files[i]});
                const auto& src = *opened;
                const auto n = negotiate(src.format(), probe, !o.exactOnly);
                if (n.plan)
                    print(std::format("  {} 曲目: 送る形式 {}、詰め替え {}\n", i + 1, describe(n.plan->output),
                                      repackText(*n.plan)));
                else
                {
                    print(std::format("  {} 曲目: 再生できません: {}\n", i + 1, rejectionText(n.exactCheck, src.format())));
                    worst = 3;
                }
            }
            return worst;
        }
        factory = [id] { return std::make_unique<WasapiExclusiveOutput>(id); };
    }

    Player player(factory, PlaybackOptions{.allowRepack = !o.exactOnly, .allowDop = o.allowDop});
    player.setQueue(std::move(items));
    if (player.status().state == PlayerState::Error)
    {
        printErr(playerErrorText(player.status()) + "\n");
        return exitCodeFor(player.status().error.kind);
    }

    g_player = &player;
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);

    // --stress N: keep N threads busy at normal priority while playing.
    std::atomic<bool> stopStress{false};
    std::vector<std::thread> stress;
    for (int i = 0; i < o.stressThreads; ++i)
        stress.emplace_back([&stopStress] {
            volatile std::uint64_t x = 0;
            while (!stopStress)
                x = x * 6364136223846793005ull + 1442695040888963407ull;
        });
    if (o.stressThreads > 0)
        print(std::format("  負荷: {} 個のスレッドで CPU を使い続けています\n", o.stressThreads));
    struct StressGuard
    {
        std::atomic<bool>& stop;
        std::vector<std::thread>& threads;
        ~StressGuard()
        {
            stop = true;
            for (auto& t : threads)
                t.join();
        }
    } stressGuard{stopStress, stress};

    player.play();

    std::size_t shownTransitions = 0;
    std::optional<PlaybackPlan> shownPlan;
    PlayerStatus s;
    for (;;)
    {
        s = player.status();
        for (; shownTransitions < s.transitions.size(); ++shownTransitions)
            print("\r  " + transitionText(s.transitions[shownTransitions]) + "                    \n");
        if (s.plan && (!shownPlan || !(shownPlan->output == s.plan->output)))
        {
            print(std::format("\r  送る形式: {}、詰め替え: {}                    \n", describe(s.plan->output),
                              repackText(*s.plan)));
            if (s.path)
                print(std::format("  出力の方式: {}\n  処理: {}\n", outputModeText(*s.path),
                                  s.path->dop ? "なし（DoP の経路では処理をかけられない）"
                                              : "なし（原音経路：音量・変換・ディザをかけない）"));
            shownPlan = s.plan;
        }
        if (s.state != PlayerState::Playing)
            break;
        const auto rate = s.sourceFormat ? s.sourceFormat->sampleRate : 1u;
        print(std::format("\r  再生中 {}/{} 曲目 {} / {}  音切れ {} 回（Ctrl+C で停止）   ", s.queueIndex + 1,
                          s.queueLength, duration(s.positionFrames, rate), duration(s.totalFrames, rate),
                          s.dropouts.total()));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    g_player = nullptr;
    player.waitUntilIdle();
    s = player.status();

    switch (s.state)
    {
        case PlayerState::Ended:   print("\n完了しました。\n"); break;
        case PlayerState::Stopped: print(std::format("\n停止しました（{} 曲目）。\n", s.queueIndex + 1)); break;
        case PlayerState::Error:   print("\n"); printErr(playerErrorText(s) + "\n"); break;
        default: break;
    }
    const auto& d = s.dropouts;
    print(std::format("音切れ: {} 回（デバイスへの受け渡しの遅れ {}、ファイルの読み込みの遅れ {}（無音にしたフレーム {}）、"
                      "デバイスの応答なし {}）\n",
                      d.total(), d.lateBuffers, d.readStarvations, d.starvedFrames, d.deviceTimeouts));
    print(std::format("再生スレッド: {}\n", d.realtimePriority ? "MMCSS（Pro Audio）で優先度を上げた"
                                                              : "通常の優先度（MMCSS は使っていない）"));
    print(std::format("ファイルの検証（{} 曲目）: {}\n", s.queueIndex + 1, integrityText(s.integrity)));
    if (!s.integrityFailures.empty())
    {
        std::string list;
        for (const auto i : s.integrityFailures)
            list += (list.empty() ? "" : "、") + std::to_string(i + 1);
        print(std::format("ファイルの検証: {} 曲目は MD5 が一致しなかった（ファイルが壊れている可能性がある）\n", list));
    }

    if (dummy && s.state == PlayerState::Ended)
    {
        const auto in = toHex(inputHash.finish());
        const auto out = toHex(sha256File(fs::path{o.dummy}));
        print(std::format("入力 PCM（全曲をつなげたもの）の SHA-256: {}\n出力の SHA-256:                         {}\n", in, out));
        const bool repacked = s.plan && s.plan->repack != Repack::None;
        print(in == out                      ? "一致（ビットパーフェクト、曲の境目に欠けも重なりもなし）\n"
              : repacked                     ? "不一致（詰め替えたため。詰め替え前の値は保たれています）\n"
              : s.dropouts.readStarvations > 0 ? "不一致（読み込みが間に合わず、無音で埋めたため）\n"
                                             : "不一致\n");
        if (in != out && !repacked)
            return 1;
    }
    return exitCodeFor(s.error.kind);
}

// Prints device changes until Ctrl+C.
int watchDevices()
{
    auto nameOf = [](const std::string& id) {
        for (const auto& d : WasapiExclusiveOutput::listDevices())
            if (d.id == id)
                return d.name;
        return std::string("（一覧にない）");
    };
    std::mutex printMutex;
    DeviceWatcher watcher([&](const DeviceEvent& e) {
        // Runs on a system thread; COM is available there (MTA).
        static const char* labels[] = {"追加", "削除", "状態の変化", "既定のデバイスの変更"};
        const auto label = labels[static_cast<int>(e.kind)];
        std::string name;
        try
        {
            name = nameOf(e.deviceId);
        }
        catch (...)
        {
        }
        std::lock_guard lock(printMutex);
        print(std::format("{}: {}\n    ID: {}\n", label, name, e.deviceId));
    });
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    print("出力デバイスの変化を待っています（Ctrl+C で終了）…\n");
    while (!g_interrupted)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
        if (!o->asioProbeOne.empty())
            return asioProbeOne(o->asioProbeOne);
        if (o->asioList)
            return asioList();
        if (!o->asioLoad.empty())
            return asioLoad(o->asioLoad);

        ScopedComInit com;
        if (o->list)
            return listDevices();
        if (o->formats)
            return listFormats(*o);

        if (o->watch)
            return watchDevices();
        return runQueue(*o);
    }
    catch (const FormatNotSupportedError& e)
    {
        printErr("再生できません: " + rejectionText(e.check(), e.source()) + "\n");
        return 3;
    }
    catch (const OutputException& e)
    {
        printErr("エラー: " + outputErrorText(e.code(), e.what()) + "\n");
        return 4;
    }
    catch (const WavFormatError& e)
    {
        printErr(std::string("このファイルは読めません: ") + e.what() + "\n");
        return 5;
    }
    catch (const UnsupportedAudioFileError& e)
    {
        printErr(std::string("このファイルは読めません（WAV、FLAC、AIFF、ALAC・AAC（.m4a）、MP3、DSF、DFF に対応）: ") + e.what() + "\n");
        return 5;
    }
    catch (const FlacFormatError& e)
    {
        printErr(std::string("このファイルは読めません（FLAC）: ") + e.what() + "\n");
        return 5;
    }
    catch (const AiffFormatError& e)
    {
        printErr(std::string("このファイルは読めません（AIFF）: ") + e.what() + "\n");
        return 5;
    }
    catch (const AlacFormatError& e)
    {
        printErr(std::string("このファイルは読めません（ALAC）: ") + e.what() + "\n");
        return 5;
    }
    catch (const Mp4FormatError& e)
    {
        printErr(std::string("このファイルは読めません（MP4）: ") + e.what() + "\n");
        return 5;
    }
    catch (const AacFormatError& e)
    {
        printErr(std::string("このファイルは読めません（AAC）: ") + e.what() + "\n");
        return 5;
    }
    catch (const Mp3FormatError& e)
    {
        printErr(std::string("このファイルは読めません（MP3）: ") + e.what() + "\n");
        return 5;
    }
    catch (const DsdFormatError& e)
    {
        printErr(std::string("このファイルは読めません（DSD）: ") + e.what() + "\n");
        return 5;
    }
    catch (const std::exception& e)
    {
        printErr(std::string("エラー: ") + e.what() + "\n\n" + kUsage);
        return 1;
    }
}

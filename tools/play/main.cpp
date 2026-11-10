// ap_play: prototype player for steps S1/S2 and tasks 1-1/1-2. Plays WAV
// files bit-perfect through exclusive, event-driven WASAPI as a gapless
// queue, or writes the engine output to a file (dummy output).
//
//   ap_play --list | --formats | --watch
//   ap_play FILE... [--device N|ID] [--exact-only] [--check]
//   ap_play FILE... --dummy OUT.raw [--exact-only]

#include <ap/engine/aac_file_source.h>
#include <ap/engine/abx.h>
#include <ap/engine/aiff_file_source.h>
#include <ap/engine/alac_file_source.h>
#include <ap/engine/audio_file.h>
#include <ap/engine/dsd_source.h>
#include <ap/engine/dsp/comparison.h>
#include <ap/engine/dsp/eq.h>
#include <ap/engine/dsp/isp_safe.h>
#include <ap/engine/dsp/limiter.h>
#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/dsp/true_peak.h>
#include <ap/engine/dsp/volume.h>
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
#include <conio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
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
    "  ap_play FILE... --from S [--for S]     1 曲目の S 秒から再生する。--for S は S 秒で止める（実機での試聴用）\n"
    "  ap_play FILE... --volume DB            デジタル音量（0 以下の dB。0 で原音経路のまま、下げると処理経路）\n"
    "  ap_play --analyze FILE...              サンプルピーク、True Peak（8 倍オーバーサンプリング）、ラウドネスと ReplayGain を測る（再生はしない）\n"
    "  ap_play FILE... --replaygain track|album  再生の前に各曲を解析し、ReplayGain 2.0（-18 LUFS）を処理経路でかける\n"
    "  ap_play FILE... --eq PRESET.txt        パラメトリック EQ（AutoEQ の ParametricEQ.txt、Equalizer APO の書き方）。\n"
    "                                         最小位相、プリアンプは自動（最大のブーストを 0 dB にする）\n"
    "  ap_play FILE... --compare original|processed  聴き比べ（原音と音作りの両方を作り、音作り側を原音の音量に合わせる）。\n"
    "               [--compare-at S]          最初にどちらを出すか。--compare-at S で S 秒の時点でもう一方に切り替える（5 ms のクロスフェード、何回でも）\n"
    "  ap_play FILE --abx [N] [--eq ...]       ABX（既定 16 回）。キー a/b/x で聴く音を切り替え、1 で「X は A」、2 で「X は B」と答える。\n"
    "                                         q で中断。正解数と p 値は最後に出す。--from S から --for S 秒（既定 30 秒）の区間を\n"
    "                                         くり返し流し、音作り側の音量はその区間を解析して原音に合わせる\n"
    "  ap_play FILE... --isp-safe off|measured|fixed  ISP セーフ（True Peak を -0.5 dBTP 以下に下げる。measured は解析した値から、\n"
    "                                         fixed は常に -3 dB）。off でも解析して、0 dBTP を超える曲を原音経路で鳴らすときに警告する\n"
    "  ap_play FILE... --volume-at S:DB       再生を始めてから S 秒で音量を DB にする（何回でも指定できる。0 に戻すと原音経路に戻る）\n"
    "\n"
    "複数のファイルは順に再生します。形式が同じなら出力を開き直さずにつなげ（ギャップレス）、\n"
    "形式が違えば開き直して、切り替えにかかった時間を表示します。\n"
    "  ap_play --env                          実行環境（MSIX パッケージの中かどうか）\n"
    "  ap_play --asio-list                    ASIO ドライバーの一覧（別のプロセスで読み込んで、使えるレートなども表示）\n"
    "  ap_play FILE... --asio N|NAME          ASIO で順に再生（--asio-list の番号か名前。--check、--dummy 以外の\n"
    "                                         オプションも使える。レートは曲ごとにドライバーへ設定する）\n"
    "  ap_play --asio-load N|all              ASIO ドライバーを読み込めるか試す（再生はしない）\n"
    "\n"
    "既定は原音経路です（音量、変換、ディザをかけない）。--volume で音量を下げたときと、浮動小数点の曲（MP3・AAC）を\n"
    "浮動小数点を受け付けないデバイスで鳴らすときだけ、64bit の処理経路を通して TPDF ディザをかけて整数にします。\n"
    "整数の曲でデバイスが受け付けない形式は、変換せずに理由を表示します。\n";

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
            return f.type == SampleType::Int
                       ? "このデバイスは整数の形式に対応していません（浮動小数点だけ）。原音経路では変換しないため、この曲は再生できません。"
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
    bool analyze = false;      // --analyze
    ReplayGainMode replayGain = ReplayGainMode::Off; // --replaygain track|album
    std::optional<IspSafeMode> ispSafe;              // --isp-safe off|measured|fixed (set: analyse the files)
    double fromSeconds = 0.0;  // --from S (first track)
    double forSeconds = 0.0;   // --for S (stop after; 0 = to the end)
    double volumeDb = 0.0;     // --volume DB
    std::string eqPreset;      // --eq FILE
    std::optional<ComparisonSide> compare; // --compare original|processed (the side to start with)
    std::vector<double> compareAt;         // --compare-at S (switch sides)
    int abxTrials = 0;                     // --abx [N] (0: no ABX)
    std::vector<std::pair<double, double>> volumeAt; // --volume-at S:DB (seconds since play, dB)
};

double parseDb(const std::string& v)
{
    const double db = std::stod(v);
    if (db > 0.0)
        throw std::runtime_error("音量は 0 dB 以下で指定してください（デジタル音量は上げられません）");
    return db;
}

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
        else if (a == "--analyze")
            o.analyze = true;
        else if (a == "--isp-safe")
        {
            const auto v = value();
            if (v == "off")
                o.ispSafe = IspSafeMode::Off;
            else if (v == "measured")
                o.ispSafe = IspSafeMode::Measured;
            else if (v == "fixed")
                o.ispSafe = IspSafeMode::Fixed;
            else
                throw std::runtime_error("--isp-safe は off、measured、fixed のどれかです");
        }
        else if (a == "--replaygain")
        {
            const auto v = value();
            if (v == "track")
                o.replayGain = ReplayGainMode::Track;
            else if (v == "album")
                o.replayGain = ReplayGainMode::Album;
            else if (v == "off")
                o.replayGain = ReplayGainMode::Off;
            else
                throw std::runtime_error("--replaygain は track、album、off のどれかです");
        }
        else if (a == "--compare")
        {
            const auto v = value();
            if (v == "original")
                o.compare = ComparisonSide::Original;
            else if (v == "processed")
                o.compare = ComparisonSide::Processed;
            else
                throw std::runtime_error("--compare は original か processed です");
        }
        else if (a == "--abx")
        {
            o.abxTrials = AbxTest::kDefaultTrials;
            if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])))
                o.abxTrials = std::max(1, std::stoi(argv[++i]));
        }
        else if (a == "--compare-at")
            o.compareAt.push_back(std::stod(value()));
        else if (a == "--eq")
            o.eqPreset = value();
        else if (a == "--from")
            o.fromSeconds = std::max(0.0, std::stod(value()));
        else if (a == "--for")
            o.forSeconds = std::max(0.0, std::stod(value()));
        else if (a == "--volume")
            o.volumeDb = parseDb(value());
        else if (a == "--volume-at")
        {
            const auto v = value();
            const auto colon = v.find(':');
            if (colon == std::string::npos)
                throw std::runtime_error("--volume-at は 秒:dB で指定してください（例 5:-6）");
            o.volumeAt.emplace_back(std::stod(v.substr(0, colon)), parseDb(v.substr(colon + 1)));
        }
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
          "処理経路で TPDF ディザをかけて整数にして送る）\n");
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
    if (t.kind == TransitionKind::ProcessingChanged)
        return std::format("{} 曲目: 処理経路と原音経路の切り替え（音量などが変わったため、今の位置で出力を開き直した。"
                           "データが途切れてから続きを送り始めるまで約 {:.0f} ms）",
                           t.fromIndex + 1, t.switchMilliseconds);
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

std::string processingText(const PlaybackPlan& p, bool dop)
{
    if (dop)
        return "なし（DoP の経路では処理をかけられない）";
    if (!p.processing)
        return "なし（原音経路：音量・変換・ディザをかけない）";
    std::string parts;
    for (const auto& n : p.processingStages)
        parts += (parts.empty() ? "" : "、")
               + (n == "volume"       ? std::string("デジタル音量")
                  : n == "replaygain" ? std::string("ReplayGain")
                  : n == "isp-safe"   ? std::string("ISP セーフ")
                  : n == "eq"         ? std::string("パラメトリック EQ")
                  : n == "comparison" ? std::string("聴き比べ")
                  : n == "limiter"    ? std::string("保険のリミッター")
                                      : n);
    if (p.processingRequired)
        parts += std::string(parts.empty() ? "" : "、") + "浮動小数点の曲を整数にする（デバイスが浮動小数点を受け付けないため）";
    if (p.dither)
        parts += std::string(parts.empty() ? "" : "、") + "TPDF ディザ";
    return "あり（64bit 浮動小数点の処理経路：" + parts + "）";
}

// The BIT-PERFECT line (task 4-2), from the engine's judgement.
std::string bitPerfectLine(const PlayerStatus& s)
{
    if (!s.bitPerfect)
        return "—";
    const auto& v = *s.bitPerfect;
    std::string device;
    if (v.deviceVolume)
        device = std::format("。デバイス側の音量 {}：DAC に届いたあとで信号が変わる（この判定はドライバーまで）",
                             v.deviceMuted ? std::string("ミュート") : std::format("{:.0f}%", *v.deviceVolume * 100));
    const auto db = [](double x) { return std::isfinite(x) ? std::format("{:+.1f} dB", x) : std::string("-∞ dB"); };
    if (v.bitPerfect)
    {
        std::string notes;
        const auto add = [&](const char* n) { notes += (notes.empty() ? "。" : "、") + std::string(n); };
        if (v.dop)
            add("DSD を DoP でそのまま");
        if (v.repacked)
            add("コンテナの詰め替えあり、値は同じ");
        if (v.floatSource)
            add("非可逆の曲をデコードした値のまま");
        if (v.dummyOutput)
            add("ダミー出力（ファイル）");
        return "BIT-PERFECT（アプリからドライバーまで値をそのまま" + notes + "）" + device;
    }
    std::string reasons;
    const auto add = [&](const std::string& r) { reasons += (reasons.empty() ? "" : "、") + r; };
    for (const auto& st : v.stages)
    {
        if (st.name == "limiter" || st.name == "comparison")
            continue;
        if (st.name == "volume" && st.gainDb)
            add("DIGITAL VOL " + db(*st.gainDb));
        else if (st.name == "replaygain" && st.gainDb)
            add("ReplayGain " + db(*st.gainDb));
        else if (st.name == "eq" && st.gainDb)
            add("パラメトリック EQ（プリアンプ " + db(*st.gainDb) + "）");
        else if (st.name == "isp-safe" && st.gainDb)
            add("ISP セーフ " + db(*st.gainDb));
        else
            add(st.name);
    }
    if (v.quantized)
        add("浮動小数点の曲を整数に変換");
    if (v.comparison)
        add("聴き比べ中（原音側も値は同じだが処理経路を通る）");
    if (v.dither)
        add("TPDF ディザ");
    if (v.deliveryMismatch && v.delivery)
        add(std::format("自己確認で不一致（{} フレーム）", v.delivery->mismatchedFrames));
    return "BIT-PERFECT ではない：" + reasons + device;
}

// The self-check line (task 4-3), with how far it reaches.
std::string selfCheckLine(const PlayerStatus& s)
{
    if (s.plan && s.plan->processing)
        return "処理経路では行わない（値を変える経路のため）";
    if (!s.delivery)
        return "—";
    const auto& d = *s.delivery;
    if (d.mismatchedFrames > 0)
        return std::format("ドライバーに渡した値のうち {} フレーム分（1 フレーム＝全チャンネル 1 組の値）が、"
                           "ファイルから読み出した値と違っていた。アプリの不具合と考えられる",
                           d.mismatchedFrames);
    if (d.matchedFrames == 0)
        return "まだ比べていない（データを渡す前に終わった）";
    const double rate = s.plan ? s.plan->output.sampleRate : 0.0;
    return std::format("{}（{} フレーム）、ドライバーに渡した値はすべて、ファイルから読み出した値と一致した。"
                       "確かめられるのはアプリからドライバーまで。ドライバーの中、USB などの転送、DAC の中は確かめられない",
                       rate > 0 ? std::format("{:.1f} 秒分", static_cast<double>(d.matchedFrames) / rate)
                                : std::string("全部"),
                       d.matchedFrames);
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

// Engine error messages are English with file paths in the ANSI code page
// (path::string()); the console prints UTF-8.
std::string fromAnsi(const char* text)
{
    const int wn = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (wn <= 0)
        return text;
    std::wstring w(static_cast<std::size_t>(wn), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, w.data(), wn);
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u.data(), n, nullptr, nullptr);
    u.resize(static_cast<std::size_t>(n > 0 ? n - 1 : 0));
    return u;
}

std::string dbText(double linear)
{
    return linear > 0.0 ? std::format("{:+.2f}", 20.0 * std::log10(linear)) : std::string("-inf");
}

// --analyze: sample peak and true peak of each file (task 3-3). Nothing is played.
int analyzeFiles(const Options& o)
{
    int worst = 0;
    double albumTruePeak = 0.0;
    std::vector<TrackAnalysis> tracks;
    for (std::size_t i = 0; i < o.files.size(); ++i)
    {
        const fs::path path{o.files[i]};
        print(std::format("{} 曲目: {}\n", i + 1, reinterpret_cast<const char*>(path.filename().u8string().c_str())));
        try
        {
            const auto src = openAudioFile(path);
            if (src->dop())
            {
                print("  DSD の曲です（PCM ではないので True Peak は測りません）\n");
                continue;
            }
            const auto started = std::chrono::steady_clock::now();
            tracks.push_back(analyzeTrack(*src));
            const auto& t = tracks.back();
            const auto& a = t.peaks;
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            const auto rate = src->format().sampleRate;
            print(std::format("  形式: {}、長さ {}（解析 {:.2f} 秒）\n", describe(src->format()), duration(a.frames, rate),
                              seconds));
            for (std::size_t c = 0; c < a.truePeak.size(); ++c)
                print(std::format("  チャンネル {}: サンプルピーク {} dBFS、True Peak {} dBTP\n", c + 1,
                                  dbText(a.samplePeak[c]), dbText(a.truePeak[c])));
            const bool over = a.truePeakMax > 1.0;
            const double at = std::max(a.truePeakPosition, 0.0) / rate;
            print(std::format("  True Peak {} dBTP（サンプルピークより {:.2f} dB 高い、{}:{:06.3f} の位置）{}\n",
                              dbText(a.truePeakMax),
                              a.samplePeakMax > 0.0 ? 20.0 * std::log10(a.truePeakMax / a.samplePeakMax) : 0.0,
                              static_cast<int>(at / 60), std::fmod(at, 60.0),
                              over ?"  ！ 0 dBTP を超えています（DAC の中でクリップする可能性。ISP セーフで下げられます）" : ""));
            albumTruePeak = std::max(albumTruePeak, a.truePeakMax);
            const double gain = replayGainDb(t.integratedLufs);
            const double limited = peakLimitedGainDb(gain, a.truePeakMax);
            print(std::format("  ラウドネス {} LUFS、ReplayGain（トラック）{:+.2f} dB{}\n",
                              std::isfinite(t.integratedLufs) ? std::format("{:.2f}", t.integratedLufs) : std::string("-inf"),
                              gain,
                              limited < gain ? std::format("（True Peak を 0 dBTP 以下に保つため {:+.2f} dB に抑える）", limited)
                                             : std::string()));
        }
        catch (const std::exception& e)
        {
            printErr(std::format("  読めません: {}\n", fromAnsi(e.what())));
            worst = 5;
        }
    }
    if (o.files.size() > 1)
    {
        print(std::format("全体（アルバム）の True Peak: {} dBTP\n", dbText(albumTruePeak)));
        if (!tracks.empty())
        {
            const auto album = analyzeAlbum(tracks);
            const double gain = replayGainDb(album.integratedLufs);
            const double limited = peakLimitedGainDb(gain, album.truePeak);
            print(std::format("全体（アルバム）のラウドネス {:.2f} LUFS、ReplayGain（アルバム）{:+.2f} dB{}\n",
                              album.integratedLufs, gain,
                              limited < gain ? std::format("（{:+.2f} dB に抑える）", limited) : std::string()));
        }
    }
    return worst;
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

    std::vector<EqBand> eqBands;
    if (!o.eqPreset.empty())
    {
        const auto preset = loadEqPreset(fs::path{o.eqPreset});
        for (const auto& w : preset.warnings)
            print("  EQ: " + w + "\n");
        eqBands = preset.bands;
        print(std::format("  EQ: {} バンド（ファイルのプリアンプ {}、こちらでは自動で決める）\n", preset.bands.size(),
                          preset.filePreampDb ? std::format("{:+.1f} dB", *preset.filePreampDb) : std::string("なし")));
    }
    struct Stages
    {
        std::shared_ptr<ReplayGainStage> replayGain = std::make_shared<ReplayGainStage>();
        std::shared_ptr<EqStage> eq = std::make_shared<EqStage>();
        std::shared_ptr<VolumeStage> volume = std::make_shared<VolumeStage>();
        std::shared_ptr<IspSafeStage> ispSafe = std::make_shared<IspSafeStage>();
        std::shared_ptr<SafetyLimiter> limiter = std::make_shared<SafetyLimiter>();
        std::shared_ptr<ProcessingChain> chain;
    };
    auto makeStages = [&] {
        Stages st;
        st.replayGain->setMode(o.replayGain);
        st.eq->setBands(eqBands);
        st.volume->setGainDb(o.volumeDb);
        st.ispSafe->setMode(o.ispSafe.value_or(IspSafeMode::Off));
        st.ispSafe->setUseAlbumPeak(o.replayGain == ReplayGainMode::Album);
        // CLAUDE.md chapter 4: ReplayGain, (EQ etc.), ISP-safe gain, safety limiter, then dither.
        st.chain = std::make_shared<ProcessingChain>(
            std::vector<std::shared_ptr<ProcessingStage>>{st.replayGain, st.eq, st.volume, st.ispSafe, st.limiter});
        return st;
    };
    const Stages stages = makeStages();
    const auto& eq = stages.eq;
    const auto& volume = stages.volume;
    const auto& ispSafe = stages.ispSafe;
    const auto& limiter = stages.limiter;
    const PlaybackOptions playOptions{.allowRepack = !o.exactOnly, .allowDop = o.allowDop, .processing = stages.chain};
    if (o.abxTrials > 0)
    {
        playOptions.processing->setComparing(true);
        playOptions.processing->comparison().select(ComparisonSide::Original);
    }
    else if (o.compare)
    {
        playOptions.processing->setComparing(true);
        playOptions.processing->comparison().select(*o.compare);
    }

    auto printCheck = [&](std::size_t i, const PcmSource& src, AudioOutput& out) {
        const auto n = planPlayback(src, out, playOptions);
        if (!n.plan)
        {
            print(std::format("  {} 曲目: 再生できません: {}\n", i + 1, rejectionText(n.exactCheck, src.format())));
            return 3;
        }
        print(std::format("  {} 曲目: 送る形式 {}、{}\n", i + 1, describe(n.plan->output),
                          n.plan->processing ? "処理 " + processingText(*n.plan, false)
                                             : "詰め替え " + repackText(*n.plan)));
        return 0;
    };

    if (o.replayGain != ReplayGainMode::Off || o.ispSafe)
    {
        print("各曲を解析しています（ラウドネスと True Peak）…\n");
        std::vector<TrackAnalysis> analyses;
        for (const auto& f : o.files)
        {
            const auto src = openAudioFile(fs::path{f});
            if (src->dop())
                analyses.push_back({});
            else
                analyses.push_back(analyzeTrack(*src));
        }
        const auto album = analyzeAlbum(analyses);
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (!analyses[i].meter)
                continue;
            items[i].replayGain = makeReplayGain(analyses[i], &album);
            const auto& r = *items[i].replayGain;
            const bool albumMode = o.replayGain == ReplayGainMode::Album;
            const double gain = albumMode ? *r.albumGainDb : r.trackGainDb;
            const double peak = albumMode ? *r.albumPeak : r.trackPeak;
            print(std::format("  {} 曲目: {:.2f} LUFS、True Peak {} dBTP、{} ゲイン {:+.2f} dB（{}）\n", i + 1,
                              analyses[i].integratedLufs, dbText(analyses[i].peaks.truePeakMax),
                              albumMode ? "アルバム" : "トラック", gain,
                              o.replayGain == ReplayGainMode::Off
                                  ? std::string("ReplayGain はかけない")
                                  : std::format("適用 {:+.2f} dB", peakLimitedGainDb(gain, peak))));
        }
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
                worst = std::max(worst, printCheck(i, *openAudioFile(fs::path{o.files[i]}), out));
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
                worst = std::max(worst, printCheck(i, *openAudioFile(fs::path{o.files[i]}), probe));
            return worst;
        }
        factory = [id] { return std::make_unique<WasapiExclusiveOutput>(id); };
    }

    std::optional<AbxTest> abx;
    std::uint64_t abxFrom = 0, abxLength = 0;
    if (o.abxTrials > 0)
    {
        DWORD consoleMode = 0;
        if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &consoleMode))
        {
            printErr("ABX はキーボードで答えるので、コンソール（ターミナル）で直接実行してください。\n");
            return 2;
        }
        print("ABX: 音作り側の音量を原音に合わせるため、くり返す区間を解析しています…\n");
        const auto src = openAudioFile(fs::path{o.files[0]});
        // The ABX repeats one region (from --from, for --for seconds or 30 s); the match is worked out over it.
        const auto rate = src->format().sampleRate;
        abxFrom = static_cast<std::uint64_t>(o.fromSeconds * rate);
        abxLength = static_cast<std::uint64_t>((o.forSeconds > 0.0 ? o.forSeconds : 30.0) * rate);
        abxLength = std::min<std::uint64_t>(abxLength, src->totalFrames() > abxFrom ? src->totalFrames() - abxFrom : 0);
        src->seek(abxFrom);
        auto offline = makeStages();
        TrackInfo track{0, items[0].replayGain};
        const double match = measureMatchDb(*src, *offline.chain, track, abxLength);
        playOptions.processing->comparison().setFixedMatchDb(match);
        print(std::format("ABX: {:.1f} 秒から {:.1f} 秒間をくり返し流します。音作り側をこの区間で {:+.2f} dB にして原音の音量に合わせます\n",
                          o.fromSeconds, static_cast<double>(abxLength) / rate, match));
        if (playOptions.processing->bypassed() && eqBands.empty() && o.volumeDb == 0.0 && o.replayGain == ReplayGainMode::Off
            && o.ispSafe.value_or(IspSafeMode::Off) == IspSafeMode::Off)
            print("ABX: 音作りの設定がないので、A と B は同じ音です（確かめ用の対照）\n");
        abx.emplace(o.abxTrials);
        print(std::format("ABX: {} 回。a = A（原音）、b = B（音作り）、x = X を聴く。1 = 「X は A」、2 = 「X は B」と答える。q で中断。\n"
                          "     正解数と p 値は最後に出します。いまは A を聴いています。\n",
                          abx->trials()));
    }

    Player player(factory, playOptions);
    if (o.volumeDb < 0.0)
        print(std::format("  音量: {:.1f} dB（デジタル音量。処理経路を通ります）\n", o.volumeDb));
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

    if (o.fromSeconds > 0.0 && player.status().sourceFormat)
    {
        player.seek(static_cast<std::uint64_t>(o.fromSeconds * player.status().sourceFormat->sampleRate));
        print(std::format("  {:.1f} 秒の位置から再生します\n", o.fromSeconds));
    }
    player.play();
    const auto startedAt = std::chrono::steady_clock::now();
    std::size_t nextVolumeChange = 0;
    auto volumeChanges = o.volumeAt;
    std::sort(volumeChanges.begin(), volumeChanges.end());
    auto compareChanges = o.compareAt;
    std::sort(compareChanges.begin(), compareChanges.end());
    std::size_t nextCompareChange = 0;
    auto sideText = [](ComparisonSide side) {
        return side == ComparisonSide::Original ? "原音（値は同じ、この経路ではビットパーフェクトではない）" : "音作り";
    };

    std::size_t shownTransitions = 0;
    std::optional<PlaybackPlan> shownPlan;
    std::optional<std::size_t> warnedTrack;
    PlayerStatus s;
    for (;;)
    {
        s = player.status();
        for (; shownTransitions < s.transitions.size(); ++shownTransitions)
            print("\r  " + transitionText(s.transitions[shownTransitions]) + "                    \n");
        if (s.plan && (!shownPlan || !(shownPlan->output == s.plan->output) || shownPlan->processing != s.plan->processing))
        {
            print(s.plan->processing
                      ? std::format("\r  送る形式: {}（処理経路で作った値）                    \n", describe(s.plan->output))
                      : std::format("\r  送る形式: {}、詰め替え: {}                    \n", describe(s.plan->output),
                                    repackText(*s.plan)));
            if (s.path)
                print(std::format("  出力の方式: {}\n  処理: {}\n", outputModeText(*s.path),
                                  processingText(*s.plan, s.path->dop.has_value())));
            print("  " + bitPerfectLine(s) + "\n");
            if (s.plan->processing && !eq->isIdentity())
                print(std::format("  EQ: プリアンプ {:+.2f} dB（自動）{}\n", eq->preampDb(),
                                  eq->skippedBands() ? std::format("、このレートでは使えないバンド {} 個を飛ばした",
                                                                   eq->skippedBands())
                                                     : std::string()));
            if (s.plan->processing && ispSafe->mode() != IspSafeMode::Off)
                print(std::format("  ISP セーフ: {:+.2f} dB（{}）\n", ispSafe->gainDb(),
                                  ispSafe->mode() == IspSafeMode::Fixed ? "固定"
                                  : s.trackTruePeak ? "解析した True Peak から"
                                                    : "解析していない曲なので固定"));
            shownPlan = s.plan;
        }
        if (s.plan && !s.plan->processing && s.trackTruePeak && *s.trackTruePeak > 1.0 && warnedTrack != s.queueIndex
            && !(s.path && s.path->dop))
        {
            warnedTrack = s.queueIndex;
            print(std::format("\r  ！ ISP の警告: {} 曲目の True Peak は {} dBTP（0 dBTP 超え）。原音経路のままなので DAC の中で"
                              "クリップする可能性があります。--isp-safe measured で下げられます                    \n",
                              s.queueIndex + 1, dbText(*s.trackTruePeak)));
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
        if (o.forSeconds > 0.0 && elapsed >= o.forSeconds && !abx)
        {
            player.stop();
            break;
        }
        for (; o.compare && nextCompareChange < compareChanges.size() && compareChanges[nextCompareChange] <= elapsed;
             ++nextCompareChange)
        {
            auto& ab = playOptions.processing->comparison();
            const auto next = ab.selected() == ComparisonSide::Original ? ComparisonSide::Processed : ComparisonSide::Original;
            ab.select(next);
            print(std::format("\r  {:.1f} 秒: 聴き比べ → {}（音作り側の音量合わせ {:+.2f} dB）                    \n", elapsed,
                              sideText(next), ab.matchDb()));
        }
        for (; nextVolumeChange < volumeChanges.size() && volumeChanges[nextVolumeChange].first <= elapsed; ++nextVolumeChange)
        {
            volume->setGainDb(volumeChanges[nextVolumeChange].second);
            print(std::format("\r  {:.1f} 秒: 音量を {:.1f} dB にした                              \n", elapsed,
                              volumeChanges[nextVolumeChange].second));
        }
        if (abx)
        {
            auto& ab = playOptions.processing->comparison();
            if (s.path && s.path->comparison && ab.levelDifferenceReady())
                abx->noteLevelDifference(ab.levelDifferenceDb());
            while (_kbhit())
            {
                const int key = std::tolower(_getch());
                const auto choose = [&](AbxChoice c, const char* name) {
                    ab.select(abx->sideFor(c));
                    print(std::format("\r  {} 回目: {} を聴いています                                        \n", abx->trial(), name));
                };
                if (key == 'a')
                    choose(AbxChoice::A, "A（原音）");
                else if (key == 'b')
                    choose(AbxChoice::B, "B（音作り）");
                else if (key == 'x')
                    choose(AbxChoice::X, "X");
                else if (key == '1' || key == '2')
                {
                    const int t = abx->trial();
                    abx->answer(key == '1' ? AbxChoice::A : AbxChoice::B);
                    print(std::format("\r  {} 回目: 「X は {}」と答えました                                  \n", t, key == '1' ? "A" : "B"));
                    if (!abx->finished())
                        print(std::format("  {} 回目: X が入れ替わりました（聴いている音はそのまま）\n", abx->trial()));
                }
                else if (key == 'q')
                {
                    abx->abort();
                    print("\r  中断しました                                                    \n");
                }
            }
            if (abx->finished() || abx->result().aborted)
            {
                player.stop();
                s = player.status();
                break;
            }
            if (s.state == PlayerState::Ended)
            {
                // The track ended before the region did: start the region again.
                player.seek(abxFrom);
                player.play();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (s.positionFrames >= abxFrom + abxLength)
                player.seek(abxFrom); // the end of the region: back to its start (without reopening)
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
    if (abx)
    {
        const auto r = abx->result();
        if (r.finished)
        {
            print(std::format("ABX の結果: {} 回中 {} 回正解、p = {:.4f}（当てずっぽうでこれ以上当たる確率）\n", r.trials, *r.correct,
                              *r.pValue));
            print(r.distinguished
                      ? "  p < 0.05：偶然とは考えにくい結果です。A と B の違いを聞き分けられた可能性が高いと言えます。\n"
                      : "  p >= 0.05：偶然でも十分に起こりうる結果です。聞き分けられたとは言えません（違いがないという意味ではありません）。\n");
        }
        else
            print(std::format("ABX: {} 回中 {} 回で中断したので、結果は出しません（途中の結果で判断すると p の意味が崩れるため）。\n",
                              r.trials, r.answered));
        if (r.levelDifferenceDb)
            print(std::format("  音量差: {:+.2f} dB（テスト中に流した音全体、基準 0.1 dB）{}\n", *r.levelDifferenceDb,
                              r.levelMatched ? "" : "：基準を超えたので、結果は参考です（音量の違いで聞き分けた可能性がある）"));
        else
            print("  音量差: 測れなかった（流した時間が短すぎる）\n");
    }
    if (o.compare)
    {
        const auto& ab = playOptions.processing->comparison();
        const auto st = ab.limiterStats();
        const double diff = ab.levelDifferenceDb();
        print(std::format("聴き比べ: 最後は{}、音作り側の音量合わせ {:+.2f} dB、出てくる音の音量差 {:+.2f} dB{}、聴き比べ用のリミッター {}\n",
                          sideText(ab.selected()), ab.matchDb(), diff,
                          std::abs(diff) > 0.1 ? "（0.1 dB を超えている。原音のピークが高く、音作り側を同じ音量にすると上限を超えるため）"
                                               : "",
                          st.events ? std::format("{} 回働いた（最大 {:.2f} dB）", st.events, st.maxReductionDb)
                                    : std::string("働かなかった")));
    }
    if (const auto st = limiter->stats(); st.events > 0)
        print(std::format("保険のリミッター: {} 回働いた（合わせて {:.3f} 秒、最大 {:.2f} dB 下げた）\n", st.events,
                          static_cast<double>(st.limitedFrames) / (s.sourceFormat ? s.sourceFormat->sampleRate : 1u),
                          st.maxReductionDb));
    else if (shownPlan && shownPlan->processing)
        print("保険のリミッター: 働かなかった\n");
    print(std::format("再生スレッド: {}\n", d.realtimePriority ? "MMCSS（Pro Audio）で優先度を上げた"
                                                              : "通常の優先度（MMCSS は使っていない）"));
    print(std::format("ファイルの検証（{} 曲目）: {}\n", s.queueIndex + 1, integrityText(s.integrity)));
    print(std::format("自己確認（最後に出力を開いてから）: {}\n", selfCheckLine(s)));
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
        const bool processed = (s.plan && s.plan->processing)
                            || std::any_of(s.transitions.begin(), s.transitions.end(),
                                           [](const auto& t) { return t.kind == TransitionKind::ProcessingChanged; });
        print(in == out                      ? "一致（ビットパーフェクト、曲の境目に欠けも重なりもなし）\n"
              : processed                    ? "不一致（処理経路を通したため。音量やディザで値が変わるので、一致しないのが正しい）\n"
              : repacked                     ? "不一致（詰め替えたため。詰め替え前の値は保たれています）\n"
              : s.dropouts.readStarvations > 0 ? "不一致（読み込みが間に合わず、無音で埋めたため）\n"
                                             : "不一致\n");
        if (in != out && !repacked && !processed)
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
        if (o->analyze)
            return analyzeFiles(*o);
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
        printErr(std::string("このファイルは読めません: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const UnsupportedAudioFileError& e)
    {
        printErr(std::string("このファイルは読めません（WAV、FLAC、AIFF、ALAC・AAC（.m4a）、MP3、DSF、DFF に対応）: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const FlacFormatError& e)
    {
        printErr(std::string("このファイルは読めません（FLAC）: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const AiffFormatError& e)
    {
        printErr(std::string("このファイルは読めません（AIFF）: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const AlacFormatError& e)
    {
        printErr(std::string("このファイルは読めません（ALAC）: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const Mp4FormatError& e)
    {
        printErr(std::string("このファイルは読めません（MP4）: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const AacFormatError& e)
    {
        printErr(std::string("このファイルは読めません（AAC）: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const Mp3FormatError& e)
    {
        printErr(std::string("このファイルは読めません（MP3）: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const DsdFormatError& e)
    {
        printErr(std::string("このファイルは読めません（DSD）: ") + fromAnsi(e.what()) + "\n");
        return 5;
    }
    catch (const std::exception& e)
    {
        printErr(std::string("エラー: ") + e.what() + "\n\n" + kUsage);
        return 1;
    }
}

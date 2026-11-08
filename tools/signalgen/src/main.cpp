// ap_signalgen: writes test signals as WAV files.
//
//   ap_signalgen isp        --out FILE [common] [--level dB]
//   ap_signalgen sine       --out FILE [common] [--freq Hz] [--level dB]
//   ap_signalgen bitpattern --out FILE [common]
//   ap_signalgen sweep      --out FILE [common] [--start Hz] [--end Hz] [--level dB]
//   ap_signalgen all        --out-dir DIR [--rates a,b,..] [--bits a,b,..] [--channels N] [--seconds S]
//
// common: --rate Hz (48000) --bits 16|24|32|f32|f64 (24) --channels N (2) --seconds S

#include <ap/signalgen/pcm.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/dsd.h>
#include <ap/signalgen/wav_writer.h>

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace ap::signalgen;

namespace {

constexpr const char* kUsage =
    "使い方:\n"
    "  ap_signalgen isp        --out FILE [共通] [--level dB(0)]\n"
    "      fs/4・位相45度のサイン波。サンプルのピークが --level、サンプル間のピークは +3.01 dB 高い\n"
    "  ap_signalgen sine       --out FILE [共通] [--freq Hz(1000)] [--level dB(-90)] [--phase 度(0)]\n"
    "  ap_signalgen bitpattern --out FILE [共通]   （整数形式のみ。右チャンネルは左のビット反転）\n"
    "  ap_signalgen sweep      --out FILE [共通] [--start Hz(20)] [--end Hz(20000)] [--level dB(-6)]\n"
    "      対数スイープ。--seconds の既定は 10\n"
    "  ap_signalgen dsd        --out FILE.dsf|FILE.dff [--dsd-rate Hz(2822400)] [--channels N(2)]\n"
    "                          [--seconds S(5)] [--freq Hz(1000)] [--level dB(-20)]\n"
    "      DSD のサイン波（2 次のΔΣ変調。0 dB = 変調度 50%）\n"
    "  ap_signalgen all        --out-dir DIR [--rates 44100,48000,...] [--bits 16,24,32]\n"
    "                          [--channels N] [--seconds S]\n"
    "      上の4種類を、指定したレートと形式の組み合わせすべてで書き出す\n"
    "\n"
    "共通: --rate Hz(48000) --bits 16|24|32|f32|f64(24) --channels N(2) --seconds S(1)\n"
    "dBFS はピーク基準（ピークがフルスケールのサイン波 = 0 dBFS）。整数への変換は四捨五入でディザなし。\n"
    "\n"
    "Usage: see the comment at the top of tools/signalgen/src/main.cpp.\n";

class UsageError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct Args
{
    std::string command;
    std::map<std::string, std::string, std::less<>> options;

    std::optional<std::string_view> get(std::string_view key) const
    {
        const auto it = options.find(key);
        if (it == options.end())
            return std::nullopt;
        return it->second;
    }
};

Args parseArgs(int argc, char** argv)
{
    if (argc < 2)
        throw UsageError("コマンドを指定してください");
    Args args;
    args.command = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        const std::string_view key = argv[i];
        if (!key.starts_with("--"))
            throw UsageError(std::format("不明な引数: {}", key));
        if (i + 1 >= argc)
            throw UsageError(std::format("{} に値がありません", key));
        args.options.emplace(std::string(key.substr(2)), argv[++i]);
    }
    return args;
}

double parseDouble(std::string_view key, std::string_view text)
{
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size() || !std::isfinite(value))
        throw UsageError(std::format("--{} の値が数値ではありません: {}", key, text));
    return value;
}

std::uint32_t parseUInt(std::string_view key, std::string_view text)
{
    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size() || value == 0)
        throw UsageError(std::format("--{} の値が正の整数ではありません: {}", key, text));
    return value;
}

SampleFormat parseFormatOrThrow(std::string_view text)
{
    const auto f = parseFormat(text);
    if (!f)
        throw UsageError(std::format("--bits の値が不正です: {}（16, 24, 32, f32, f64）", text));
    return *f;
}

std::vector<std::string_view> splitComma(std::string_view text)
{
    std::vector<std::string_view> out;
    while (!text.empty())
    {
        const auto comma = text.find(',');
        out.push_back(text.substr(0, comma));
        if (comma == std::string_view::npos)
            break;
        text.remove_prefix(comma + 1);
    }
    return out;
}

double option(const Args& a, std::string_view key, double fallback)
{
    const auto v = a.get(key);
    return v ? parseDouble(key, *v) : fallback;
}

std::string num(double v)
{
    return std::format("{}", v);
}

std::string fileSuffix(std::uint32_t rate, SampleFormat format, std::uint16_t channels)
{
    return std::format("{}Hz_{}_{}ch.wav", rate, formatName(format), channels);
}

struct Job
{
    std::string signal;
    std::uint32_t rate;
    SampleFormat format;
    std::uint16_t channels;
    double seconds;
    double level;
    double freq;
    double phaseDegrees;
    double start;
    double end;
};

std::string defaultFileName(const Job& j)
{
    const auto suffix = fileSuffix(j.rate, j.format, j.channels);
    if (j.signal == "isp")
        return std::format("isp_fs4_45deg_{}dBFS_{}", num(j.level), suffix);
    if (j.signal == "sine")
        return std::format("sine_{}Hz_{}dBFS_{}", num(j.freq), num(j.level), suffix);
    if (j.signal == "bitpattern")
        return std::format("bitpattern_{}", suffix);
    return std::format("sweep_{}-{}Hz_{}dBFS_{}", num(j.start), num(j.end), num(j.level), suffix);
}

PcmData render(const Job& j)
{
    const auto frames = static_cast<std::size_t>(std::llround(j.seconds * j.rate));
    if (frames == 0)
        throw UsageError("--seconds が短すぎます");
    const Layout layout{j.rate, j.channels, frames};

    if (j.signal == "bitpattern")
    {
        if (!isInteger(j.format))
            throw UsageError("bitpattern は整数形式（16, 24, 32）だけです");
        return bitPattern(layout, j.format);
    }

    std::vector<double> x;
    if (j.signal == "isp")
        x = ispSine(layout, j.level);
    else if (j.signal == "sine")
        x = sine(layout, j.freq, j.level, j.phaseDegrees * 3.14159265358979323846 / 180.0);
    else
        x = logSweep(layout, j.start, j.end, j.level);
    return quantize(x, layout.sampleRate, layout.channels, j.format);
}

Job jobFromArgs(const Args& a, const std::string& signal)
{
    Job j{};
    j.signal = signal;
    j.rate = a.get("rate") ? parseUInt("rate", *a.get("rate")) : 48000;
    j.format = a.get("bits") ? parseFormatOrThrow(*a.get("bits")) : SampleFormat::Int24;
    const auto ch = a.get("channels") ? parseUInt("channels", *a.get("channels")) : 2u;
    if (ch > 64)
        throw UsageError("--channels が大きすぎます");
    j.channels = static_cast<std::uint16_t>(ch);
    j.seconds = option(a, "seconds", signal == "sweep" ? 10.0 : 1.0);
    j.freq = option(a, "freq", 1000.0);
    j.phaseDegrees = option(a, "phase", 0.0);
    j.start = option(a, "start", 20.0);
    j.end = option(a, "end", 20000.0);
    const double defaultLevel = signal == "isp" ? 0.0 : signal == "sine" ? -90.0 : -6.0;
    j.level = option(a, "level", defaultLevel);
    return j;
}

void writeJob(const Job& j, const fs::path& path)
{
    writeWav(path, render(j));
    const auto text = path.u8string(); // console code page is UTF-8
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
}

int runSingle(const Args& a)
{
    const auto out = a.get("out");
    if (!out)
        throw UsageError("--out FILE を指定してください");
    for (const auto& [key, value] : a.options)
    {
        static const std::vector<std::string_view> known{
            "out", "rate", "bits", "channels", "seconds", "level", "freq", "phase", "start", "end"};
        if (std::find(known.begin(), known.end(), key) == known.end())
            throw UsageError(std::format("不明なオプション: --{}", key));
    }
    const fs::path path{std::string(*out)}; // argv is in the ANSI code page, as is path(std::string)
    if (path.has_parent_path())
        fs::create_directories(path.parent_path());
    writeJob(jobFromArgs(a, a.command), path);
    return 0;
}

// ap_signalgen dsd --out FILE.dsf|FILE.dff [--dsd-rate 2822400] [--channels 2] [--seconds 5] [--freq 1000] [--level -20]
int runDsd(const Args& a)
{
    const auto out = a.get("out");
    if (!out)
        throw UsageError("--out FILE.dsf または FILE.dff を指定してください");
    for (const auto& [key, value] : a.options)
    {
        static const std::vector<std::string_view> known{"out", "dsd-rate", "channels", "seconds", "freq", "level"};
        if (std::find(known.begin(), known.end(), key) == known.end())
            throw UsageError(std::format("不明なオプション: --{}", key));
    }
    const fs::path path{std::string(*out)};
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".dsf" && ext != ".dff")
        throw UsageError("DSD の出力ファイルは .dsf か .dff にしてください");

    const auto rate = a.get("dsd-rate") ? parseUInt("dsd-rate", *a.get("dsd-rate")) : 2822400u;
    const auto ch = a.get("channels") ? parseUInt("channels", *a.get("channels")) : 2u;
    const double seconds = option(a, "seconds", 5.0);
    const auto bytes = static_cast<std::size_t>(std::llround(seconds * rate / 8.0));
    const auto channels = dsdSine(rate, static_cast<std::uint16_t>(ch), bytes, option(a, "freq", 1000.0), option(a, "level", -20.0));
    if (path.has_parent_path())
        fs::create_directories(path.parent_path());
    writeBytes(path, ext == ".dsf" ? encodeDsf(channels, rate) : encodeDff(channels, rate));
    const auto text = path.u8string();
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
    return 0;
}

int runAll(const Args& a)
{
    const auto outDir = a.get("out-dir");
    if (!outDir)
        throw UsageError("--out-dir DIR を指定してください");
    for (const auto& [key, value] : a.options)
    {
        static const std::vector<std::string_view> known{"out-dir", "rates", "bits", "channels", "seconds"};
        if (std::find(known.begin(), known.end(), key) == known.end())
            throw UsageError(std::format("不明なオプション: --{}", key));
    }

    std::vector<std::uint32_t> rates{44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};
    if (const auto r = a.get("rates"))
    {
        rates.clear();
        for (auto s : splitComma(*r))
            rates.push_back(parseUInt("rates", s));
    }
    std::vector<SampleFormat> formats{SampleFormat::Int16, SampleFormat::Int24, SampleFormat::Int32};
    if (const auto b = a.get("bits"))
    {
        formats.clear();
        for (auto s : splitComma(*b))
            formats.push_back(parseFormatOrThrow(s));
    }

    const fs::path dir{std::string(*outDir)};
    fs::create_directories(dir);

    for (const auto rate : rates)
    {
        for (const auto format : formats)
        {
            for (const std::string signal : {"isp", "sine", "bitpattern", "sweep"})
            {
                if (signal == "bitpattern" && !isInteger(format))
                    continue;
                Args perJob;
                perJob.options = {{"rate", std::to_string(rate)}};
                if (const auto c = a.get("channels"))
                    perJob.options.emplace("channels", std::string(*c));
                if (const auto s = a.get("seconds"))
                    perJob.options.emplace("seconds", std::string(*s));
                Job j = jobFromArgs(perJob, signal);
                j.format = format;
                writeJob(j, dir / defaultFileName(j));
            }
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    try
    {
        const auto args = parseArgs(argc, argv);
        if (args.command == "dsd")
            return runDsd(args);
        if (args.command == "all")
            return runAll(args);
        if (args.command == "isp" || args.command == "sine" || args.command == "bitpattern"
            || args.command == "sweep")
            return runSingle(args);
        if (args.command == "help" || args.command == "--help" || args.command == "-h")
        {
            std::fputs(kUsage, stdout);
            return 0;
        }
        throw UsageError(std::format("不明なコマンド: {}", args.command));
    }
    catch (const UsageError& e)
    {
        std::fprintf(stderr, "エラー: %s\n\n%s", e.what(), kUsage);
        return 2;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "エラー: %s\n", e.what());
        return 1;
    }
}

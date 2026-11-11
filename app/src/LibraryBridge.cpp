#include "LibraryBridge.h"

#include <ap/library/analysis.h>
#include <ap/library/browse.h>
#include <ap/library/covers.h>

#include <cmath>

namespace ap::app {

using namespace ap::library;
namespace fs = std::filesystem;

namespace {

juce::String str(const std::string& utf8)
{
    return juce::String::fromUTF8(utf8.c_str());
}

juce::String str(const fs::path& p)
{
    return juce::String(p.wstring().c_str());
}

std::string utf8(const juce::String& s)
{
    return s.toStdString();
}

juce::var opt(const std::optional<double>& v)
{
    return v && std::isfinite(*v) ? juce::var(*v) : juce::var();
}

template <typename T>
juce::var optInt(const std::optional<T>& v)
{
    return v ? juce::var(static_cast<juce::int64>(*v)) : juce::var();
}

TrackSort trackSort(const juce::String& s)
{
    if (s == "album") return TrackSort::Album;
    if (s == "title") return TrackSort::Title;
    if (s == "length") return TrackSort::Length;
    if (s == "format") return TrackSort::Format;
    if (s == "rate") return TrackSort::Rate;
    if (s == "truePeak") return TrackSort::TruePeak;
    if (s == "plays") return TrackSort::Plays;
    if (s == "added") return TrackSort::Added;
    return TrackSort::Artist;
}

AlbumSort albumSort(const juce::String& s)
{
    if (s == "album") return AlbumSort::Album;
    if (s == "year") return AlbumSort::Year;
    if (s == "added") return AlbumSort::Added;
    return AlbumSort::Artist;
}

std::size_t sizeArg(const juce::var& q, const char* name, std::size_t fallback, std::size_t max)
{
    const auto v = q.getProperty(name, juce::var());
    if (v.isVoid() || v.isUndefined())
        return fallback;
    return static_cast<std::size_t>(std::clamp<juce::int64>(static_cast<juce::int64>(v), 0, static_cast<juce::int64>(max)));
}

juce::var trackVar(const TrackRow& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", static_cast<juce::int64>(r.id));
    o->setProperty("title", str(r.title));
    o->setProperty("artist", str(r.artist));
    o->setProperty("album", str(r.album));
    o->setProperty("albumKey", r.albumKey ? juce::var(str(*r.albumKey)) : juce::var());
    o->setProperty("trackNo", optInt(r.trackNo));
    o->setProperty("discNo", optInt(r.discNo));
    o->setProperty("seconds", opt(r.seconds));
    o->setProperty("container", str(r.container));
    o->setProperty("sampleRate", optInt(r.sampleRate));
    o->setProperty("dsdRate", optInt(r.dsdRate));
    o->setProperty("bits", optInt(r.bits));
    o->setProperty("analysis", str(r.analysis));
    o->setProperty("truePeakDb", opt(r.truePeakDb));
    o->setProperty("loudnessLufs", opt(r.loudnessLufs));
    o->setProperty("plays", static_cast<juce::int64>(r.plays));
    o->setProperty("rating", optInt(r.rating));
    o->setProperty("present", r.present);
    o->setProperty("path", str(r.path));
    return juce::var(o);
}

juce::var albumVar(const AlbumRow& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("key", str(r.key));
    o->setProperty("album", str(r.album));
    o->setProperty("artist", str(r.artist));
    o->setProperty("year", r.year ? juce::var(str(*r.year)) : juce::var());
    o->setProperty("tracks", static_cast<juce::int64>(r.tracks));
    o->setProperty("seconds", opt(r.seconds));
    o->setProperty("coverTrack", static_cast<juce::int64>(r.coverTrack));
    o->setProperty("truePeakDb", opt(r.truePeakDb));
    o->setProperty("container", str(r.container));
    o->setProperty("sampleRate", optInt(r.sampleRate));
    return juce::var(o);
}

juce::var page(const juce::Array<juce::var>& rows, std::size_t total)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("rows", rows);
    o->setProperty("total", static_cast<juce::int64>(total));
    return juce::var(o);
}

} // namespace

LibraryBridge::LibraryBridge(juce::File folder, PlayQueue playQueue)
    : folder_(std::move(folder)), playQueue_(std::move(playQueue))
{
    folder_.createDirectory();
    databaseFile_ = fs::path(folder_.getChildFile("library.db").getFullPathName().toWideCharPointer());
    coverCache_ = fs::path(folder_.getChildFile("cache").getChildFile("covers").getFullPathName().toWideCharPointer());
    try
    {
        db_ = std::make_unique<LibraryDb>(LibraryDb::open(databaseFile_));
        restartMonitor();
    }
    catch (const NewerSchemaError&)
    {
        problem_ = juce::String::fromUTF8("ライブラリのデータベースは、このアプリより新しい版で作られています。新しい版のアプリを使ってください: ")
                 + str(databaseFile_);
    }
    catch (const std::exception& e)
    {
        problem_ = juce::String::fromUTF8("ライブラリのデータベースを開けませんでした: ") + str(databaseFile_) + " ("
                 + juce::String::fromUTF8(e.what()) + ")";
    }
    startTimerHz(1);
}

LibraryBridge::~LibraryBridge()
{
    stopTimer();
    monitor_.reset();
}

void LibraryBridge::restartMonitor()
{
    monitor_.reset(); // stops its watches and the analysis first
    monitor_ = std::make_unique<LibraryMonitor>(databaseFile_);
}

juce::var LibraryBridge::counts()
{
    auto* o = new juce::DynamicObject();
    if (db_)
    {
        const auto c = libraryCounts(*db_);
        o->setProperty("tracks", static_cast<juce::int64>(c.tracks));
        o->setProperty("missing", static_cast<juce::int64>(c.missing));
        o->setProperty("albums", static_cast<juce::int64>(c.albums));
        o->setProperty("toMeasure", static_cast<juce::int64>(c.toMeasure));
        o->setProperty("folders", static_cast<int>(listFolders(*db_).size()));
    }
    o->setProperty("scanning", monitor_ && !monitor_->waitUntilIdle(std::chrono::milliseconds(0)));
    o->setProperty("problem", problem_);
    return juce::var(o);
}

void LibraryBridge::timerCallback()
{
    if (!browser_)
        return;
    const auto c = counts();
    const auto json = juce::JSON::toString(c, true);
    if (json == lastCounts_)
        return;
    lastCounts_ = json;
    browser_->emitEventIfBrowserIsVisible("libraryChanged", c);
}

juce::WebBrowserComponent::Options LibraryBridge::registerFunctions(juce::WebBrowserComponent::Options options)
{
    using Args = const juce::Array<juce::var>&;
    using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;
    // A failure in a query is reported to the page, never thrown into JUCE.
    auto guarded = [this](auto fn) {
        return [this, fn](Args args, Completion done) {
            if (!db_)
            {
                done(juce::var());
                return;
            }
            try
            {
                done(fn(args));
            }
            catch (const std::exception& e)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty("error", juce::String::fromUTF8(e.what()));
                done(juce::var(o));
            }
        };
    };

    return options
        .withNativeFunction("libraryInfo", [this](Args, Completion done) {
            auto* o = new juce::DynamicObject();
            o->setProperty("coverBase", juce::WebBrowserComponent::getResourceProviderRoot() + "art/track/");
            o->setProperty("problem", problem_);
            done(juce::var(o));
        })
        .withNativeFunction("libraryFolders", guarded([this](Args) {
            juce::Array<juce::var> rows;
            for (const auto& f : listFolders(*db_))
            {
                auto* o = new juce::DynamicObject();
                o->setProperty("id", static_cast<juce::int64>(f.id));
                o->setProperty("path", str(f.path));
                o->setProperty("tracks", static_cast<juce::int64>(f.tracks));
                o->setProperty("missing", static_cast<juce::int64>(f.missing));
                rows.add(juce::var(o));
            }
            return juce::var(rows);
        }))
        .withNativeFunction("libraryAddFolder", [this](Args, Completion done) { addFolderAsync(std::move(done)); })
        .withNativeFunction("libraryRemoveFolder", guarded([this](Args args) {
            if (!args.isEmpty())
            {
                removeFolder(*db_, static_cast<juce::int64>(args[0]));
                restartMonitor();
            }
            return juce::var(true);
        }))
        .withNativeFunction("libraryTracks", guarded([this](Args args) {
            const auto q = args.isEmpty() ? juce::var() : args[0];
            TrackQuery query;
            query.search = utf8(q.getProperty("search", "").toString());
            query.sort = trackSort(q.getProperty("sort", "artist").toString());
            query.descending = static_cast<bool>(q.getProperty("descending", false));
            if (const auto key = q.getProperty("albumKey", juce::var()); key.isString())
                query.albumKey = utf8(key.toString());
            query.offset = sizeArg(q, "offset", 0, 100'000'000);
            query.limit = sizeArg(q, "limit", 200, 2000);
            const auto p = listTracks(*db_, query);
            juce::Array<juce::var> rows;
            for (const auto& r : p.rows)
                rows.add(trackVar(r));
            return page(rows, p.total);
        }))
        .withNativeFunction("libraryAlbums", guarded([this](Args args) {
            const auto q = args.isEmpty() ? juce::var() : args[0];
            AlbumQuery query;
            query.search = utf8(q.getProperty("search", "").toString());
            query.sort = albumSort(q.getProperty("sort", "artist").toString());
            query.descending = static_cast<bool>(q.getProperty("descending", false));
            query.offset = sizeArg(q, "offset", 0, 100'000'000);
            query.limit = sizeArg(q, "limit", 200, 2000);
            const auto p = listAlbums(*db_, query);
            juce::Array<juce::var> rows;
            for (const auto& r : p.rows)
                rows.add(albumVar(r));
            return page(rows, p.total);
        }))
        .withNativeFunction("libraryCounts", [this](Args, Completion done) { done(counts()); })
        .withNativeFunction("libraryPlay", guarded([this](Args args) {
            std::vector<QueueEntry> entries;
            if (args.size() >= 1)
                if (const auto* ids = args[0].getArray())
                    for (const auto& idVar : *ids)
                    {
                        const auto id = static_cast<juce::int64>(idVar);
                        auto st = db_->database().prepare(
                            "SELECT f.path, d.title FROM tracks AS t JOIN files AS f ON f.id = t.file_id "
                            "JOIN track_display AS d ON d.track_id = t.id WHERE t.id = ? AND f.present = 1");
                        st.bind(1, static_cast<std::int64_t>(id));
                        if (!st.step())
                            continue;
                        const auto p = st.text(0);
                        entries.push_back({st.text(1), fs::path(std::u8string(p.begin(), p.end())),
                                           replayGainOf(*db_, id)});
                    }
            const auto start = args.size() >= 2 ? static_cast<std::size_t>(std::max(0, static_cast<int>(args[1]))) : 0;
            auto* o = new juce::DynamicObject();
            if (entries.empty())
            {
                o->setProperty("ok", false);
                o->setProperty("message", juce::String::fromUTF8("再生できる曲がありません（ファイルが見つかりません）"));
                return juce::var(o);
            }
            const auto message = playQueue_(std::move(entries), start);
            o->setProperty("ok", message.isEmpty());
            if (message.isNotEmpty())
                o->setProperty("message", message);
            return juce::var(o);
        }));
}

void LibraryBridge::addFolderAsync(juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    chooser_ = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("音楽のフォルダーを選ぶ（中のフォルダーも含めて監視します）"));
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, completion = std::move(completion)](const juce::FileChooser& fc) {
                              auto* result = new juce::DynamicObject();
                              const auto dir = fc.getResult();
                              if (dir == juce::File{} || !db_)
                              {
                                  result->setProperty("ok", false);
                                  result->setProperty("cancelled", true);
                                  completion(juce::var(result));
                                  return;
                              }
                              try
                              {
                                  const fs::path path(dir.getFullPathName().toWideCharPointer());
                                  bool known = false;
                                  for (const auto& f : listFolders(*db_))
                                      known = known || juce::File(str(f.path)) == dir;
                                  if (!known)
                                      db_->addRoot(path);
                                  restartMonitor(); // its startup scan takes the new folder in
                                  result->setProperty("ok", true);
                              }
                              catch (const std::exception& e)
                              {
                                  result->setProperty("ok", false);
                                  result->setProperty("message", juce::String::fromUTF8("フォルダーを加えられませんでした: ")
                                                                     + juce::String::fromUTF8(e.what()));
                              }
                              completion(juce::var(result));
                          });
}

std::optional<juce::WebBrowserComponent::Resource> LibraryBridge::provideResource(const juce::String& url)
{
    auto path = url.upToFirstOccurrenceOf("?", false, false).trimCharactersAtStart("/");
    if (!path.startsWith("art/track/") || !path.endsWith(".jpg") || !db_)
        return std::nullopt;
    const auto idText = path.fromFirstOccurrenceOf("art/track/", false, false).upToLastOccurrenceOf(".jpg", false, false);
    if (idText.isEmpty() || !idText.containsOnly("0123456789"))
        return std::nullopt;
    try
    {
        const auto thumb = coverThumbnail(*db_, idText.getLargeIntValue(), coverCache_);
        if (!thumb)
            return std::nullopt;
        juce::MemoryBlock block;
        if (!juce::File(str(*thumb)).loadFileAsData(block))
            return std::nullopt;
        juce::WebBrowserComponent::Resource r;
        const auto* bytes = static_cast<const std::byte*>(block.getData());
        r.data.assign(bytes, bytes + block.getSize());
        r.mimeType = "image/jpeg";
        return r;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

} // namespace ap::app

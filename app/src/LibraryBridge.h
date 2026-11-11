#pragma once

#include <ap/engine/track_info.h>
#include <ap/library/library_db.h>
#include <ap/library/library_monitor.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ap::app {

// Connects the web UI to the library (task 6-1).
//
// The database is <folder>/library.db, covers are cached in <folder>/cache/covers.
// A LibraryMonitor keeps the database in line with the watched folders (it
// also reads tags and wakes the background analysis); it is started again
// when a folder is added or removed.
//
// Native functions (JavaScript -> C++), see ui/src/backend.ts:
//   libraryInfo()            { coverBase }: covers are coverBase + "<track id>.jpg"
//   libraryFolders()         [{ id, path, tracks, missing }]
//   libraryAddFolder()       shows a folder dialog; { ok, cancelled?, message? }
//   libraryRemoveFolder(id)
//   libraryTracks(query)     { rows, total }; query { search, sort, descending, albumKey, offset, limit }
//   libraryAlbums(query)     { rows, total }; query { search, sort, descending, offset, limit }
//   libraryCounts()          { tracks, missing, albums, toMeasure, scanning }
//   libraryPlay(ids, start)  plays these tracks as the queue, from ids[start]
// Event (C++ -> JavaScript): "libraryChanged" with the counts, when they change
// (looked at once a second).
class LibraryBridge final : private juce::Timer
{
public:
    struct QueueEntry
    {
        std::string name; // shown while playing
        std::filesystem::path path;
        std::optional<ap::engine::ReplayGainInfo> replayGain;
    };
    using PlayQueue = std::function<juce::String(std::vector<QueueEntry>, std::size_t start)>;

    // `folder` holds the database and the cover cache; created when missing.
    LibraryBridge(juce::File folder, PlayQueue playQueue);
    ~LibraryBridge() override;

    juce::WebBrowserComponent::Options registerFunctions(juce::WebBrowserComponent::Options options);
    void attach(juce::WebBrowserComponent* browser) { browser_ = browser; }

    // Serves "/art/track/<id>.jpg" (the cover thumbnail); empty for anything else.
    std::optional<juce::WebBrowserComponent::Resource> provideResource(const juce::String& url);

    // Empty when the library could not be opened; else why (Japanese).
    const juce::String& problem() const noexcept { return problem_; }

private:
    void timerCallback() override;
    void restartMonitor();
    void addFolderAsync(juce::WebBrowserComponent::NativeFunctionCompletion completion);
    juce::var counts();

    juce::File folder_;
    std::filesystem::path databaseFile_;
    std::filesystem::path coverCache_;
    PlayQueue playQueue_;
    std::unique_ptr<ap::library::LibraryDb> db_; // the screen's connection (message thread)
    std::unique_ptr<ap::library::LibraryMonitor> monitor_;
    juce::WebBrowserComponent* browser_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastCounts_;
    juce::String problem_;
};

} // namespace ap::app

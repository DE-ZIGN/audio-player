// The library schema, as migrations (task 5-1). Never edit a migration that
// has been released: add a new one. Times are unix milliseconds.

#include <ap/library/library_db.h>

namespace ap::library {

namespace {

constexpr std::string_view kV1 = R"sql(
-- Watched folders (task 5-2).
CREATE TABLE roots (
    id        INTEGER PRIMARY KEY,
    path      TEXT NOT NULL UNIQUE COLLATE NOCASE,
    added_at  INTEGER NOT NULL
);

-- Where the music files are. The application only reads them (CLAUDE.md chapter 7).
CREATE TABLE files (
    id             INTEGER PRIMARY KEY,
    root_id        INTEGER REFERENCES roots(id) ON DELETE SET NULL,
    path           TEXT NOT NULL UNIQUE COLLATE NOCASE, -- absolute; NTFS does not tell case apart
    -- NTFS identity (volume serial + 128-bit file ID): the same file after a
    -- rename or a move within the volume. NULL where the drive has none (NAS).
    volume_serial  INTEGER,
    ntfs_file_id   BLOB,
    size           INTEGER NOT NULL,
    modified_at    INTEGER NOT NULL,     -- last write time
    tag_hash       BLOB,                 -- hash of the tag region: notices tag edits the time does not show
    present        INTEGER NOT NULL DEFAULT 1 CHECK (present IN (0, 1)), -- 0: not found (drive not connected, moved)
    last_seen_at   INTEGER NOT NULL,
    CHECK ((volume_serial IS NULL) = (ntfs_file_id IS NULL))
);
CREATE UNIQUE INDEX files_ntfs_identity ON files (volume_serial, ntfs_file_id) WHERE ntfs_file_id IS NOT NULL;

-- A piece of audio. Its edits, rating, play history and measurements hang
-- here, not on the file, so they survive renames and moves (task 5-5).
CREATE TABLE tracks (
    id           INTEGER PRIMARY KEY,
    file_id      INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    start_frame  INTEGER,  -- NULL: the whole file (a range: one track of a cue sheet, later)
    end_frame    INTEGER,
    -- Of the audio data (FLAC: the STREAMINFO MD5; otherwise computed): finds
    -- the track again on another drive or a NAS.
    fingerprint  TEXT,
    -- Facts read from the file (not editable).
    container    TEXT,     -- 'FLAC', 'WAV', 'AIFF', 'ALAC', 'MP3', 'AAC', 'DSF', 'DFF'
    sample_rate  INTEGER,  -- PCM rate; for DSD, the DSD rate is dsd_rate
    bits         INTEGER,
    channels     INTEGER,
    frames       INTEGER,
    dsd_rate     INTEGER,
    rating       INTEGER CHECK (rating BETWEEN 0 AND 100), -- the user's; NULL: not rated
    added_at     INTEGER NOT NULL
);
CREATE UNIQUE INDEX tracks_in_file ON tracks (file_id, COALESCE(start_frame, -1));
CREATE INDEX tracks_fingerprint ON tracks (fingerprint) WHERE fingerprint IS NOT NULL;

-- Displayed values in three layers (CLAUDE.md chapter 7): 2 the user's edit >
-- 1 an external database > 0 the file's tag. Per field, the highest layer that
-- has it is shown. Field names are lower case, as in Vorbis comments: title,
-- artist, album, albumartist, composer, conductor, ensemble, work,
-- movementname, movement, genre, date, tracknumber, discnumber, comment, ...
-- Several values of one field (two artists) are rows with idx 0, 1, ...
CREATE TABLE track_fields (
    track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    field       TEXT NOT NULL,
    layer       INTEGER NOT NULL CHECK (layer IN (0, 1, 2)),
    idx         INTEGER NOT NULL CHECK (idx >= 0),
    value       TEXT,  -- NULL only as the user's "cleared" (layer 2, idx 0): the field shows nothing
    source      TEXT,  -- tag: 'id3v2.3', 'vorbis', ...; external: 'musicbrainz', ...
    updated_at  INTEGER NOT NULL,
    PRIMARY KEY (track_id, field, layer, idx),
    CHECK (value IS NOT NULL OR (layer = 2 AND idx = 0))
) WITHOUT ROWID;

-- What is shown: for each field, the values of its highest layer.
CREATE VIEW track_values AS
SELECT f.track_id, f.field, f.idx, f.value, f.layer
FROM track_fields AS f
WHERE f.layer = (SELECT MAX(g.layer) FROM track_fields AS g WHERE g.track_id = f.track_id AND g.field = f.field)
  AND f.value IS NOT NULL;

-- Measured when the track is added (tasks 3-3, 3-4): True Peak by 8x
-- oversampling and BS.1770 loudness. Peak values in tags are not used.
CREATE TABLE track_analysis (
    track_id          INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    analyzer_version  INTEGER NOT NULL,  -- measured again when the analysis changes
    fingerprint       TEXT,              -- the audio that was measured: measured again when it changed
    sample_peak       REAL NOT NULL,     -- linear, the largest channel
    true_peak         REAL NOT NULL,     -- linear
    true_peak_frame   REAL,              -- where (frames from the start, between samples)
    loudness_lufs     REAL,              -- integrated; NULL: nothing above the gate (silence)
    analyzed_at       INTEGER NOT NULL
);

-- An album's values: the loudness of all its tracks gated together and the
-- largest true peak (ReplayGain album mode, ISP safe with album values).
CREATE TABLE album_analysis (
    album_key         TEXT PRIMARY KEY,  -- how the tracks were grouped (album artist and album as shown)
    members_hash      TEXT NOT NULL,     -- which tracks were measured: measured again when they changed
    analyzer_version  INTEGER NOT NULL,
    true_peak         REAL NOT NULL,
    loudness_lufs     REAL,
    analyzed_at       INTEGER NOT NULL
);

-- File provenance checks (phase 2, quality-measures.md 32): CD match,
-- upsampled, made from a lossy file, padded bit depth, pre-emphasis, polarity,
-- time offset between channels. Each is reported with the measurements behind
-- it ('found', 'none' or 'inconclusive'); nothing is ever fixed automatically.
CREATE TABLE provenance_findings (
    track_id         INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    check_name       TEXT NOT NULL,  -- 'cd_match', 'upsampled', 'lossy_origin', 'bit_depth_padding', 'pre_emphasis', 'polarity', 'channel_delay'
    result           TEXT NOT NULL CHECK (result IN ('found', 'none', 'inconclusive')),
    evidence         TEXT CHECK (evidence IS NULL OR json_valid(evidence)),
    checker_version  INTEGER NOT NULL,
    fingerprint      TEXT,
    checked_at       INTEGER NOT NULL,
    PRIMARY KEY (track_id, check_name)
) WITHOUT ROWID;

-- Play history (task 5-5).
CREATE TABLE plays (
    id             INTEGER PRIMARY KEY,
    track_id       INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    started_at     INTEGER NOT NULL,
    played_frames  INTEGER NOT NULL CHECK (played_frames >= 0),
    completed      INTEGER NOT NULL CHECK (completed IN (0, 1))
);
CREATE INDEX plays_track ON plays (track_id);
)sql";

constexpr std::string_view kV2 = R"sql(
-- Files whose tags are to be read (again): added, or changed on disk (task
-- 5-2). Reading them is task 5-3. Files already in the library are read once.
ALTER TABLE files ADD COLUMN needs_read INTEGER NOT NULL DEFAULT 1 CHECK (needs_read IN (0, 1));
CREATE INDEX files_needs_read ON files (id) WHERE needs_read = 1;
)sql";

constexpr std::string_view kV3 = R"sql(
-- Reading the tags (task 5-3). Text in fields that declare no character set
-- (ID3v1, ID3v2 "ISO-8859-1" frames, RIFF INFO) is decoded by a character set
-- judged per file; the judgement is kept, and the user can choose instead.
ALTER TABLE files ADD COLUMN tag_charset TEXT
    CHECK (tag_charset IN ('shift_jis', 'windows-1252', 'utf-8')); -- NULL: no such text beyond ASCII
ALTER TABLE files ADD COLUMN tag_charset_override TEXT
    CHECK (tag_charset_override IN ('shift_jis', 'windows-1252', 'utf-8')); -- the user's choice; NULL: detect
ALTER TABLE files ADD COLUMN read_error TEXT; -- why the tags could not be read (read again when the file changes)
)sql";

constexpr std::string_view kV4 = R"sql(
-- Search (task 5-4): the shown values in the search form (search.h), one row
-- per track (rowid = tracks.id), indexed by 3-character pieces. Filled by the
-- application (the search form is computed there); built again when the form
-- changes (library_meta 'search_index').
CREATE TABLE library_meta (
    key    TEXT PRIMARY KEY,
    value  TEXT
) WITHOUT ROWID;
CREATE VIRTUAL TABLE track_search USING fts5 (title, artists, album, other, file, tokenize = 'trigram');
CREATE TRIGGER tracks_leave_search AFTER DELETE ON tracks BEGIN
    DELETE FROM track_search WHERE rowid = old.id;
END;
)sql";

constexpr std::string_view kV5 = R"sql(
-- Plays and moves (task 5-5). A play counts when half the track or 4 minutes
-- were heard (history.h); every play is kept with whether it counts.
ALTER TABLE plays ADD COLUMN counted INTEGER NOT NULL DEFAULT 0 CHECK (counted IN (0, 1));
UPDATE plays SET counted = completed;
-- Fingerprints are taken when the tags are read: read the tracks that have none yet.
UPDATE files SET needs_read = 1 WHERE id IN (SELECT file_id FROM tracks WHERE fingerprint IS NULL);
)sql";

constexpr std::string_view kV6 = R"sql(
-- Measuring at import (task 5-6). A track's analysis also records a DSD file
-- (not PCM: nothing to measure) or a file that does not decode, so neither is
-- tried again until the file changes; and the album it was measured with.
CREATE TABLE track_analysis_new (
    track_id          INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    analyzer_version  INTEGER NOT NULL,
    fingerprint       TEXT,               -- of the audio that was measured: measured again when it changed
    status            TEXT NOT NULL CHECK (status IN ('ok', 'not_pcm', 'failed')),
    error             TEXT,               -- why it failed
    sample_peak       REAL,               -- linear, the largest channel
    true_peak         REAL,               -- linear, 8x oversampling
    true_peak_frame   REAL,               -- where (frames from the start, between samples)
    loudness_lufs     REAL,               -- integrated; NULL: nothing above the gate (silence)
    album_key         TEXT,               -- the album it was measured with; NULL: alone
    analyzed_at       INTEGER NOT NULL,
    CHECK (status <> 'ok' OR (sample_peak IS NOT NULL AND true_peak IS NOT NULL))
);
INSERT INTO track_analysis_new (track_id, analyzer_version, fingerprint, status, sample_peak, true_peak,
                                true_peak_frame, loudness_lufs, analyzed_at)
    SELECT track_id, analyzer_version, fingerprint, 'ok', sample_peak, true_peak, true_peak_frame, loudness_lufs,
           analyzed_at FROM track_analysis;
DROP TABLE track_analysis;
ALTER TABLE track_analysis_new RENAME TO track_analysis;
)sql";

constexpr Migration kMigrations[] = {
    {1, "files, tracks, three layers of values, analysis, provenance findings, plays", kV1},
    {2, "files.needs_read", kV2},
    {3, "files.tag_charset, tag_charset_override, read_error", kV3},
    {4, "library_meta, track_search (FTS5 trigram)", kV4},
    {5, "plays.counted; read again for fingerprints", kV5},
    {6, "track_analysis with status, error and album_key", kV6},
};

} // namespace

std::span<const Migration> libraryMigrations()
{
    return kMigrations;
}

int librarySchemaVersion()
{
    return kMigrations[std::size(kMigrations) - 1].version;
}

} // namespace ap::library

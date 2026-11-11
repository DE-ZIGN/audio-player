#include <ap/library/tag_importer.h>

#include <ap/library/history.h>

namespace ap::library {

namespace {
std::filesystem::path fromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
} // namespace

ImportReport TagImporter::readPending(std::size_t limit)
{
    struct Item
    {
        std::int64_t file = 0;
        std::int64_t track = 0;
        std::string path;
        std::optional<LegacyCharset> override;
    };
    std::vector<Item> items;
    {
        auto st = db_.database().prepare(
            "SELECT files.id, tracks.id, files.path, files.tag_charset_override FROM files "
            "JOIN tracks ON tracks.file_id = files.id "
            "WHERE files.needs_read = 1 AND files.present = 1 ORDER BY files.id LIMIT ?");
        st.bind(1, static_cast<std::int64_t>(std::min<std::size_t>(limit, INT64_MAX)));
        while (st.step())
            items.push_back({st.integer(0), st.integer(1), st.text(2),
                             st.isNull(3) ? std::nullopt : charsetFromName(st.text(3))});
    }

    ImportReport report;
    auto& db = db_.database();
    for (const auto& it : items)
    {
        // Read outside the transaction: a slow file does not hold the database.
        std::optional<TagReadResult> tags;
        std::string error;
        try
        {
            tags = readTags(fromUtf8(it.path), it.override);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        // Found again by it after a move to another drive (task 5-5), tags or not.
        const auto fingerprint = fingerprintOf(fromUtf8(it.path));

        Database::Transaction tx(db);
        {
            auto fp = db.prepare("UPDATE tracks SET fingerprint = ? WHERE id = ?");
            if (fingerprint)
                fp.bind(1, std::string_view(*fingerprint));
            else
                fp.bindNull(1);
            fp.bind(2, it.track).run();
        }
        if (!tags)
        {
            // Not retried until the file changes again (the scan marks it then).
            db.prepare("UPDATE files SET needs_read = 0, read_error = ? WHERE id = ?").bind(1, error).bind(2, it.file).run();
            tx.commit();
            ++report.failed;
            report.errors.push_back(it.path + ": " + error);
            continue;
        }
        const auto now = nowUnixMs();
        db.prepare("DELETE FROM track_fields WHERE track_id = ? AND layer = 0").bind(1, it.track).run();
        auto ins = db.prepare("INSERT INTO track_fields (track_id, field, layer, idx, value, source, updated_at) "
                              "VALUES (?, ?, 0, ?, ?, ?, ?)");
        for (const auto& [field, values] : tags->fields)
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                ins.reset();
                ins.bind(1, it.track)
                    .bind(2, std::string_view(field))
                    .bind(3, static_cast<std::int64_t>(i))
                    .bind(4, std::string_view(values[i]))
                    .bind(5, std::string_view(tags->source))
                    .bind(6, now)
                    .run();
            }
        {
            // What the headers say about the audio, until the analysis has the decoder's values (task 5-6).
            const auto& a = tags->audio;
            auto facts = db.prepare("UPDATE tracks SET container = ?, sample_rate = ?, bits = ?, channels = ?, frames = ?, "
                                    "dsd_rate = ? WHERE id = ?");
            if (a.container.empty())
                facts.bindNull(1);
            else
                facts.bind(1, std::string_view(a.container));
            auto opt = [&](int i, auto v) {
                if (v)
                    facts.bind(i, static_cast<std::int64_t>(*v));
                else
                    facts.bindNull(i);
            };
            opt(2, a.sampleRate);
            opt(3, a.bits);
            opt(4, a.channels);
            opt(5, a.frames);
            opt(6, a.dsdRate);
            facts.bind(7, it.track).run();
        }
        auto upd = db.prepare("UPDATE files SET needs_read = 0, read_error = NULL, tag_charset = ? WHERE id = ?");
        if (tags->legacyCharset)
            upd.bind(1, charsetName(*tags->legacyCharset));
        else
            upd.bindNull(1);
        upd.bind(2, it.file).run();
        db_.refreshSearch(it.track);
        tx.commit();
        ++report.read;
        if (tags->legacyCharset == LegacyCharset::ShiftJis)
            ++report.shiftJis;
    }
    return report;
}

void setTagCharset(LibraryDb& db, std::int64_t track, std::optional<LegacyCharset> charset)
{
    auto st = db.database().prepare("UPDATE files SET tag_charset_override = ?, needs_read = 1 "
                                    "WHERE id = (SELECT file_id FROM tracks WHERE id = ?)");
    if (charset)
        st.bind(1, charsetName(*charset));
    else
        st.bindNull(1);
    st.bind(2, track).run();
}

} // namespace ap::library

#include <ap/library/migration.h>

#include <string>

namespace ap::library {

NewerSchemaError::NewerSchemaError(int found, int supported)
    : std::runtime_error("the library database has schema version " + std::to_string(found)
                         + ", newer than this application knows (" + std::to_string(supported) + ")"),
      found_(found), supported_(supported)
{
}

MigrationError::MigrationError(int failedVersion, int reachedVersion, const std::string& detail)
    : std::runtime_error("migration to schema version " + std::to_string(failedVersion) + " failed (the database is at "
                         + std::to_string(reachedVersion) + "): " + detail),
      failedVersion_(failedVersion), reachedVersion_(reachedVersion)
{
}

MigrationResult migrate(Database& db, std::span<const Migration> steps, const std::filesystem::path& backupFile)
{
    for (std::size_t i = 0; i < steps.size(); ++i)
        if (steps[i].version != static_cast<int>(i + 1))
            throw std::invalid_argument("migrations must be numbered 1, 2, ... in order");

    const int latest = steps.empty() ? 0 : steps.back().version;
    const int current = db.userVersion();
    if (current > latest)
        throw NewerSchemaError(current, latest); // before anything is written

    MigrationResult r{current, current, std::nullopt};
    if (current == latest)
        return r;
    if (current > 0 && !backupFile.empty())
    {
        db.backupTo(backupFile);
        r.backup = backupFile;
    }
    for (const auto& step : steps)
    {
        if (step.version <= current)
            continue;
        try
        {
            Database::Transaction tx(db);
            db.exec(step.sql);
            db.exec("PRAGMA user_version = " + std::to_string(step.version)); // part of the same transaction
            tx.commit();
        }
        catch (const SqliteError& e)
        {
            throw MigrationError(step.version, r.toVersion, e.what());
        }
        r.toVersion = step.version;
    }
    return r;
}

} // namespace ap::library

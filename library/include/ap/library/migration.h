#pragma once

// Schema versions and how a database is brought up to date (task 5-1).
//
// The version is SQLite's user_version (0 = empty database). Each migration
// takes the database from version - 1 to version in one transaction: it either
// completes, or nothing changes. Before an existing database is changed it is
// copied (a backup the user can go back to). A database made by a newer
// version of the application is not touched at all.

#include <ap/library/sqlite.h>

#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ap::library {

struct Migration
{
    int version = 0;              // the version this step produces (1, 2, ...)
    std::string_view description; // English, for logs
    std::string_view sql;
};

// The database was made by a newer application (its version is unknown here).
class NewerSchemaError : public std::runtime_error
{
public:
    NewerSchemaError(int found, int supported);
    int found() const noexcept { return found_; }
    int supported() const noexcept { return supported_; }

private:
    int found_;
    int supported_;
};

// A step failed; the database is still at `reachedVersion` (the step was rolled back).
class MigrationError : public std::runtime_error
{
public:
    MigrationError(int failedVersion, int reachedVersion, const std::string& detail);
    int failedVersion() const noexcept { return failedVersion_; }
    int reachedVersion() const noexcept { return reachedVersion_; }

private:
    int failedVersion_;
    int reachedVersion_;
};

struct MigrationResult
{
    int fromVersion = 0;
    int toVersion = 0;
    std::optional<std::filesystem::path> backup; // set when an existing database was copied first
};

// Brings `db` to the last version of `steps` (versions 1, 2, ... in order).
// When the database already has a version below the last one and `backupFile`
// is not empty, the database is copied there first.
// Throws NewerSchemaError (nothing touched), MigrationError, std::invalid_argument (bad steps).
MigrationResult migrate(Database& db, std::span<const Migration> steps, const std::filesystem::path& backupFile = {});

} // namespace ap::library

/******************************************************************************
 * sqlite3_metadata_reader.h
 * vapoursynth-analog - SQLite3-based TBC metadata reader (no Qt SQL)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef SQLITE3_METADATA_READER_H
#define SQLITE3_METADATA_READER_H

#include <QString>
#include "lddecodemetadata.h"

// Read TBC metadata from SQLite database using sqlite3 C API
// This avoids Qt SQL to prevent symbol conflicts with PyQt
class Sqlite3MetadataReader {
public:
    // Read metadata from database file and populate LdDecodeMetaData
    // Returns true on success, false on failure
    static bool read(const QString &dbPath, LdDecodeMetaData &metadata);
};

#endif // SQLITE3_METADATA_READER_H

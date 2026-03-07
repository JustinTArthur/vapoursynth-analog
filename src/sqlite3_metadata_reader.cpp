/******************************************************************************
 * sqlite3_metadata_reader.cpp
 * vapoursynth-analog - SQLite3-based TBC metadata reader (no Qt SQL)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "sqlite3_metadata_reader.h"
#include "dropouts.h"

#include <sqlite3.h>
#include <QDebug>

namespace {

// Helper to get text column, returns empty string if NULL
QString getTextColumn(sqlite3_stmt *stmt, int col) {
    const char *text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    return text ? QString::fromUtf8(text) : QString();
}

// Helper to get int column with default
int getIntColumn(sqlite3_stmt *stmt, int col, int defaultVal = 0) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return defaultVal;
    }
    return sqlite3_column_int(stmt, col);
}

// Helper to get double column with default
double getDoubleColumn(sqlite3_stmt *stmt, int col, double defaultVal = 0.0) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return defaultVal;
    }
    return sqlite3_column_double(stmt, col);
}

// Helper to get int64 column with default
qint64 getInt64Column(sqlite3_stmt *stmt, int col, qint64 defaultVal = 0) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return defaultVal;
    }
    return sqlite3_column_int64(stmt, col);
}

bool readVideoParameters(sqlite3 *db, LdDecodeMetaData::VideoParameters &vp) {
    const char *sql = R"(
        SELECT system, video_sample_rate, field_width, field_height,
               active_video_start, active_video_end, colour_burst_start, colour_burst_end,
               white_16b_ire, black_16b_ire, is_subcarrier_locked, is_widescreen,
               number_of_sequential_fields
        FROM capture WHERE capture_id = 1;
    )";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        qCritical() << "Failed to prepare video parameters query:" << sqlite3_errmsg(db);
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        qCritical() << "No capture record found in database";
        sqlite3_finalize(stmt);
        return false;
    }

    // Parse system
    QString systemStr = getTextColumn(stmt, 0);
    if (systemStr == "PAL") {
        vp.system = PAL;
    } else if (systemStr == "PAL_M" || systemStr == "PAL-M") {
        vp.system = PAL_M;
    } else {
        vp.system = NTSC;
    }

    vp.sampleRate = getDoubleColumn(stmt, 1);
    vp.fieldWidth = getIntColumn(stmt, 2);
    vp.fieldHeight = getIntColumn(stmt, 3);
    vp.activeVideoStart = getIntColumn(stmt, 4);
    vp.activeVideoEnd = getIntColumn(stmt, 5);
    vp.colourBurstStart = getIntColumn(stmt, 6);
    vp.colourBurstEnd = getIntColumn(stmt, 7);
    vp.white16bIre = getIntColumn(stmt, 8);
    vp.black16bIre = getIntColumn(stmt, 9);
    vp.isSubcarrierLocked = getIntColumn(stmt, 10) != 0;
    vp.isWidescreen = getIntColumn(stmt, 11) != 0;
    vp.numberOfSequentialFields = getIntColumn(stmt, 12);

    // Set derived values based on video system
    // (These match the VideoSystemDefaults in ld-decode's lddecodemetadata.cpp)
    if (vp.system == PAL) {
        vp.fSC = (283.75 * 15625.0) + 25.0;
        vp.firstActiveFieldLine = 22;
        vp.lastActiveFieldLine = 308;
        vp.firstActiveFrameLine = 44;
        vp.lastActiveFrameLine = 620;
    } else if (vp.system == PAL_M) {
        vp.fSC = 5.0e6 * (63.0 / 88.0) * (909.0 / 910.0);
        vp.firstActiveFieldLine = 20;
        vp.lastActiveFieldLine = 263;
        vp.firstActiveFrameLine = 40;
        vp.lastActiveFrameLine = 525;
    } else {
        vp.fSC = 315.0e6 / 88.0;
        vp.firstActiveFieldLine = 20;
        vp.lastActiveFieldLine = 263;
        vp.firstActiveFrameLine = 40;
        vp.lastActiveFrameLine = 525;
    }

    vp.isValid = true;

    qInfo() << "Video parameters loaded:"
            << "system=" << (vp.system == PAL ? "PAL" : "NTSC")
            << "fieldWidth=" << vp.fieldWidth
            << "fieldHeight=" << vp.fieldHeight
            << "activeVideoStart=" << vp.activeVideoStart
            << "activeVideoEnd=" << vp.activeVideoEnd
            << "firstActiveFrameLine=" << vp.firstActiveFrameLine
            << "lastActiveFrameLine=" << vp.lastActiveFrameLine;

    sqlite3_finalize(stmt);
    return true;
}

bool readFields(sqlite3 *db, LdDecodeMetaData &metadata) {
    const char *sql = R"(
        SELECT field_id, is_first_field, sync_conf, median_burst_ire,
               field_phase_id, audio_samples, disk_loc, file_loc,
               decode_faults, pad
        FROM field_record
        WHERE capture_id = 1
        ORDER BY field_id;
    )";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        qCritical() << "Failed to prepare fields query:" << sqlite3_errmsg(db);
        return false;
    }

    int fieldCount = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        LdDecodeMetaData::Field field;

        // field_id is 0-indexed, seqNo is 1-indexed
        field.seqNo = getIntColumn(stmt, 0) + 1;
        field.isFirstField = getIntColumn(stmt, 1) != 0;
        field.syncConf = getIntColumn(stmt, 2, 100);
        field.medianBurstIRE = getDoubleColumn(stmt, 3);
        field.fieldPhaseID = getIntColumn(stmt, 4);
        field.audioSamples = getIntColumn(stmt, 5, -1);
        field.diskLoc = getDoubleColumn(stmt, 6, -1);
        field.fileLoc = getInt64Column(stmt, 7, -1);
        field.decodeFaults = getIntColumn(stmt, 8, 0);
        field.pad = getIntColumn(stmt, 9) != 0;

        metadata.appendField(field);
        fieldCount++;
    }

    sqlite3_finalize(stmt);

    if (fieldCount == 0) {
        qCritical() << "No field records found in database";
        return false;
    }

    qInfo() << "Read" << fieldCount << "field records from database";
    return true;
}

bool readDropOuts(sqlite3 *db, LdDecodeMetaData &metadata) {
    const char *sql = R"(
        SELECT field_id, field_line, startx, endx
        FROM drop_outs
        WHERE capture_id = 1
        ORDER BY field_id, field_line, startx;
    )";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        // Table may not exist — not an error
        return true;
    }

    int count = 0;
    qint32 currentFieldId = -1;
    DropOuts currentDropOuts;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        qint32 fieldId = getIntColumn(stmt, 0);
        qint32 fieldLine = getIntColumn(stmt, 1);
        qint32 startx = getIntColumn(stmt, 2);
        qint32 endx = getIntColumn(stmt, 3);

        if (fieldId != currentFieldId) {
            // Flush previous field's dropouts
            if (currentFieldId >= 0 && !currentDropOuts.empty()) {
                metadata.updateFieldDropOuts(currentDropOuts, currentFieldId + 1);
            }
            currentFieldId = fieldId;
            currentDropOuts = DropOuts();
        }

        currentDropOuts.append(startx, endx, fieldLine);
        count++;
    }

    // Flush last field
    if (currentFieldId >= 0 && !currentDropOuts.empty()) {
        metadata.updateFieldDropOuts(currentDropOuts, currentFieldId + 1);
    }

    sqlite3_finalize(stmt);

    if (count > 0) {
        qInfo() << "Read" << count << "dropout records from database";
    }
    return true;
}

bool readVbi(sqlite3 *db, LdDecodeMetaData &metadata) {
    const char *sql = R"(
        SELECT field_id, vbi0, vbi1, vbi2
        FROM vbi
        WHERE capture_id = 1
        ORDER BY field_id;
    )";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        // Table may not exist — not an error
        return true;
    }

    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        qint32 fieldId = getIntColumn(stmt, 0);
        LdDecodeMetaData::Vbi vbi;
        vbi.inUse = true;
        vbi.vbiData[0] = getIntColumn(stmt, 1);
        vbi.vbiData[1] = getIntColumn(stmt, 2);
        vbi.vbiData[2] = getIntColumn(stmt, 3);

        // seqNo is 1-based
        metadata.updateFieldVbi(vbi, fieldId + 1);
        count++;
    }

    sqlite3_finalize(stmt);

    if (count > 0) {
        qInfo() << "Read" << count << "VBI records from database";
    }
    return true;
}

} // anonymous namespace

bool Sqlite3MetadataReader::read(const QString &dbPath, LdDecodeMetaData &metadata) {
    // Clear any existing data
    metadata.clear();

    // Open database
    sqlite3 *db = nullptr;
    int rc = sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        qCritical() << "Failed to open database:" << dbPath << "-" << sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    // Read video parameters
    LdDecodeMetaData::VideoParameters vp;
    if (!readVideoParameters(db, vp)) {
        sqlite3_close(db);
        return false;
    }
    metadata.setVideoParameters(vp);

    // Read field records
    if (!readFields(db, metadata)) {
        sqlite3_close(db);
        return false;
    }

    // Read dropout and VBI data (optional tables)
    if (!readDropOuts(db, metadata) || !readVbi(db, metadata)) {
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

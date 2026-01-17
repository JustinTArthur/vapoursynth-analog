/******************************************************************************
 * jsonconverter_wrapper.cpp
 * vapoursynth-analog - JSON to SQLite converter for TBC metadata
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#include "jsonconverter_wrapper.h"

#include <sqlite3.h>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

namespace {

struct VideoParams {
    QString system = "NTSC";
    int numberOfSequentialFields = 0;
    int fieldWidth = 0;
    int fieldHeight = 0;
    double sampleRate = 0;
    int activeVideoStart = 0;
    int activeVideoEnd = 0;
    int colourBurstStart = 0;
    int colourBurstEnd = 0;
    int white16bIre = 0;
    int black16bIre = 0;
    bool isMapped = false;
    bool isSubcarrierLocked = false;
    bool isWidescreen = false;
    QString gitBranch;
    QString gitCommit;
    QString tapeFormat;
};

struct FieldData {
    int seqNo = 0;
    bool isFirstField = false;
    int syncConf = 0;
    double medianBurstIRE = 0;
    int fieldPhaseID = 0;
    int audioSamples = -1;
    double diskLoc = -1;
    qint64 fileLoc = -1;
    int decodeFaults = -1;
    int efmTValues = -1;
    bool pad = false;
};

void parseVideoParams(const QJsonObject &obj, VideoParams &params) {
    if (obj.contains("system")) params.system = obj["system"].toString();
    if (obj.contains("numberOfSequentialFields")) params.numberOfSequentialFields = obj["numberOfSequentialFields"].toInt();
    if (obj.contains("fieldWidth")) params.fieldWidth = obj["fieldWidth"].toInt();
    if (obj.contains("fieldHeight")) params.fieldHeight = obj["fieldHeight"].toInt();
    if (obj.contains("sampleRate")) params.sampleRate = obj["sampleRate"].toDouble();
    if (obj.contains("activeVideoStart")) params.activeVideoStart = obj["activeVideoStart"].toInt();
    if (obj.contains("activeVideoEnd")) params.activeVideoEnd = obj["activeVideoEnd"].toInt();
    if (obj.contains("colourBurstStart")) params.colourBurstStart = obj["colourBurstStart"].toInt();
    if (obj.contains("colourBurstEnd")) params.colourBurstEnd = obj["colourBurstEnd"].toInt();
    if (obj.contains("white16bIre")) params.white16bIre = obj["white16bIre"].toInt();
    if (obj.contains("black16bIre")) params.black16bIre = obj["black16bIre"].toInt();
    if (obj.contains("isMapped")) params.isMapped = obj["isMapped"].toBool();
    if (obj.contains("isSubcarrierLocked")) params.isSubcarrierLocked = obj["isSubcarrierLocked"].toBool();
    if (obj.contains("isWidescreen")) params.isWidescreen = obj["isWidescreen"].toBool();
    if (obj.contains("gitBranch")) params.gitBranch = obj["gitBranch"].toString();
    if (obj.contains("gitCommit")) params.gitCommit = obj["gitCommit"].toString();
    if (obj.contains("tapeFormat")) params.tapeFormat = obj["tapeFormat"].toString();
}

void parseField(const QJsonObject &obj, FieldData &field) {
    if (obj.contains("seqNo")) field.seqNo = obj["seqNo"].toInt();
    if (obj.contains("isFirstField")) field.isFirstField = obj["isFirstField"].toBool();
    if (obj.contains("syncConf")) field.syncConf = obj["syncConf"].toInt();
    if (obj.contains("medianBurstIRE")) field.medianBurstIRE = obj["medianBurstIRE"].toDouble();
    if (obj.contains("fieldPhaseID")) field.fieldPhaseID = obj["fieldPhaseID"].toInt();
    if (obj.contains("audioSamples")) field.audioSamples = obj["audioSamples"].toInt();
    if (obj.contains("diskLoc")) field.diskLoc = obj["diskLoc"].toDouble();
    if (obj.contains("fileLoc")) field.fileLoc = obj["fileLoc"].toVariant().toLongLong();
    if (obj.contains("decodeFaults")) field.decodeFaults = obj["decodeFaults"].toInt();
    if (obj.contains("efmTValues")) field.efmTValues = obj["efmTValues"].toInt();
    if (obj.contains("pad")) field.pad = obj["pad"].toBool();
}

bool execSql(sqlite3 *db, const char *sql, const char *errorContext) {
    char *errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        qCritical() << errorContext << ":" << errMsg;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool createSchema(sqlite3 *db) {
    if (!execSql(db, "PRAGMA user_version = 1;", "Failed to set user_version")) {
        return false;
    }

    const char *createCapture = R"(
        CREATE TABLE capture (
            capture_id INTEGER PRIMARY KEY,
            system TEXT NOT NULL CHECK (system IN ('NTSC','PAL','PAL_M')),
            decoder TEXT NOT NULL CHECK (decoder IN ('ld-decode','vhs-decode')),
            git_branch TEXT,
            git_commit TEXT,
            video_sample_rate REAL,
            active_video_start INTEGER,
            active_video_end INTEGER,
            field_width INTEGER,
            field_height INTEGER,
            number_of_sequential_fields INTEGER,
            colour_burst_start INTEGER,
            colour_burst_end INTEGER,
            is_mapped INTEGER CHECK (is_mapped IN (0,1)),
            is_subcarrier_locked INTEGER CHECK (is_subcarrier_locked IN (0,1)),
            is_widescreen INTEGER CHECK (is_widescreen IN (0,1)),
            white_16b_ire INTEGER,
            black_16b_ire INTEGER,
            blanking_16b_ire INTEGER,
            capture_notes TEXT
        );
    )";

    if (!execSql(db, createCapture, "Failed to create capture table")) {
        return false;
    }

    const char *createFieldRecord = R"(
        CREATE TABLE field_record (
            capture_id INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
            field_id INTEGER NOT NULL,
            audio_samples INTEGER,
            decode_faults INTEGER,
            disk_loc REAL,
            efm_t_values INTEGER,
            field_phase_id INTEGER,
            file_loc INTEGER,
            is_first_field INTEGER CHECK (is_first_field IN (0,1)),
            median_burst_ire REAL,
            pad INTEGER CHECK (pad IN (0,1)),
            sync_conf INTEGER,
            ntsc_is_fm_code_data_valid INTEGER CHECK (ntsc_is_fm_code_data_valid IN (0,1)),
            ntsc_fm_code_data INTEGER,
            ntsc_field_flag INTEGER CHECK (ntsc_field_flag IN (0,1)),
            ntsc_is_video_id_data_valid INTEGER CHECK (ntsc_is_video_id_data_valid IN (0,1)),
            ntsc_video_id_data INTEGER,
            ntsc_white_flag INTEGER CHECK (ntsc_white_flag IN (0,1)),
            PRIMARY KEY (capture_id, field_id)
        );
    )";

    if (!execSql(db, createFieldRecord, "Failed to create field_record table")) {
        return false;
    }

    return true;
}

} // anonymous namespace

bool convertJsonToSqlite(const QString &jsonPath, const QString &sqlitePath) {
    // Read JSON file
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open JSON file:" << jsonPath;
        return false;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCritical() << "JSON parse error:" << parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qCritical() << "JSON root is not an object";
        return false;
    }

    QJsonObject root = doc.object();

    // Parse video parameters
    VideoParams videoParams;
    if (root.contains("videoParameters")) {
        parseVideoParams(root["videoParameters"].toObject(), videoParams);
    }

    // Parse fields
    std::vector<FieldData> fields;
    if (root.contains("fields")) {
        QJsonArray fieldsArray = root["fields"].toArray();
        fields.reserve(fieldsArray.size());
        for (const auto &fieldVal : fieldsArray) {
            FieldData field;
            parseField(fieldVal.toObject(), field);
            fields.push_back(field);
        }
    }

    qInfo() << "Parsed JSON:" << fields.size() << "fields," << "system:" << videoParams.system;

    // Remove existing database if present
    if (QFileInfo::exists(sqlitePath)) {
        QDir().remove(sqlitePath);
    }

    // Open SQLite database
    sqlite3 *db = nullptr;
    int rc = sqlite3_open(sqlitePath.toUtf8().constData(), &db);
    if (rc != SQLITE_OK) {
        qCritical() << "Failed to create SQLite database:" << sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    // Create schema
    if (!createSchema(db)) {
        qCritical() << "Failed to create database schema";
        sqlite3_close(db);
        return false;
    }

    // Begin transaction
    if (!execSql(db, "BEGIN TRANSACTION;", "Failed to begin transaction")) {
        sqlite3_close(db);
        return false;
    }

    // Insert capture record
    const char *insertCapture = R"(
        INSERT INTO capture (
            capture_id, system, decoder, git_branch, git_commit,
            video_sample_rate, active_video_start, active_video_end,
            field_width, field_height, number_of_sequential_fields,
            colour_burst_start, colour_burst_end, is_mapped,
            is_subcarrier_locked, is_widescreen, white_16b_ire,
            black_16b_ire, blanking_16b_ire, capture_notes
        ) VALUES (1, ?, 'ld-decode', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db, insertCapture, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        qCritical() << "Failed to prepare capture insert:" << sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, videoParams.system.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    if (videoParams.gitBranch.isEmpty()) {
        sqlite3_bind_null(stmt, 2);
    } else {
        sqlite3_bind_text(stmt, 2, videoParams.gitBranch.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    }
    if (videoParams.gitCommit.isEmpty()) {
        sqlite3_bind_null(stmt, 3);
    } else {
        sqlite3_bind_text(stmt, 3, videoParams.gitCommit.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_double(stmt, 4, videoParams.sampleRate);
    sqlite3_bind_int(stmt, 5, videoParams.activeVideoStart);
    sqlite3_bind_int(stmt, 6, videoParams.activeVideoEnd);
    sqlite3_bind_int(stmt, 7, videoParams.fieldWidth);
    sqlite3_bind_int(stmt, 8, videoParams.fieldHeight);
    sqlite3_bind_int(stmt, 9, static_cast<int>(fields.size()));
    sqlite3_bind_int(stmt, 10, videoParams.colourBurstStart);
    sqlite3_bind_int(stmt, 11, videoParams.colourBurstEnd);
    sqlite3_bind_int(stmt, 12, videoParams.isMapped ? 1 : 0);
    sqlite3_bind_int(stmt, 13, videoParams.isSubcarrierLocked ? 1 : 0);
    sqlite3_bind_int(stmt, 14, videoParams.isWidescreen ? 1 : 0);
    sqlite3_bind_int(stmt, 15, videoParams.white16bIre);
    sqlite3_bind_int(stmt, 16, videoParams.black16bIre);
    sqlite3_bind_int(stmt, 17, videoParams.black16bIre);  // blanking same as black
    if (videoParams.tapeFormat.isEmpty()) {
        sqlite3_bind_null(stmt, 18);
    } else {
        sqlite3_bind_text(stmt, 18, videoParams.tapeFormat.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        qCritical() << "Failed to insert capture record:" << sqlite3_errmsg(db);
        execSql(db, "ROLLBACK;", "Rollback failed");
        sqlite3_close(db);
        return false;
    }

    // Prepare field insert statement
    const char *insertField = R"(
        INSERT INTO field_record (
            capture_id, field_id, audio_samples, decode_faults, disk_loc,
            efm_t_values, field_phase_id, file_loc, is_first_field,
            median_burst_ire, pad, sync_conf
        ) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    rc = sqlite3_prepare_v2(db, insertField, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        qCritical() << "Failed to prepare field insert:" << sqlite3_errmsg(db);
        execSql(db, "ROLLBACK;", "Rollback failed");
        sqlite3_close(db);
        return false;
    }

    // Insert field records
    for (const auto &field : fields) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        // Use seqNo directly as field_id (JSON from vhs-decode is already 0-indexed)
        sqlite3_bind_int(stmt, 1, field.seqNo);

        if (field.audioSamples > 0) {
            sqlite3_bind_int(stmt, 2, field.audioSamples);
        } else {
            sqlite3_bind_null(stmt, 2);
        }

        if (field.decodeFaults > 0) {
            sqlite3_bind_int(stmt, 3, field.decodeFaults);
        } else {
            sqlite3_bind_null(stmt, 3);
        }

        if (field.diskLoc > 0) {
            sqlite3_bind_double(stmt, 4, field.diskLoc);
        } else {
            sqlite3_bind_null(stmt, 4);
        }

        if (field.efmTValues > 0) {
            sqlite3_bind_int(stmt, 5, field.efmTValues);
        } else {
            sqlite3_bind_null(stmt, 5);
        }

        sqlite3_bind_int(stmt, 6, field.fieldPhaseID);

        if (field.fileLoc > 0) {
            sqlite3_bind_int64(stmt, 7, field.fileLoc);
        } else {
            sqlite3_bind_null(stmt, 7);
        }

        sqlite3_bind_int(stmt, 8, field.isFirstField ? 1 : 0);
        sqlite3_bind_double(stmt, 9, field.medianBurstIRE);
        sqlite3_bind_int(stmt, 10, field.pad ? 1 : 0);
        sqlite3_bind_int(stmt, 11, field.syncConf);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            qCritical() << "Failed to insert field" << field.seqNo << ":" << sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            execSql(db, "ROLLBACK;", "Rollback failed");
            sqlite3_close(db);
            return false;
        }
    }

    sqlite3_finalize(stmt);

    // Commit transaction
    if (!execSql(db, "COMMIT;", "Failed to commit transaction")) {
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);

    qInfo() << "Successfully converted JSON to SQLite:" << sqlitePath;
    return true;
}

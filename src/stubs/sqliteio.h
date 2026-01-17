/******************************************************************************
 * sqliteio.h (stub)
 * vapoursynth-analog - Stub header to replace ld-decode's Qt SQL-based sqliteio.h
 *
 * This stub provides minimal declarations to satisfy includes from ld-decode
 * library files without actually using Qt SQL. We use our own sqlite3-based
 * metadata reader instead.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef SQLITEIO_H
#define SQLITEIO_H

// Define Qt SQL header guards so the real headers are skipped if included directly
#define QSQLDATABASE_H
#define QSQLQUERY_H
#define QSQLERROR_H

#include <QString>
#include <QVariant>
#include <stdexcept>

// Stub QSqlQuery - provides minimal interface to compile ld-decode code
// The read/write methods that use this are never called in our code path
class QSqlQuery {
public:
    QSqlQuery() = default;
    ~QSqlQuery() = default;
    bool next() { return false; }
    QVariant value(int) const { return QVariant(); }
    QVariant value(const QString &) const { return QVariant(); }
    bool isValid() const { return false; }
};

// Also stub QSqlDatabase since dropouts.cpp includes it directly
class QSqlDatabase {
public:
    static QSqlDatabase addDatabase(const QString &, const QString & = QString()) { return QSqlDatabase(); }
    static void removeDatabase(const QString &) {}
    bool open() { return false; }
    void close() {}
    bool isOpen() const { return false; }
    void setDatabaseName(const QString &) {}
};

// Stub QSqlError
class QSqlError {
public:
    QString text() const { return QString(); }
};

namespace SqliteValue
{
    // These functions are never called - just need declarations for compilation
    inline int toIntOrDefault(const QSqlQuery &, const char *, int defaultValue = -1) { return defaultValue; }
    inline qint64 toLongLongOrDefault(const QSqlQuery &, const char *, qint64 defaultValue = -1) { return defaultValue; }
    inline double toDoubleOrDefault(const QSqlQuery &, const char *, double defaultValue = -1.0) { return defaultValue; }
    inline bool toBoolOrDefault(const QSqlQuery &, const char *, bool defaultValue = false) { return defaultValue; }
}

// Stub SqliteReader - provides interface but no real implementation
// We use Sqlite3MetadataReader instead of this class
class SqliteReader
{
public:
    SqliteReader(const QString &) {}
    ~SqliteReader() {}

    void close() {}

    class Error : public std::runtime_error
    {
    public:
        Error(std::string message) : std::runtime_error(message) {}
    };

    [[noreturn]] void throwError(std::string message) {
        throw Error(message);
    }

    // Stub methods - these are never called since we use Sqlite3MetadataReader
    bool readCaptureMetadata(int &, QString &, QString &,
                           QString &, QString &,
                           double &, int &, int &,
                           int &, int &, int &,
                           int &, int &,
                           bool &, bool &, bool &,
                           int &, int &, int &, QString &) { return false; }

    bool readPcmAudioParameters(int, int &, bool &,
                              bool &, double &) { return false; }

    bool readFields(int, QSqlQuery &) { return false; }

    bool readFieldVitsMetrics(int, int, double &, double &) { return false; }
    bool readFieldVbi(int, int, int &, int &, int &) { return false; }
    bool readFieldVitc(int, int, int[8]) { return false; }
    bool readFieldClosedCaption(int, int, int &, int &) { return false; }
    bool readFieldDropouts(int, int, QSqlQuery &) { return false; }

    bool readAllFieldVitsMetrics(int, QSqlQuery &) { return false; }
    bool readAllFieldVbi(int, QSqlQuery &) { return false; }
    bool readAllFieldVitc(int, QSqlQuery &) { return false; }
    bool readAllFieldClosedCaptions(int, QSqlQuery &) { return false; }
    bool readAllFieldDropouts(int, QSqlQuery &) { return false; }
};

// Stub SqliteWriter - provides interface but no real implementation
// We use our jsonconverter_wrapper instead for writing
class SqliteWriter
{
public:
    SqliteWriter(const QString &) {}
    ~SqliteWriter() {}

    void close() {}

    class Error : public std::runtime_error
    {
    public:
        Error(std::string message) : std::runtime_error(message) {}
    };

    [[noreturn]] void throwError(std::string message) {
        throw Error(message);
    }

    bool createSchema() { return false; }

    int writeCaptureMetadata(const QString &, const QString &,
                           const QString &, const QString &,
                           double, int, int,
                           int, int, int,
                           int, int,
                           bool, bool, bool,
                           int, int, int, const QString &) { return -1; }

    bool updateCaptureMetadata(int, const QString &, const QString &,
                             const QString &, const QString &,
                             double, int, int,
                             int, int, int,
                             int, int,
                             bool, bool, bool,
                             int, int, int, const QString &) { return false; }

    bool writePcmAudioParameters(int, int, bool,
                               bool, double) { return false; }

    bool writeField(int, int, int, int,
                   double, int, int, int,
                   bool, double, bool, int,
                   bool, int, bool,
                   bool, int, bool) { return false; }

    bool writeFieldVitsMetrics(int, int, double, double) { return false; }
    bool writeFieldVbi(int, int, int, int, int) { return false; }
    bool writeFieldVitc(int, int, const int[8]) { return false; }
    bool writeFieldClosedCaption(int, int, int, int) { return false; }
    bool writeFieldDropouts(int, int, int, int, int) { return false; }

    bool beginTransaction() { return false; }
    bool commitTransaction() { return false; }
    bool rollbackTransaction() { return false; }
};

#endif // SQLITEIO_H

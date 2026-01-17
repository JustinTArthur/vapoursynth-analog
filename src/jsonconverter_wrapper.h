/******************************************************************************
 * jsonconverter_wrapper.h
 * vapoursynth-analog - Wrapper to isolate JSON converter from TBC library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ******************************************************************************/

#ifndef JSONCONVERTER_WRAPPER_H
#define JSONCONVERTER_WRAPPER_H

#include <QString>

// Convert a JSON metadata file to SQLite format
// Returns true on success, false on failure
bool convertJsonToSqlite(const QString &jsonPath, const QString &sqlitePath);

#endif // JSONCONVERTER_WRAPPER_H

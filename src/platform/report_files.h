/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The saved reports (model/report.h) on disk: where they go, saving one,
// and listing and reading them for Remote view. Each report is two files
// with the same time in their names: "lokictl-20261123-075130.html" and
// "lokictl-20261123-075130-devices.csv" ("-2" to "-9" after the time when
// that second already has one). The folder is the user's own (0700) and
// each file is readable by the user only (0600).
namespace platform {

// The environment variable that names another folder for reports (an
// absolute path). The desktop simulator saves reports only when it is set,
// so it never writes into the Mac's own Documents folder.
constexpr const char* kReportFolderVariable = "LOKICTL_REPORT_DIR";

// Remote view lists at most this many reports, the newest first, and
// hands out files of at most this size.
constexpr std::size_t kReportListMax = 40;
constexpr uint64_t kReportFileMax = 4u * 1024u * 1024u;

// The folder reports go to: LOKICTL_REPORT_DIR when it is set to an
// absolute path; otherwise, on the device, ~/Documents/LokiCTL/reports
// ($HOME, else the account's home folder). Empty when there is none (the
// desktop simulator without the variable).
std::string report_folder();

// Whether `name` is the name of a report file exactly as save_report()
// makes them. Nothing else is ever listed or read, so a name can never
// lead out of the folder.
bool valid_report_name(std::string_view name);

struct SavedReport {
    std::string page;     // "lokictl-20261123-075130.html"
    std::string devices;  // "lokictl-20261123-075130-devices.csv"
};

// Saves `html` and `csv` as a new report in `folder`, named after the
// local time `stamp` ("20261123-075130"). Makes the folder when it is
// missing and sets it (and a parent named "LokiCTL") to 0700; creates new
// files only (never over an existing one, never through a link), mode
// 0600. On a failure nothing half-written is left, and `reason` says why in
// a few words ("no folder", "disk full").
bool save_report(const std::string& folder,
                 const std::string& stamp,
                 const std::string& html,
                 const std::string& csv,
                 SavedReport& saved,
                 std::string& reason);

struct ReportFile {
    std::string name;
    uint64_t bytes{0};
};

// The report files in `folder` (regular files with a valid name), the
// newest first, at most kReportListMax.
std::vector<ReportFile> list_reports(const std::string& folder);

// Reads the report file `name` from `folder`: only a valid name, a regular
// file (not a link), at most kReportFileMax bytes. False when it can't.
bool read_report(const std::string& folder, std::string_view name, std::string& content);

} // namespace platform

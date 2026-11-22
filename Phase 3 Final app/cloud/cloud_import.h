// ============================================================
// cloud_import.h  -  SmartCare Phase 3
// Validates patient records retrieved from Firebase and builds a
// single transactional SQL script to import the approved ones.
//
// Principle: the C++ app does NOT trust the cloud. Every record is
// re-validated here even though Python validated it before upload.
//
// DSA used:
//   unordered_set<int>  -> O(1) average duplicate detection in a batch
//   callback into the app's unordered_map -> O(1) "already in DB?" check
// ============================================================
#ifndef SMARTCARE_CLOUD_IMPORT_H
#define SMARTCARE_CLOUD_IMPORT_H

#include "json_mini.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace smartcare {

// Each hospital owns a disjoint ID range, so a record's ID also proves
// where it came from and can never collide with local IDs (1-99999).
struct HospitalIdRange
{
    const char *hospital;
    int minId;
    int maxId;
};

inline const std::vector<HospitalIdRange> &hospitalRanges()
{
    static const std::vector<HospitalIdRange> ranges = {
        {"Hospital_A", 110001, 119999},
        {"Hospital_B", 120001, 129999},
        {"Hospital_C", 130001, 139999},
        {"Hospital_D", 140001, 149999},
        {"Hospital_E", 150001, 159999},
        // To add a hospital: add it here AND in python/validation.py,
        // then run python/make_firebase_rules.py and publish the rules.
    };
    return ranges;
}

inline const HospitalIdRange *rangeFor(const std::string &hospital)
{
    for (const auto &r : hospitalRanges())
        if (hospital == r.hospital)
            return &r;
    return nullptr;
}

struct CloudPatient
{
    std::string recordKey;      // e.g. "P110005"
    std::string sourceHospital; // "Hospital_A"
    int id = 0;
    std::string name;
    int age = 0;
    char gender = 'U';
};

struct ImportIssue
{
    std::string recordKey;
    int patientId = 0;   // 0 if unknown
    std::string status;  // "REJECTED" or "SKIPPED"
    std::string reason;
};

struct ImportReport
{
    size_t total = 0;
    std::vector<CloudPatient> accepted;
    std::vector<ImportIssue> issues;
    std::string fatalError; // whole response unusable
};

enum class ImportMode
{
    HospitalDataset, // SmartCare/Hospital_X/Patients
    SharedRecords    // SmartCare/Shared_Records/<my hospital>
};

namespace detail {

inline std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Returns "" if valid, otherwise the reason.
inline std::string checkName(const std::string &raw, std::string &clean)
{
    clean = trim(raw);
    if (clean.empty())
        return "name is empty";
    if (clean.size() > 100)
        return "name longer than 100 characters";
    for (char c : clean)
    {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F)
            return "name contains control characters";
        if (c == '\\' || c == ';')
            return "name contains disallowed characters (\\ or ;)";
    }
    return "";
}

inline std::string sqlQuote(const std::string &s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'')
            out += "''";
        else
            out += c;
    }
    return out + "'";
}

} // namespace detail

// root:          parsed JSON returned by Firebase for the path
// mode:          dataset or shared-records validation rules
// expected:      for HospitalDataset, the hospital whose path was read;
//                for SharedRecords, the receiving hospital (must match shared_with)
// existsLocally: returns true if a patient ID is already in SmartCare/PostgreSQL
inline ImportReport validatePatients(const JsonValue &root, ImportMode mode,
                                     const std::string &expected,
                                     const std::function<bool(int)> &existsLocally)
{
    ImportReport rep;

    if (root.isNull())
        return rep; // path exists but is empty -> nothing to import
    if (root.isArray())
    {
        rep.fatalError = "Firebase returned an array. Keys must be non-numeric (e.g. \"P110001\"), "
                         "otherwise Firebase converts the node to an array.";
        return rep;
    }
    if (!root.isObject())
    {
        rep.fatalError = "Unexpected response shape (expected a JSON object of records).";
        return rep;
    }

    std::unordered_set<int> seenInBatch;
    rep.total = root.keys.size();

    for (size_t i = 0; i < root.keys.size(); i++)
    {
        const std::string &key = root.keys[i];
        const JsonValue &rec = root.items[i];
        ImportIssue issue;
        issue.recordKey = key;
        issue.status = "REJECTED";

        auto reject = [&](const std::string &why) {
            issue.reason = why;
            rep.issues.push_back(issue);
        };

        if (!rec.isObject())
        {
            reject("record is not an object");
            continue;
        }

        CloudPatient p;
        p.recordKey = key;

        const JsonValue *id = rec.find("patient_id");
        if (!id || !id->asInt(p.id))
        {
            reject("patient_id missing or not an integer");
            continue;
        }
        issue.patientId = p.id;

        if (key != "P" + std::to_string(p.id))
        {
            reject("record key does not match patient_id");
            continue;
        }

        // --- provenance / consent ---
        if (mode == ImportMode::HospitalDataset)
        {
            p.sourceHospital = expected;
        }
        else
        {
            const JsonValue *src = rec.find("source_hospital");
            const JsonValue *to = rec.find("shared_with");
            const JsonValue *consent = rec.find("consent");
            if (!src || !src->isString() || !rangeFor(src->text))
            {
                reject("source_hospital missing or unknown");
                continue;
            }
            if (!to || !to->isString() || to->text != expected)
            {
                reject("shared_with does not match this hospital");
                continue;
            }
            if (src->text == expected)
            {
                reject("record shared with its own source hospital");
                continue;
            }
            if (!consent || !consent->isBool() || !consent->boolean)
            {
                reject("no recorded consent for sharing");
                continue;
            }
            p.sourceHospital = src->text;
        }

        const HospitalIdRange *range = rangeFor(p.sourceHospital);
        if (!range)
        {
            reject("unknown source hospital " + p.sourceHospital);
            continue;
        }
        if (p.id < range->minId || p.id > range->maxId)
        {
            reject("patient_id outside " + p.sourceHospital + " range " +
                   std::to_string(range->minId) + "-" + std::to_string(range->maxId));
            continue;
        }

        // --- field checks ---
        const JsonValue *name = rec.find("name");
        if (!name || !name->isString())
        {
            reject("name missing");
            continue;
        }
        std::string why = detail::checkName(name->text, p.name);
        if (!why.empty())
        {
            reject(why);
            continue;
        }

        const JsonValue *age = rec.find("age");
        if (!age || !age->asInt(p.age) || p.age < 0 || p.age > 120)
        {
            reject("age missing or outside 0-120");
            continue;
        }

        const JsonValue *g = rec.find("gender");
        if (!g || !g->isString() || g->text.size() != 1 ||
            (g->text[0] != 'M' && g->text[0] != 'F' && g->text[0] != 'O'))
        {
            reject("gender must be M, F or O");
            continue;
        }
        p.gender = g->text[0];

        // --- duplicates ---
        if (!seenInBatch.insert(p.id).second)
        {
            reject("duplicate patient_id within this batch");
            continue;
        }
        if (existsLocally && existsLocally(p.id))
        {
            issue.status = "SKIPPED";
            issue.reason = "patient_id already exists in SmartCare";
            rep.issues.push_back(issue);
            continue;
        }

        rep.accepted.push_back(p);
    }
    return rep;
}

// One script, one transaction: either every approved row and every log
// row is written, or (on any error, psql runs with ON_ERROR_STOP=1)
// nothing is. ON CONFLICT DO NOTHING protects against rows inserted by
// another client (e.g. the web app) since SmartCare loaded its cache.
inline std::string buildImportSql(const ImportReport &rep, const std::string &cloudPath)
{
    using detail::sqlQuote;
    std::string sql = "BEGIN;\n";
    for (const CloudPatient &p : rep.accepted)
    {
        sql += "INSERT INTO patients (patient_id, name, age, gender, source) VALUES (" +
               std::to_string(p.id) + ", " + sqlQuote(p.name) + ", " + std::to_string(p.age) +
               ", " + sqlQuote(std::string(1, p.gender)) + ", " +
               sqlQuote("FIREBASE:" + p.sourceHospital) + ") ON CONFLICT (patient_id) DO NOTHING;\n";
        sql += "INSERT INTO cloud_import_log (cloud_path, record_key, patient_id, status, reason) "
               "VALUES (" + sqlQuote(cloudPath) + ", " + sqlQuote(p.recordKey) + ", " +
               std::to_string(p.id) + ", 'IMPORTED', NULL);\n";
    }
    for (const ImportIssue &is : rep.issues)
    {
        sql += "INSERT INTO cloud_import_log (cloud_path, record_key, patient_id, status, reason) "
               "VALUES (" + sqlQuote(cloudPath) + ", " + sqlQuote(is.recordKey.substr(0, 60)) + ", " +
               (is.patientId ? std::to_string(is.patientId) : std::string("NULL")) + ", " +
               sqlQuote(is.status) + ", " + sqlQuote(is.reason) + ");\n";
    }
    sql += "COMMIT;\n";
    return sql;
}

} // namespace smartcare

#endif

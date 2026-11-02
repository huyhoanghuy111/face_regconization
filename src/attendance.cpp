#include "attendance.hpp"

#include <mysql.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {

// Lich su giu lai ten luc cham cong va khong co khoa ngoai,
// nen xoa nhan vien khong lam mat ban ghi cu.
const char* SCHEMA[] = {
    R"sql(CREATE TABLE IF NOT EXISTS employees (
        code       VARCHAR(32) PRIMARY KEY,
        name       VARCHAR(255) NOT NULL,
        department VARCHAR(255) NOT NULL DEFAULT ''
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4)sql",
    R"sql(CREATE TABLE IF NOT EXISTS attendance (
        id         BIGINT AUTO_INCREMENT PRIMARY KEY,
        checked_at BIGINT NOT NULL,
        code       VARCHAR(32) NOT NULL,
        name       VARCHAR(255) NOT NULL,
        type       ENUM('Vào', 'Ra') NOT NULL,
        INDEX attendance_code_time (code, checked_at)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4)sql",
};

[[noreturn]] void throwSqlError(const std::string& action, const char* detail) {
    throw std::runtime_error("Loi MySQL khi " + action + ": " + detail);
}

void runQuery(MYSQL* db, const char* sql) {
    if (mysql_query(db, sql) != 0) {
        throwSqlError("chay cau lenh", mysql_error(db));
    }
}

Database openDatabase(const DatabaseConfig& config) {
    Database db(mysql_init(nullptr), mysql_close);
    if (!db) {
        throw std::runtime_error("Khong du bo nho de khoi tao MySQL");
    }
    const unsigned int timeoutSeconds = 3;
    mysql_options(db.get(), MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSeconds);
    mysql_options(db.get(), MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (mysql_real_connect(db.get(), config.host.c_str(), config.user.c_str(),
                           config.password.c_str(), config.database.c_str(), config.port, nullptr,
                           0) == nullptr) {
        throw std::runtime_error(std::string("Khong ket noi duoc MySQL: ") + mysql_error(db.get()) +
                                 ". Hay bat may chu: bash scripts/mysql.sh start");
    }
    // May cham cong co the de yen ca dem, khong de may chu tu ngat ket noi
    runQuery(db.get(), "SET SESSION wait_timeout = 31536000");
    for (const char* statement : SCHEMA) {
        runQuery(db.get(), statement);
    }
    return db;
}

// Cau lenh co tham so: gia tri duoc gui rieng nen ten co dau nhay hay dau phay van an toan.
class Statement {
public:
    Statement(MYSQL* db, const char* sql) : statement_(mysql_stmt_init(db)) {
        if (statement_ == nullptr ||
            mysql_stmt_prepare(statement_, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
            throwSqlError("chuan bi cau lenh", mysql_error(db));
        }
    }
    ~Statement() { mysql_stmt_close(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // Moi lan goi gan gia tri cho dau ? tiep theo.
    Statement& bind(const std::string& value) {
        parameters_.push_back({value, 0, static_cast<unsigned long>(value.size()), true});
        return *this;
    }
    Statement& bind(long long value) {
        parameters_.push_back({"", value, 0, false});
        return *this;
    }

    void execute() {
        std::vector<MYSQL_BIND> binds(parameters_.size());
        for (size_t i = 0; i < parameters_.size(); ++i) {
            Parameter& parameter = parameters_[i];
            std::memset(&binds[i], 0, sizeof(MYSQL_BIND));
            if (parameter.isText) {
                binds[i].buffer_type = MYSQL_TYPE_STRING;
                binds[i].buffer = parameter.text.data();
                binds[i].buffer_length = parameter.length;
                binds[i].length = &parameter.length;
            } else {
                binds[i].buffer_type = MYSQL_TYPE_LONGLONG;
                binds[i].buffer = &parameter.number;
            }
        }
        if (mysql_stmt_bind_named_param(statement_, binds.data(),
                                        static_cast<unsigned int>(binds.size()), nullptr) != 0 ||
            mysql_stmt_execute(statement_) != 0) {
            throwSqlError("ghi du lieu", mysql_stmt_error(statement_));
        }
    }

private:
    struct Parameter {
        std::string text;
        long long number;
        unsigned long length;
        bool isText;
    };

    MYSQL_STMT* statement_;
    std::deque<Parameter> parameters_;  // deque giu nguyen dia chi khi them phan tu
};

template <typename RowHandler>
void forEachRow(MYSQL* db, const char* sql, RowHandler handle) {
    runQuery(db, sql);
    std::unique_ptr<MYSQL_RES, void (*)(MYSQL_RES*)> result(mysql_store_result(db), mysql_free_result);
    if (!result) {
        throwSqlError("doc du lieu", mysql_error(db));
    }
    while (MYSQL_ROW row = mysql_fetch_row(result.get())) {
        handle(row);
    }
}

std::string formatWith(std::time_t time, const char* format) {
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), format, std::localtime(&time));
    return buffer;
}

}  // namespace

bool isValidEmployeeCode(const std::string& code) {
    if (code.empty() || code.size() > 32) {
        return false;
    }
    return std::all_of(code.begin(), code.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '_';
    });
}

std::string formatDate(std::time_t time) { return formatWith(time, "%Y-%m-%d"); }
std::string formatTime(std::time_t time) { return formatWith(time, "%H:%M:%S"); }

std::optional<std::time_t> parseDateTime(const std::string& text) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    char extra = '\0';
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d%c", &year, &month, &day, &hour, &minute, &extra) != 5) {
        return std::nullopt;
    }
    std::tm parts{};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_isdst = -1;
    const std::time_t time = std::mktime(&parts);
    // mktime tu doi 31/02 thanh ngay khac, so lai de phat hien ngay khong co that
    if (time == static_cast<std::time_t>(-1) || parts.tm_year != year - 1900 ||
        parts.tm_mon != month - 1 || parts.tm_mday != day || parts.tm_hour != hour ||
        parts.tm_min != minute) {
        return std::nullopt;
    }
    return time;
}

DatabaseConfig loadDatabaseConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Thieu file " + path + ". Hay chay: bash scripts/mysql.sh start");
    }
    DatabaseConfig config;
    std::string line;
    while (std::getline(file, line)) {
        const size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "host") {
            config.host = value;
        } else if (key == "port") {
            config.port = static_cast<unsigned int>(std::stoul(value));
        } else if (key == "user") {
            config.user = value;
        } else if (key == "password") {
            config.password = value;
        } else if (key == "database") {
            config.database = value;
        }
    }
    return config;
}

EmployeeStore::EmployeeStore(const DatabaseConfig& config) : db_(openDatabase(config)) {
    forEachRow(db_.get(), "SELECT code, name, department FROM employees ORDER BY code",
               [&](MYSQL_ROW row) { employees_.push_back({row[0], row[1], row[2]}); });
}

const Employee* EmployeeStore::find(const std::string& code) const {
    for (const Employee& employee : employees_) {
        if (employee.code == code) {
            return &employee;
        }
    }
    return nullptr;
}

std::string EmployeeStore::nextCode() const {
    int highest = 0;
    for (const Employee& employee : employees_) {
        if (employee.code.rfind("NV", 0) == 0) {
            highest = std::max(highest, std::atoi(employee.code.c_str() + 2));
        }
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "NV%03d", highest + 1);
    return buffer;
}

void EmployeeStore::add(Employee employee) {
    Statement insert(db_.get(), "INSERT INTO employees (code, name, department) VALUES (?, ?, ?)");
    insert.bind(employee.code).bind(employee.name).bind(employee.department).execute();
    employees_.push_back(std::move(employee));
}

void EmployeeStore::update(const std::string& code, const std::string& name,
                           const std::string& department) {
    Statement change(db_.get(), "UPDATE employees SET name = ?, department = ? WHERE code = ?");
    change.bind(name).bind(department).bind(code).execute();
    for (Employee& employee : employees_) {
        if (employee.code == code) {
            employee.name = name;
            employee.department = department;
        }
    }
}

void EmployeeStore::remove(const std::string& code) {
    Statement erase(db_.get(), "DELETE FROM employees WHERE code = ?");
    erase.bind(code).execute();
    employees_.erase(std::remove_if(employees_.begin(), employees_.end(),
                                    [&](const Employee& e) { return e.code == code; }),
                     employees_.end());
}

AttendanceBook::AttendanceBook(const DatabaseConfig& config) : db_(openDatabase(config)) {
    forEachRow(db_.get(), "SELECT checked_at, code, name, type FROM attendance ORDER BY checked_at, id",
               [&](MYSQL_ROW row) {
                   records_.push_back(
                       {static_cast<std::time_t>(std::atoll(row[0])), row[1], row[2], row[3]});
               });
}

AttendanceCheck AttendanceBook::check(const std::string& code, const std::string& name,
                                      std::time_t now) {
    AttendanceCheck result = preview(code, name, now);
    if (result.recorded) {
        add(result.record);
    }
    return result;
}

AttendanceCheck AttendanceBook::preview(const std::string& code, const std::string& name,
                                        std::time_t now) const {
    const AttendanceRecord* last = nullptr;
    for (const AttendanceRecord& record : records_) {
        if (record.code == code && formatDate(record.timestamp) == formatDate(now)) {
            last = &record;
        }
    }
    if (last != nullptr && now - last->timestamp < ATTENDANCE_COOLDOWN_SECONDS) {
        return {false, *last};
    }

    return {true, AttendanceRecord{now, code, name, last == nullptr ? "Vào" : "Ra"}};
}

void AttendanceBook::add(const AttendanceRecord& record) {
    Statement insert(db_.get(),
                     "INSERT INTO attendance (checked_at, code, name, type) VALUES (?, ?, ?, ?)");
    insert.bind(static_cast<long long>(record.timestamp))
        .bind(record.code)
        .bind(record.name)
        .bind(record.type)
        .execute();
    auto position = std::upper_bound(records_.begin(), records_.end(), record,
                                     [](const AttendanceRecord& a, const AttendanceRecord& b) {
                                         return a.timestamp < b.timestamp;
                                     });
    records_.insert(position, record);
}

StableTracker::StableTracker(int requiredFrames) : requiredFrames_(requiredFrames) {}

std::vector<std::string> StableTracker::update(const std::set<std::string>& seen) {
    std::vector<std::string> confirmed;
    for (auto it = counts_.begin(); it != counts_.end();) {
        it = seen.count(it->first) == 0 ? counts_.erase(it) : std::next(it);
    }
    for (const std::string& code : seen) {
        if (++counts_[code] == requiredFrames_) {
            confirmed.push_back(code);
        }
    }
    return confirmed;
}

void StableTracker::reset() { counts_.clear(); }

std::vector<float> LiveSmoother::update(const std::vector<LiveObservation>& faces) {
    const float newFaceScore = 0.6f;  // chua du du lieu thi chi hoi nghieng ve nguoi that
    const float weight = 0.35f;

    std::vector<LiveObservation> next = faces;
    std::vector<bool> taken(tracked_.size(), false);
    std::vector<float> smoothed;
    for (LiveObservation& face : next) {
        int nearest = -1;
        float nearestDistance = face.size;  // xa hon mot khuon mat thi coi la nguoi khac
        for (size_t i = 0; i < tracked_.size(); ++i) {
            const float distance = std::hypot(face.x - tracked_[i].x, face.y - tracked_[i].y);
            if (!taken[i] && distance < nearestDistance) {
                nearest = static_cast<int>(i);
                nearestDistance = distance;
            }
        }
        float previous = newFaceScore;
        if (nearest >= 0) {
            taken[nearest] = true;
            previous = tracked_[nearest].score;
        }
        face.score = previous * (1.f - weight) + face.score * weight;
        smoothed.push_back(face.score);
    }
    tracked_ = std::move(next);
    return smoothed;
}

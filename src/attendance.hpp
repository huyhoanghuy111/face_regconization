#pragma once

#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct MYSQL;
using Database = std::unique_ptr<MYSQL, void (*)(MYSQL*)>;

struct DatabaseConfig {
    std::string host;
    unsigned int port = 3306;
    std::string user;
    std::string password;
    std::string database;
};

// Doc file key=value do scripts/mysql.sh tao ra.
DatabaseConfig loadDatabaseConfig(const std::string& path);

// Thoi gian toi thieu giua hai lan cham cong cua cung mot nguoi.
inline const int ATTENDANCE_COOLDOWN_SECONDS = 60;

struct Employee {
    std::string code;  // ma nhan vien, cung la ten thu muc trong known_faces
    std::string name;
    std::string department;
};

// Danh sach nhan vien, luu trong bang employees cua MySQL.
class EmployeeStore {
public:
    explicit EmployeeStore(const DatabaseConfig& config);

    const std::vector<Employee>& all() const { return employees_; }
    const Employee* find(const std::string& code) const;
    std::string nextCode() const;  // ma tiep theo, vi du NV003

    void add(Employee employee);
    void update(const std::string& code, const std::string& name, const std::string& department);
    void remove(const std::string& code);

private:
    Database db_;
    std::vector<Employee> employees_;
};

struct AttendanceRecord {
    std::time_t timestamp = 0;
    std::string code;
    std::string name;
    std::string type;  // "Vào" hoac "Ra"
};

struct AttendanceCheck {
    bool recorded = false;    // false nghia la vua cham cong, khong ghi trung
    AttendanceRecord record;  // ban ghi moi, hoac ban ghi gan nhat neu khong ghi
};

// So cham cong, luu trong bang attendance cua MySQL.
class AttendanceBook {
public:
    explicit AttendanceBook(const DatabaseConfig& config);

    // Lan dau trong ngay la Vao, cac lan sau la Ra.
    AttendanceCheck check(const std::string& code, const std::string& name, std::time_t now);

    // Giong check nhung khong ghi gi, de hoi nguoi dung truoc khi cham cong.
    AttendanceCheck preview(const std::string& code, const std::string& name, std::time_t now) const;

    // Ghi mot ban ghi nhap tay, khong ap dung quy tac Vao/Ra tu dong.
    void add(const AttendanceRecord& record);

    // Sap xep theo thoi gian tang dan.
    const std::vector<AttendanceRecord>& all() const { return records_; }

private:
    Database db_;
    std::vector<AttendanceRecord> records_;
};

// Chi xac nhan mot nguoi khi thay lien tiep du so khung hinh, tranh nhan nham thoang qua.
class StableTracker {
public:
    explicit StableTracker(int requiredFrames);

    // Tra ve nhung nguoi vua dat du so khung hinh trong lan goi nay.
    std::vector<std::string> update(const std::set<std::string>& seen);
    void reset();

private:
    int requiredFrames_;
    std::map<std::string, int> counts_;
};

struct LiveObservation {
    float x = 0.f;  // tam khuon mat
    float y = 0.f;
    float size = 0.f;
    float score = 0.f;
};

// Lam muot diem nguoi that qua nhieu khung hinh de canh bao khong nhap nhay.
// Moi khuon mat duoc noi voi khuon mat gan nhat o khung hinh truoc.
class LiveSmoother {
public:
    std::vector<float> update(const std::vector<LiveObservation>& faces);

private:
    std::vector<LiveObservation> tracked_;
};

// Ma nhan vien cung la ten thu muc anh nen chi nhan chu, so, dau gach. Toi da 32 ky tu.
bool isValidEmployeeCode(const std::string& code);

std::string formatDate(std::time_t time);  // 2026-11-01
std::string formatTime(std::time_t time);  // 12:17:40

// Doc "2026-11-01 12:17". Tra ve rong neu sai dinh dang hoac khong phai ngay gio co that.
std::optional<std::time_t> parseDateTime(const std::string& text);

// Kiem thu logic cham cong tren co so du lieu rieng face_attendance_test.
// Can bat MySQL truoc, roi chay tu thu muc goc: ./build/attendance_test
#include "attendance.hpp"

#include <mysql.h>

#include <cstdlib>
#include <ctime>
#include <iostream>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "SAI dong " << __LINE__ << ": " #condition "\n";       \
            std::exit(1);                                                       \
        }                                                                       \
    } while (false)

namespace {

// 8 gio sang hom nay, de cac moc thoi gian trong test khong vuot sang ngay moi
std::time_t todayAtEight() {
    std::time_t now = std::time(nullptr);
    std::tm local = *std::localtime(&now);
    local.tm_hour = 8;
    local.tm_min = 0;
    local.tm_sec = 0;
    return std::mktime(&local);
}

void clearTestTables(const DatabaseConfig& config) {
    MYSQL* db = mysql_init(nullptr);
    CHECK(mysql_real_connect(db, config.host.c_str(), config.user.c_str(), config.password.c_str(),
                             config.database.c_str(), config.port, nullptr, 0) != nullptr);
    CHECK(mysql_query(db, "DROP TABLE IF EXISTS employees, attendance") == 0);
    mysql_close(db);
}

void testAttendanceRules(const DatabaseConfig& config) {
    const std::time_t morning = todayAtEight();
    AttendanceBook book(config);

    AttendanceCheck first = book.check("NV001", "Nguyễn Văn A", morning);
    CHECK(first.recorded);
    CHECK(first.record.type == "Vào");

    AttendanceCheck duplicate = book.check("NV001", "Nguyễn Văn A", morning + 59);
    CHECK(!duplicate.recorded);
    CHECK(duplicate.record.timestamp == morning);

    AttendanceCheck leaving = book.check("NV001", "Nguyễn Văn A", morning + 60);
    CHECK(leaving.recorded);
    CHECK(leaving.record.type == "Ra");

    // Nguoi khac khong bi anh huong
    CHECK(book.check("NV002", "Trần Thị B", morning + 61).record.type == "Vào");

    // Sang ngay moi thi lai la Vao
    AttendanceCheck tomorrow = book.check("NV001", "Nguyễn Văn A", morning + 24 * 3600);
    CHECK(tomorrow.recorded);
    CHECK(tomorrow.record.type == "Vào");

    // Doc lai tu co so du lieu khong mat du lieu va giu nguyen tieng Viet
    AttendanceBook reloaded(config);
    CHECK(reloaded.all().size() == 4);
    CHECK(reloaded.all()[0].name == "Nguyễn Văn A");
    CHECK(!reloaded.check("NV002", "Trần Thị B", morning + 62).recorded);
}

void testEmployeeStore(const DatabaseConfig& config) {
    EmployeeStore store(config);
    CHECK(store.nextCode() == "NV001");

    store.add({"NV001", "Nguyễn, O'Brien'; DROP TABLE employees; --", ""});
    store.add({"NV002", "Trần Thị B", "Kế toán"});
    CHECK(store.nextCode() == "NV003");
    store.update("NV002", "Trần Thị C", "Nhân sự");

    EmployeeStore reloaded(config);
    CHECK(reloaded.all().size() == 2);
    // Dau phay, dau nhay va doan giong lenh SQL duoc giu nguyen, khong bi chay nhu lenh
    CHECK(reloaded.find("NV001")->name == "Nguyễn, O'Brien'; DROP TABLE employees; --");
    CHECK(reloaded.find("NV001")->department.empty());
    CHECK(reloaded.find("NV002")->name == "Trần Thị C");

    reloaded.remove("NV001");
    CHECK(EmployeeStore(config).find("NV001") == nullptr);
    CHECK(EmployeeStore(config).all().size() == 1);
}

void testPreview(const DatabaseConfig& config) {
    clearTestTables(config);
    const std::time_t morning = todayAtEight();
    AttendanceBook book(config);

    // Xem truoc khong ghi gi, ca khi xem nhieu lan
    CHECK(book.preview("NV001", "A", morning).recorded);
    CHECK(book.preview("NV001", "A", morning).record.type == "Vào");
    CHECK(book.all().empty());
    CHECK(AttendanceBook(config).all().empty());

    book.check("NV001", "A", morning);
    CHECK(!book.preview("NV001", "A", morning + 10).recorded);
    CHECK(book.preview("NV001", "A", morning + 60).record.type == "Ra");
    CHECK(book.all().size() == 1);
}

void testManualRecord(const DatabaseConfig& config) {
    clearTestTables(config);
    const std::time_t morning = todayAtEight();
    AttendanceBook book(config);
    book.check("NV001", "Nguyễn Văn A", morning);
    book.add({morning - 3600, "NV002", "Trần Thị B", "Ra"});

    // Ban ghi nhap tay cho gio cu duoc xep dung thu tu, ca sau khi doc lai
    CHECK(book.all().front().code == "NV002");
    AttendanceBook reloaded(config);
    CHECK(reloaded.all().size() == 2);
    CHECK(reloaded.all().front().code == "NV002");
    CHECK(reloaded.all().front().type == "Ra");
}

void testParseDateTime() {
    const std::optional<std::time_t> valid = parseDateTime("2026-11-01 12:17");
    CHECK(valid.has_value());
    CHECK(formatDate(*valid) == "2026-11-01");
    CHECK(formatTime(*valid) == "12:17:00");

    CHECK(!parseDateTime("").has_value());
    CHECK(!parseDateTime("hom nay").has_value());
    CHECK(!parseDateTime("2026-02-31 08:00").has_value());
    CHECK(!parseDateTime("2026-13-01 08:00").has_value());
    CHECK(!parseDateTime("2026-11-01 25:00").has_value());
    CHECK(!parseDateTime("2026-11-01 08:00 sang").has_value());
}

void testEmployeeCode() {
    CHECK(isValidEmployeeCode("NV001"));
    CHECK(isValidEmployeeCode("ke-toan_12"));
    CHECK(!isValidEmployeeCode(""));
    CHECK(!isValidEmployeeCode("../NV001"));
    CHECK(!isValidEmployeeCode("NV 001"));
    CHECK(!isValidEmployeeCode("Nguyễn"));
    CHECK(!isValidEmployeeCode(std::string(33, 'A')));
}

void testLiveSmoother() {
    LiveSmoother smoother;

    // Anh gia lien tuc: sau 2 khung hinh da duoi nguong 0.5
    std::vector<float> score = smoother.update({{100, 100, 50, 0.05f}});
    score = smoother.update({{102, 100, 50, 0.05f}});
    CHECK(score[0] < 0.5f);

    // Nguoi that gap mot khung hinh sai khong bi bao do oan
    LiveSmoother real;
    for (int i = 0; i < 10; ++i) {
        score = real.update({{100, 100, 50, 1.0f}});
    }
    score = real.update({{100, 100, 50, 0.1f}});
    CHECK(score[0] > 0.5f);

    // Hai nguoi cung khung hinh duoc theo doi rieng, thu tu doi cho van dung
    LiveSmoother two;
    for (int i = 0; i < 6; ++i) {
        two.update({{100, 100, 50, 1.0f}, {400, 100, 50, 0.0f}});
    }
    score = two.update({{400, 100, 50, 0.0f}, {100, 100, 50, 1.0f}});
    CHECK(score[0] < 0.5f);
    CHECK(score[1] > 0.5f);
}

void testStableTracker() {
    StableTracker tracker(3);
    CHECK(tracker.update({"A"}).empty());
    CHECK(tracker.update({"A"}).empty());
    CHECK(tracker.update({"A"}).size() == 1);
    CHECK(tracker.update({"A"}).empty());  // moi lan xuat hien chi xac nhan mot lan

    // Mat dau mot khung hinh thi dem lai tu dau
    CHECK(tracker.update({"B"}).empty());
    CHECK(tracker.update({}).empty());
    CHECK(tracker.update({"B"}).empty());
    CHECK(tracker.update({"B"}).empty());
    CHECK(tracker.update({"B"}).size() == 1);

    // Quet lai cho phep xac nhan lai nguoi dang dung truoc camera
    tracker.reset();
    CHECK(tracker.update({"B"}).empty());
    CHECK(tracker.update({"B"}).empty());
    CHECK(tracker.update({"B"}).size() == 1);
}

}  // namespace

int main() {
    DatabaseConfig config = loadDatabaseConfig("data/db.conf");
    config.database += "_test";
    clearTestTables(config);
    testAttendanceRules(config);
    testEmployeeStore(config);
    testPreview(config);
    testManualRecord(config);
    testParseDateTime();
    testEmployeeCode();
    testLiveSmoother();
    testStableTracker();
    clearTestTables(config);
    std::cout << "Tat ca kiem thu deu dat\n";
    return 0;
}

// May cham cong nhan dien khuon mat, co giao dien.
//   face_kiosk [--index <so>] [--video <file>]
// Cac tuy chon de kiem thu giao dien khong can bam chuot:
//   --open add|history|staff   mo san mot trang
//   --add-name <ten>           mo trang them nguoi va bat dau quet ngay
//   --screenshot <file>        luu anh cua so sau --frames khung hinh roi thoat
//   --input <kich ban>         tu thao tac, cac buoc cach nhau boi dau ;
//                              <khung hinh>:click:<x>:<y> hoac <khung hinh>:text:<chu>

#include "attendance.hpp"
#include "face_engine.hpp"
#include "mac_input.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

const char* WINDOW_TITLE = "Máy chấm công nhận diện khuôn mặt";
const char* DATABASE_CONFIG = "data/db.conf";
const int STABLE_FRAMES = 8;           // so khung hinh lien tiep truoc khi cham cong
const float LIVE_THRESHOLD = 0.5f;     // duoi nguong nay coi la anh tren dien thoai hoac giay
const double RESULT_SECONDS = 4.0;     // thoi gian giu ket qua tren man hinh
const double PENDING_SECONDS = 1.5;    // nguoi cho xac nhan roi khung hinh lau hon thi huy
const int SCAN_COUNT = 10;             // so anh chup khi them nguoi
const double SCAN_INTERVAL_SECONDS = 0.7;
const int MIN_SCAN_FACE_SIZE = 120;
const float PANEL_HEIGHT = 142.0f;     // bang thong tin nguoi o cuoi khung camera

const ImU32 COLOR_BACKGROUND = IM_COL32(28, 31, 36, 255);
const ImU32 COLOR_SURFACE = IM_COL32(44, 48, 55, 255);
const ImU32 COLOR_TEXT = IM_COL32(255, 255, 255, 255);
const ImU32 COLOR_MUTED = IM_COL32(200, 205, 212, 255);
const ImU32 COLOR_OVERLAY = IM_COL32(20, 22, 26, 170);
const ImU32 COLOR_BLUE = IM_COL32(10, 124, 255, 255);
const ImU32 COLOR_ORANGE = IM_COL32(255, 170, 30, 255);
const ImU32 COLOR_GREEN = IM_COL32(52, 199, 89, 255);
const ImU32 COLOR_YELLOW = IM_COL32(255, 214, 10, 255);
const ImU32 COLOR_RED = IM_COL32(255, 59, 48, 255);

const std::array<const char*, 7> WEEKDAYS = {"Chủ Nhật", "Thứ Hai", "Thứ Ba",  "Thứ Tư",
                                             "Thứ Năm",  "Thứ Sáu", "Thứ Bảy"};
const std::array<const char*, 5> POSES = {"Nhìn thẳng", "Quay trái", "Quay phải", "Ngẩng lên",
                                          "Cúi xuống"};

enum class Page { Kiosk, AddPerson, History, Staff };

struct Options {
    int cameraIndex = 0;
    std::string video;
    std::string screenshot;
    std::string addName;
    std::string input;
    Page page = Page::Kiosk;
    int frames = 60;
};

struct InputStep {
    int frame = 0;
    bool isClick = false;
    bool isKey = false;
    bool isSource = false;
    bool isField = false;
    ImVec2 position;
    std::string text;
};

std::vector<InputStep> parseInputScript(const std::string& script) {
    std::vector<InputStep> steps;
    std::stringstream stream(script);
    std::string item;
    while (std::getline(stream, item, ';')) {
        const size_t first = item.find(':');
        const size_t second = item.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            throw std::runtime_error("Buoc khong hop le: " + item);
        }
        InputStep step;
        step.frame = std::stoi(item.substr(0, first));
        const std::string kind = item.substr(first + 1, second - first - 1);
        const std::string value = item.substr(second + 1);
        if (kind == "click") {
            const size_t comma = value.find(':');
            step.isClick = true;
            step.position = ImVec2(std::stof(value.substr(0, comma)), std::stof(value.substr(comma + 1)));
        } else {
            step.isKey = kind == "key";
            step.isSource = kind == "source";
            step.isField = kind == "field";  // <khung hinh>:field:<id>=<chu>
            step.text = value;
        }
        steps.push_back(step);
    }
    return steps;
}

void applyInputSteps(const std::vector<InputStep>& steps, int frame) {
    ImGuiIO& io = ImGui::GetIO();
    for (const InputStep& step : steps) {
        if (step.frame != frame) {
            continue;
        }
        if (step.isClick) {
            io.AddMousePosEvent(step.position.x, step.position.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        } else if (step.text == "<enter>" || step.text == "<esc>") {
            const ImGuiKey key = step.text == "<enter>" ? ImGuiKey_Enter : ImGuiKey_Escape;
            io.AddKeyEvent(key, true);
            io.AddKeyEvent(key, false);
        } else if (step.isField) {
            const size_t equals = step.text.find('=');
            macSetTextField(step.text.substr(0, equals).c_str(), step.text.substr(equals + 1).c_str());
        } else if (step.isSource) {
            macSelectInputSource(step.text.c_str());
        } else if (step.isKey) {
            macPrintInputState();
            macSendKey(step.text[0]);
        } else {
            io.AddInputCharactersUTF8(step.text.c_str());
        }
    }
}

struct Fonts {
    ImFont* body = nullptr;
    ImFont* bold = nullptr;
    ImFont* heading = nullptr;
    ImFont* clock = nullptr;
};

// Ket qua dang hien o bang duoi, giu lai vai giay sau khi cham cong.
struct ShownResult {
    std::string code;
    std::string title;
    std::string subtitle;
    std::string status;
    bool recorded = false;
    Clock::time_point until{};
};

struct ViewRect {
    ImVec2 min;
    ImVec2 max;
    float width() const { return max.x - min.x; }
    float height() const { return max.y - min.y; }
};

ImTextureID toTextureId(GLuint texture) {
    return static_cast<ImTextureID>(static_cast<std::intptr_t>(texture));
}

void uploadTexture(GLuint texture, const cv::Mat& bgr) {
    cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba.cols, rgba.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.data);
}

GLuint createTexture() {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

// Cat giua anh cho vua ty le khung hien thi, giong object-fit: cover.
cv::Rect coverCrop(const cv::Size& size, float aspect) {
    int width = size.width;
    int height = cvRound(width / aspect);
    if (height > size.height) {
        height = size.height;
        width = cvRound(height * aspect);
    }
    return cv::Rect((size.width - width) / 2, (size.height - height) / 2, width, height);
}

std::string findFont(const std::vector<std::string>& candidates) {
    for (const std::string& path : candidates) {
        if (fs::exists(path)) {
            return path;
        }
    }
    return "";
}

Fonts loadFonts(float dpiScale) {
    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesVietnamese();
    const std::string regular = findFont({"/System/Library/Fonts/Supplemental/Arial.ttf",
                                          "C:\\Windows\\Fonts\\segoeui.ttf",
                                          "C:\\Windows\\Fonts\\arial.ttf",
                                          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"});
    const std::string bold = findFont({"/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                                       "C:\\Windows\\Fonts\\segoeuib.ttf",
                                       "C:\\Windows\\Fonts\\arialbd.ttf",
                                       "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"});
    if (regular.empty() || bold.empty()) {
        throw std::runtime_error("Khong tim thay font ho tro tieng Viet");
    }

    // Nap font theo do phan giai that cua man hinh de chu khong bi mo tren Retina
    auto load = [&](const std::string& path, float size) {
        return io.Fonts->AddFontFromFileTTF(path.c_str(), size * dpiScale, nullptr, ranges);
    };
    Fonts fonts;
    fonts.body = load(regular, 17.0f);
    fonts.bold = load(bold, 17.0f);
    fonts.heading = load(bold, 25.0f);
    fonts.clock = load(regular, 44.0f);
    io.FontGlobalScale = 1.0f / dpiScale;
    return fonts;
}

void applyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameRounding = 10.0f;
    style.PopupRounding = 10.0f;
    style.FramePadding = ImVec2(12, 9);
    style.CellPadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 10);
    style.Colors[ImGuiCol_WindowBg] = ImColor(COLOR_BACKGROUND);
    style.Colors[ImGuiCol_PopupBg] = ImColor(COLOR_BACKGROUND);  // khac mau o nhap va nut
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
    style.Colors[ImGuiCol_FrameBg] = ImColor(COLOR_SURFACE);
    style.Colors[ImGuiCol_Button] = ImColor(COLOR_SURFACE);
    style.Colors[ImGuiCol_ButtonHovered] = ImColor(IM_COL32(60, 65, 74, 255));
    style.Colors[ImGuiCol_ButtonActive] = ImColor(IM_COL32(74, 80, 90, 255));
    style.Colors[ImGuiCol_TableHeaderBg] = ImColor(COLOR_SURFACE);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    style.Colors[ImGuiCol_CheckMark] = ImColor(COLOR_BLUE);
}

float textWidth(ImFont* font, float size, const std::string& text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

void drawText(ImDrawList* draw, ImFont* font, float size, ImVec2 position, ImU32 color,
              const std::string& text) {
    draw->AddText(font, size, position, color, text.c_str());
}

void drawBrackets(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color) {
    const float length = std::min(max.x - min.x, max.y - min.y) * 0.25f;
    const float thickness = 3.0f;
    const auto corner = [&](ImVec2 point, float dx, float dy) {
        draw->AddLine(point, ImVec2(point.x + dx * length, point.y), color, thickness);
        draw->AddLine(point, ImVec2(point.x, point.y + dy * length), color, thickness);
    };
    corner(min, 1, 1);
    corner(ImVec2(max.x, min.y), -1, 1);
    corner(ImVec2(min.x, max.y), 1, -1);
    corner(max, -1, -1);
}

void drawCheckIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    draw->AddLine(ImVec2(center.x - size, center.y), ImVec2(center.x - size * 0.2f, center.y + size * 0.8f),
                  color, 2.5f);
    draw->AddLine(ImVec2(center.x - size * 0.2f, center.y + size * 0.8f),
                  ImVec2(center.x + size, center.y - size * 0.7f), color, 2.5f);
}

void drawCrossIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    draw->AddLine(ImVec2(center.x - size, center.y - size), ImVec2(center.x + size, center.y + size),
                  color, 2.5f);
    draw->AddLine(ImVec2(center.x + size, center.y - size), ImVec2(center.x - size, center.y + size),
                  color, 2.5f);
}

bool drawButton(const char* label, ImVec2 position, ImVec2 size, bool primary = false,
                ImU32 primaryColor = COLOR_BLUE) {
    if (primary) {
        ImVec4 color = ImColor(primaryColor).Value;
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        color.w = 0.85f;
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
        color.w = 0.7f;
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
    }
    ImGui::SetCursorScreenPos(position);
    const bool clicked = ImGui::Button(label, size);
    if (primary) {
        ImGui::PopStyleColor(3);
    }
    return clicked;
}

// Ve khung o nhap roi dat o nhap goc cua macOS len tren.
void nativeInput(const char* id, char* buffer, size_t size, float width, bool focus = false) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(position, ImVec2(position.x + width, position.y + height),
                                              COLOR_SURFACE, 10.0f);
    ImGui::Dummy(ImVec2(width, height));
    const float padding = 12.0f;
    const float textHeight = 22.0f;
    macTextField(id, position.x + padding, position.y + (height - textHeight) / 2,
                 width - 2 * padding, textHeight, buffer, size, focus);
}

class KioskApp {
public:
    KioskApp(const Options& options, const Fonts& fonts)
        : options_(options),
          fonts_(fonts),
          engine_(makeDefaultEngine()),
          employees_(loadDatabaseConfig(DATABASE_CONFIG)),
          attendance_(loadDatabaseConfig(DATABASE_CONFIG)),
          tracker_(STABLE_FRAMES),
          page_(options.page) {
        cameraTexture_ = createTexture();
        engine_.loadKnownFaces(KNOWN_FACES_DIR);
        reloadAvatars();
        std::snprintf(historyDate_.data(), historyDate_.size(), "%s",
                      formatDate(std::time(nullptr)).c_str());
        resetAddForm();
        if (!options.addName.empty()) {
            page_ = Page::AddPerson;
            std::snprintf(newName_.data(), newName_.size(), "%s", options.addName.c_str());
            startScan();
        }
    }

    // Dong cua so giua luc quet thi bo anh do dang, tranh de lai khuon mat khong co ho so
    ~KioskApp() { cancelScan(); }

    void frame() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        switch (page_) {
            case Page::Kiosk: drawKiosk(); break;
            case Page::AddPerson: drawAddPerson(); break;
            case Page::History: drawHistory(); break;
            case Page::Staff: drawStaff(); break;
        }
        ImGui::End();
    }

private:
    void openCamera() {
        if (options_.video.empty()) {
            camera_.open(options_.cameraIndex);
            camera_.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
            camera_.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        } else {
            camera_.open(options_.video);
        }
        cameraOk_ = camera_.isOpened();
    }

    bool readFrame() {
        if (!cameraOk_) {
            // Chi mo khi trang can camera. Lan dau macOS hoi quyen nen thu lai,
            // de khong phai mo lai chuong trinh sau khi cho phep
            const auto now = Clock::now();
            if (now - lastCameraTry_ >= std::chrono::seconds(2)) {
                lastCameraTry_ = now;
                openCamera();
            }
            if (!cameraOk_) {
                return false;
            }
        }
        if (!camera_.read(frame_) || frame_.empty()) {
            if (options_.video.empty()) {
                return false;
            }
            // File video het thi phat lai tu dau
            camera_.set(cv::CAP_PROP_POS_FRAMES, 0);
            return camera_.read(frame_) && !frame_.empty();
        }
        return true;
    }

    // Lay khung hinh, cat vua khung hien thi va dua len man hinh dang lat guong.
    bool updateCamera(const ViewRect& view, cv::Mat& cropped) {
        if (!readFrame()) {
            return false;
        }
        cropped = frame_(coverCrop(frame_.size(), view.width() / view.height()));
        cv::Mat mirrored;
        cv::flip(cropped, mirrored, 1);
        uploadTexture(cameraTexture_, mirrored);
        return true;
    }

    void drawCameraView(ImDrawList* draw, const ViewRect& view, bool hasFrame) {
        const float rounding = 14.0f;
        if (hasFrame) {
            draw->AddImageRounded(toTextureId(cameraTexture_), view.min, view.max, ImVec2(0, 0),
                                  ImVec2(1, 1), IM_COL32_WHITE, rounding);
            return;
        }
        draw->AddRectFilled(view.min, view.max, COLOR_SURFACE, rounding);
        const std::string message = "Không mở được camera";
        drawText(draw, fonts_.bold, 18.0f,
                 ImVec2(view.min.x + (view.width() - textWidth(fonts_.bold, 18.0f, message)) / 2,
                        view.min.y + view.height() / 3),
                 COLOR_ORANGE, message);
        const std::string hint = "Kiểm tra quyền Camera trong System Settings";
        drawText(draw, fonts_.body, 15.0f,
                 ImVec2(view.min.x + (view.width() - textWidth(fonts_.body, 15.0f, hint)) / 2,
                        view.min.y + view.height() / 3 + 28),
                 COLOR_MUTED, hint);
    }

    // Doi vi tri khuon mat tu anh goc sang man hinh da lat guong.
    ViewRect faceOnScreen(const FaceResult& face, const ViewRect& view, const cv::Mat& cropped) const {
        const float scale = view.width() / cropped.cols;
        const float left = view.min.x + (cropped.cols - face.box.x - face.box.width) * scale;
        const float top = view.min.y + face.box.y * scale;
        return {ImVec2(left, top),
                ImVec2(left + face.box.width * scale, top + face.box.height * scale)};
    }

    // Thay diem tung khung hinh bang diem da lam muot.
    void smoothLiveScores(std::vector<FaceResult>& faces) {
        std::vector<LiveObservation> observations;
        for (const FaceResult& face : faces) {
            observations.push_back({face.box.x + face.box.width / 2.f, face.box.y + face.box.height / 2.f,
                                    static_cast<float>(std::max(face.box.width, face.box.height)),
                                    face.liveScore});
        }
        const std::vector<float> smoothed = liveSmoother_.update(observations);
        for (size_t i = 0; i < faces.size(); ++i) {
            faces[i].liveScore = smoothed[i];
        }
    }

    std::string displayName(const std::string& code) const {
        const Employee* employee = employees_.find(code);
        return employee != nullptr ? employee->name : code;
    }

    std::string subtitleFor(const std::string& code) const {
        const Employee* employee = employees_.find(code);
        std::string subtitle = "Mã: " + code;
        if (employee != nullptr && !employee->department.empty()) {
            subtitle += " · " + employee->department;
        }
        return subtitle;
    }

    // Nhan ra nguoi on dinh: chua ghi gi, chi hoi xac nhan.
    void requestConfirmation(const std::string& code) {
        if (!pendingCode_.empty()) {
            return;
        }
        const AttendanceCheck preview = attendance_.preview(code, displayName(code), std::time(nullptr));
        if (preview.recorded) {
            pendingCode_ = code;
            pendingType_ = preview.record.type;
            pendingLastSeen_ = Clock::now();
        } else if (shown_.code != code) {
            showResult(code, preview);
        }
    }

    void confirmAttendance() {
        const std::string code = pendingCode_;
        showResult(code, attendance_.check(code, displayName(code), std::time(nullptr)));
        pendingCode_.clear();
        tracker_.reset();  // de nguoi khac trong khung hinh duoc hoi tiep
    }

    void showResult(const std::string& code, const AttendanceCheck& check) {
        const std::string name = displayName(code);
        const std::string clock = formatTime(check.record.timestamp);
        ShownResult result;
        result.code = code;
        result.title = name;
        result.subtitle = subtitleFor(code);
        result.recorded = check.recorded;
        result.status = check.recorded
                            ? (check.record.type == "Vào" ? "Đã chấm công vào lúc " : "Đã chấm công ra lúc ") + clock
                            : "Đã chấm công lúc " + clock;
        result.until = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(RESULT_SECONDS));
        shown_ = result;
    }

    void drawKiosk() {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const float margin = 16.0f;
        const float buttonHeight = 52.0f;
        const float gap = 10.0f;
        const ViewRect view{ImVec2(margin, margin),
                            ImVec2(windowSize.x - margin,
                                   windowSize.y - margin - 2 * buttonHeight - 2 * gap)};

        cv::Mat cropped;
        const bool hasFrame = updateCamera(view, cropped);
        std::vector<FaceResult> faces;
        std::set<std::string> seen;
        if (hasFrame) {
            faces = engine_.process(cropped);
            smoothLiveScores(faces);
            for (const FaceResult& face : faces) {
                // Anh gia khong duoc tinh, du khuon mat trong anh khop voi nhan vien
                if (face.name != FaceEngine::UNKNOWN_NAME && face.liveScore >= LIVE_THRESHOLD) {
                    seen.insert(face.name);
                }
            }
            for (const std::string& code : tracker_.update(seen)) {
                requestConfirmation(code);
            }
        }
        if (seen.count(pendingCode_) != 0) {
            pendingLastSeen_ = Clock::now();
        } else if (!pendingCode_.empty() &&
                   std::chrono::duration<double>(Clock::now() - pendingLastSeen_).count() > PENDING_SECONDS) {
            pendingCode_.clear();
        }
        if (shown_.code.empty() == false && Clock::now() > shown_.until) {
            shown_ = ShownResult{};
        }

        drawCameraView(draw, view, hasFrame);
        drawClock(draw, view);

        const FaceResult* largest = nullptr;
        bool spoofDetected = false;
        for (const FaceResult& face : faces) {
            if (largest == nullptr || face.box.area() > largest->box.area()) {
                largest = &face;
            }
            const bool spoof = face.liveScore < LIVE_THRESHOLD;
            const bool known = face.name != FaceEngine::UNKNOWN_NAME;
            spoofDetected = spoofDetected || spoof;
            const ImU32 color = spoof ? COLOR_RED : known ? COLOR_GREEN : COLOR_ORANGE;
            const ViewRect box = faceOnScreen(face, view, cropped);
            drawBrackets(draw, box.min, box.max, color);
            const std::string label = spoof   ? "ẢNH GIẢ"
                                      : known ? displayName(face.name)
                                              : "Không xác định";
            const float labelWidth = textWidth(fonts_.heading, 25.0f, label);
            const float centeredX = box.min.x + (box.max.x - box.min.x - labelWidth) / 2;
            const ImVec2 labelPosition(
                std::max(view.min.x + 4, std::min(centeredX, view.max.x - labelWidth - 4)),
                std::max(view.min.y + 4, box.min.y - 34));
            drawText(draw, fonts_.heading, 25.0f, ImVec2(labelPosition.x + 1, labelPosition.y + 1),
                     IM_COL32(0, 0, 0, 200), label);  // bong de chu noi tren nen sang
            drawText(draw, fonts_.heading, 25.0f, labelPosition, color, label);
        }

        if (spoofDetected) {
            draw->AddRect(view.min, view.max, COLOR_RED, 14.0f, 0, 6.0f);
        }
        drawStatusPill(draw, view, spoofDetected);
        drawPersonPanel(draw, view, largest, spoofDetected);
        // Chi xac nhan duoc khi dung nguoi do con trong khung hinh va khong co anh gia
        if (seen.count(pendingCode_) != 0 && !spoofDetected) {
            drawConfirmButtons(view);
        }
        drawKioskButtons(view, margin, buttonHeight, gap);
    }

    void drawConfirmButtons(const ViewRect& view) {
        const float inset = 12.0f;
        const float gap = 10.0f;
        const float height = 48.0f;
        const float top = view.max.y - PANEL_HEIGHT - inset - height - gap;
        const float cancelWidth = 130.0f;
        const float confirmWidth = view.width() - 2 * inset - gap - cancelWidth;

        ImGui::PushFont(fonts_.bold);
        const bool confirm = drawButton("Xác nhận chấm công", ImVec2(view.min.x + inset, top),
                                        ImVec2(confirmWidth, height), true, COLOR_GREEN);
        const bool cancel = drawButton("Hủy", ImVec2(view.min.x + inset + confirmWidth + gap, top),
                                       ImVec2(cancelWidth, height));
        ImGui::PopFont();

        if (confirm || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            confirmAttendance();
        } else if (cancel || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            pendingCode_.clear();  // khong hoi lai cho den khi roi khung hinh hoac bam Quet lai
        }
    }

    void drawClock(ImDrawList* draw, const ViewRect& view) {
        const std::time_t now = std::time(nullptr);
        const int weekday = std::localtime(&now)->tm_wday;
        const ImVec2 max(view.min.x + 196, view.min.y + 136);
        draw->AddRectFilled(view.min, max, COLOR_OVERLAY, 14.0f, ImDrawFlags_RoundCornersTopLeft);

        const auto centered = [&](ImFont* font, float size, float y, const std::string& text) {
            drawText(draw, font, size,
                     ImVec2(view.min.x + (196 - textWidth(font, size, text)) / 2, view.min.y + y),
                     COLOR_TEXT, text);
        };
        centered(fonts_.body, 19.0f, 14, formatDate(now));
        centered(fonts_.clock, 44.0f, 42, formatTime(now));
        centered(fonts_.body, 18.0f, 100, WEEKDAYS[weekday]);
    }

    void drawStatusPill(ImDrawList* draw, const ViewRect& view, bool spoof) {
        const bool pending = !pendingCode_.empty();
        const bool done = !shown_.code.empty() && !pending;
        const std::string label = spoof     ? "Cảnh báo"
                                  : pending ? "Chờ xác nhận"
                                  : done    ? "Thành công"
                                            : "Đang quét";
        const ImU32 dot = spoof ? COLOR_RED : done ? COLOR_GREEN : COLOR_YELLOW;
        const float width = textWidth(fonts_.bold, 18.0f, label) + 56;
        const ImVec2 max(view.max.x - 12, view.min.y + 54);
        const ImVec2 min(max.x - width, view.min.y + 12);
        draw->AddRectFilled(min, max, COLOR_OVERLAY, 21.0f);
        draw->AddCircleFilled(ImVec2(min.x + 24, min.y + 21), 7.0f, dot);
        drawText(draw, fonts_.bold, 18.0f, ImVec2(min.x + 42, min.y + 11), COLOR_TEXT, label);
    }

    void drawAvatar(ImDrawList* draw, ImVec2 center, float radius, const std::string& code) {
        auto avatar = avatars_.find(code);
        if (avatar != avatars_.end()) {
            draw->AddImageRounded(toTextureId(avatar->second),
                                  ImVec2(center.x - radius, center.y - radius),
                                  ImVec2(center.x + radius, center.y + radius), ImVec2(0, 0),
                                  ImVec2(1, 1), IM_COL32_WHITE, radius);
        } else {
            draw->AddCircleFilled(center, radius, IM_COL32(190, 195, 205, 255), 64);
            const float size = 40.0f;
            drawText(draw, fonts_.heading, size,
                     ImVec2(center.x - textWidth(fonts_.heading, size, "?") / 2, center.y - size / 2 - 2),
                     IM_COL32_WHITE, "?");
        }
        draw->AddCircle(center, radius, COLOR_BLUE, 64, 3.0f);
    }

    void drawPersonPanel(ImDrawList* draw, const ViewRect& view, const FaceResult* largest, bool spoof) {
        const float inset = 12.0f;
        const ImVec2 min(view.min.x + inset, view.max.y - PANEL_HEIGHT - inset);
        const ImVec2 max(view.max.x - inset, view.max.y - inset);
        draw->AddRectFilled(min, max, COLOR_OVERLAY, 14.0f);

        std::string title = "Mời nhìn vào camera";
        std::string subtitle = "Đưa khuôn mặt vào giữa khung hình";
        std::string status;
        std::string code;
        ImU32 statusColor = COLOR_ORANGE;
        ImU32 titleColor = COLOR_TEXT;
        bool success = false;

        if (spoof) {
            title = "Không phải người thật";
            subtitle = "Phát hiện ảnh trên điện thoại hoặc giấy";
            status = "Không chấm công";
            statusColor = COLOR_RED;
            titleColor = COLOR_RED;
        } else if (!pendingCode_.empty()) {
            code = pendingCode_;
            title = displayName(code);
            subtitle = subtitleFor(code);
            status = pendingType_ == "Vào" ? "Xác nhận chấm công vào?" : "Xác nhận chấm công ra?";
            statusColor = COLOR_YELLOW;
            success = true;
        } else if (!shown_.code.empty()) {
            code = shown_.code;
            title = shown_.title;
            subtitle = shown_.subtitle;
            status = shown_.status;
            statusColor = shown_.recorded ? COLOR_GREEN : COLOR_YELLOW;
            success = true;
        } else if (largest != nullptr && largest->name != FaceEngine::UNKNOWN_NAME) {
            code = largest->name;
            title = displayName(code);
            subtitle = subtitleFor(code);
            status = "Đã nhận ra khuôn mặt";
            statusColor = COLOR_YELLOW;
            success = true;
        } else if (largest != nullptr) {
            title = "Không xác định";
            subtitle = "Khuôn mặt chưa đăng ký";
            status = "Không có trong hệ thống";
        }

        drawAvatar(draw, ImVec2(min.x + 72, (min.y + max.y) / 2), 52.0f, code);

        const float textLeft = min.x + 142;
        drawText(draw, fonts_.heading, 23.0f, ImVec2(textLeft, min.y + 18), titleColor, title);
        drawText(draw, fonts_.body, 17.0f, ImVec2(textLeft, min.y + 56), COLOR_MUTED, subtitle);
        if (!status.empty()) {
            const ImVec2 icon(textLeft + 8, min.y + 104);
            if (success) {
                drawCheckIcon(draw, icon, 6.0f, statusColor);
            } else {
                drawCrossIcon(draw, icon, 5.0f, statusColor);
            }
            drawText(draw, fonts_.bold, 16.0f, ImVec2(textLeft + 24, min.y + 95), statusColor, status);
        }
    }

    void drawKioskButtons(const ViewRect& view, float margin, float height, float gap) {
        const float width = (view.width() - gap) / 2;
        const float rowOne = view.max.y + gap;
        const float rowTwo = rowOne + height + gap;
        const ImVec2 size(width, height);

        ImGui::PushFont(fonts_.bold);
        if (drawButton("+  Thêm người", ImVec2(margin, rowOne), size)) {
            resetAddForm();
            page_ = Page::AddPerson;
        }
        if (drawButton("Quét lại", ImVec2(margin + width + gap, rowOne), size, true)) {
            shown_ = ShownResult{};
            pendingCode_.clear();
            tracker_.reset();
        }
        if (drawButton("Lịch sử chấm công", ImVec2(margin, rowTwo), size)) {
            page_ = Page::History;
        }
        if (drawButton("Quản lý nhân viên", ImVec2(margin + width + gap, rowTwo), size)) {
            page_ = Page::Staff;
        }
        ImGui::PopFont();
    }

    void drawPageHeader(const char* title) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::PushFont(fonts_.bold);
        if (drawButton("<  Quay lại", ImVec2(16, 16), ImVec2(120, 42))) {
            cancelScan();
            page_ = Page::Kiosk;
        }
        ImGui::PopFont();
        drawText(draw, fonts_.heading, 25.0f, ImVec2(152, 23), COLOR_TEXT, title);
        ImGui::SetCursorScreenPos(ImVec2(16, 74));
    }

    void resetAddForm() {
        std::snprintf(newCode_.data(), newCode_.size(), "%s", employees_.nextCode().c_str());
        newName_[0] = '\0';
        newDepartment_[0] = '\0';
        addError_.clear();
        focusNameField_ = true;
    }

    void startScan() {
        scanCode_ = newCode_.data();
        scanSaved_ = 0;
        scanning_ = true;
        lastShot_ = Clock::now();
    }

    void cancelScan() {
        if (scanning_) {
            std::error_code error;
            fs::remove_all(fs::path(KNOWN_FACES_DIR) / scanCode_, error);
            scanning_ = false;
        }
    }

    void finishScan() {
        scanning_ = false;
        employees_.add({scanCode_, newName_.data(), newDepartment_.data()});
        engine_.loadKnownFaces(KNOWN_FACES_DIR);
        reloadAvatars();
        tracker_.reset();
        page_ = Page::Kiosk;
    }

    void drawAddPerson() {
        drawPageHeader("Thêm người");
        if (scanning_) {
            drawScan();
            return;
        }

        ImGui::PushFont(fonts_.body);
        const float width = ImGui::GetWindowSize().x - 32;
        ImGui::SetCursorPosX(16);
        ImGui::TextUnformatted("Mã nhân viên");
        ImGui::SetCursorPosX(16);
        nativeInput("code", newCode_.data(), newCode_.size(), width);
        ImGui::SetCursorPosX(16);
        ImGui::TextUnformatted("Họ và tên");
        ImGui::SetCursorPosX(16);
        nativeInput("name", newName_.data(), newName_.size(), width, focusNameField_);
        focusNameField_ = false;
        ImGui::SetCursorPosX(16);
        ImGui::TextUnformatted("Phòng ban (không bắt buộc)");
        ImGui::SetCursorPosX(16);
        nativeInput("department", newDepartment_.data(), newDepartment_.size(), width);
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(16);
        ImGui::TextWrapped("Bước tiếp theo: chương trình mở camera và tự chụp %d ảnh. "
                           "Hãy xoay mặt theo hướng dẫn trên màn hình.", SCAN_COUNT);
        if (!addError_.empty()) {
            ImGui::SetCursorPosX(16);
            ImGui::TextColored(ImColor(COLOR_ORANGE), "%s", addError_.c_str());
        }
        ImGui::PopFont();

        ImGui::BeginDisabled(newName_[0] == '\0' || newCode_[0] == '\0');
        ImGui::PushFont(fonts_.bold);
        ImGui::Dummy(ImVec2(0, 4));
        if (drawButton("Bắt đầu quét", ImVec2(16, ImGui::GetCursorScreenPos().y), ImVec2(width, 52), true)) {
            const std::string code = newCode_.data();
            if (!isValidEmployeeCode(code)) {
                addError_ = "Mã chỉ gồm chữ không dấu, số, dấu - hoặc _";
            } else if (employees_.find(code) != nullptr || fs::exists(fs::path(KNOWN_FACES_DIR) / code)) {
                addError_ = "Mã " + code + " đã tồn tại";
            } else {
                addError_.clear();
                startScan();
            }
        }
        ImGui::PopFont();
        ImGui::EndDisabled();
    }

    void drawScan() {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ViewRect view{ImVec2(16, 74), ImVec2(windowSize.x - 16, windowSize.y - 16)};

        cv::Mat cropped;
        const bool hasFrame = updateCamera(view, cropped);
        drawCameraView(draw, view, hasFrame);

        std::vector<FaceResult> faces;
        if (hasFrame) {
            faces = engine_.process(cropped);
            smoothLiveScores(faces);
        }

        std::string message = POSES[scanSaved_ % POSES.size()];
        ImU32 color = COLOR_ORANGE;
        if (faces.empty()) {
            message = "Không thấy khuôn mặt";
        } else if (faces.size() > 1) {
            message = "Chỉ để một người trong khung";
        } else if (faces[0].liveScore < LIVE_THRESHOLD) {
            message = "Ảnh giả, hãy dùng khuôn mặt thật";
            color = COLOR_RED;
        } else if (faces[0].name != FaceEngine::UNKNOWN_NAME) {
            message = "Khuôn mặt đã đăng ký: " + displayName(faces[0].name);
        } else if (std::min(faces[0].box.width, faces[0].box.height) < MIN_SCAN_FACE_SIZE) {
            message = "Lại gần camera hơn";
        } else {
            color = COLOR_GREEN;
            const auto now = Clock::now();
            if (std::chrono::duration<double>(now - lastShot_).count() >= SCAN_INTERVAL_SECONDS) {
                saveKnownImage(scanCode_, cropped);
                ++scanSaved_;
                lastShot_ = now;
            }
        }

        for (const FaceResult& face : faces) {
            const ViewRect box = faceOnScreen(face, view, cropped);
            drawBrackets(draw, box.min, box.max, color);
        }

        const ImVec2 panelMin(view.min.x + 12, view.max.y - 96);
        const ImVec2 panelMax(view.max.x - 12, view.max.y - 12);
        draw->AddRectFilled(panelMin, panelMax, COLOR_OVERLAY, 14.0f);
        drawText(draw, fonts_.heading, 23.0f, ImVec2(panelMin.x + 16, panelMin.y + 12), COLOR_TEXT, message);
        const float barLeft = panelMin.x + 16;
        const float barRight = panelMax.x - 16;
        const float progress = static_cast<float>(scanSaved_) / SCAN_COUNT;
        draw->AddRectFilled(ImVec2(barLeft, panelMin.y + 56), ImVec2(barRight, panelMin.y + 64),
                            IM_COL32(255, 255, 255, 50), 4.0f);
        draw->AddRectFilled(ImVec2(barLeft, panelMin.y + 56),
                            ImVec2(barLeft + (barRight - barLeft) * progress, panelMin.y + 64),
                            COLOR_GREEN, 4.0f);

        if (scanSaved_ >= SCAN_COUNT) {
            finishScan();
        }
    }

    void drawHistory() {
        drawPageHeader("Lịch sử chấm công");
        ImGui::PushFont(fonts_.body);
        ImGui::SetCursorPosX(16);
        ImGui::SetNextItemWidth(150);
        ImGui::InputTextWithHint("##date", "YYYY-MM-DD", historyDate_.data(), historyDate_.size());
        ImGui::SameLine();
        if (ImGui::Button("Hôm nay")) {
            std::snprintf(historyDate_.data(), historyDate_.size(), "%s",
                          formatDate(std::time(nullptr)).c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Tất cả")) {
            historyDate_[0] = '\0';
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(employees_.all().empty());
        if (ImGui::Button("+ Ghi thủ công")) {
            const std::time_t now = std::time(nullptr);
            std::snprintf(manualTime_.data(), manualTime_.size(), "%s %s", formatDate(now).c_str(),
                          formatTime(now).substr(0, 5).c_str());
            manualEmployee_ = 0;
            manualType_ = 0;
            manualError_.clear();
            ImGui::OpenPopup("Ghi chấm công");
        }
        ImGui::EndDisabled();

        const std::string filter = historyDate_.data();
        ImGui::SetCursorPosX(16);
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (ImGui::BeginTable("history", 5, flags, ImVec2(ImGui::GetWindowSize().x - 32, available.y - 8))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Ngày", ImGuiTableColumnFlags_WidthFixed, 96);
            ImGui::TableSetupColumn("Giờ", ImGuiTableColumnFlags_WidthFixed, 72);
            ImGui::TableSetupColumn("Mã", ImGuiTableColumnFlags_WidthFixed, 62);
            ImGui::TableSetupColumn("Họ tên", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Loại", ImGuiTableColumnFlags_WidthFixed, 44);
            ImGui::TableHeadersRow();

            const std::vector<AttendanceRecord>& records = attendance_.all();
            for (auto record = records.rbegin(); record != records.rend(); ++record) {
                const std::string date = formatDate(record->timestamp);
                if (!filter.empty() && date != filter) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(date.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatTime(record->timestamp).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(record->code.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(record->name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(ImColor(record->type == "Vào" ? COLOR_GREEN : COLOR_ORANGE),
                                   "%s", record->type.c_str());
            }
            ImGui::EndTable();
        }
        drawManualPopup();
        ImGui::PopFont();
    }

    void drawManualPopup() {
        if (!ImGui::BeginPopupModal("Ghi chấm công", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        const std::vector<Employee>& all = employees_.all();
        const auto label = [](const Employee& employee) {
            return employee.code + " - " + employee.name;
        };

        ImGui::TextUnformatted("Nhân viên");
        ImGui::SetNextItemWidth(320);
        if (ImGui::BeginCombo("##employee", label(all[manualEmployee_]).c_str())) {
            for (int i = 0; i < static_cast<int>(all.size()); ++i) {
                if (ImGui::Selectable(label(all[i]).c_str(), i == manualEmployee_)) {
                    manualEmployee_ = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TextUnformatted("Loại");
        ImGui::RadioButton("Vào", &manualType_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Ra", &manualType_, 1);

        ImGui::TextUnformatted("Thời gian (YYYY-MM-DD HH:MM)");
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("##manualTime", manualTime_.data(), manualTime_.size());

        if (!manualError_.empty()) {
            ImGui::TextColored(ImColor(COLOR_ORANGE), "%s", manualError_.c_str());
        }
        if (ImGui::Button("Lưu", ImVec2(155, 0))) {
            const std::optional<std::time_t> time = parseDateTime(manualTime_.data());
            if (!time.has_value()) {
                manualError_ = "Thời gian không hợp lệ";
            } else if (*time > std::time(nullptr)) {
                manualError_ = "Không thể ghi cho thời gian trong tương lai";
            } else {
                const Employee& employee = all[manualEmployee_];
                attendance_.add({*time, employee.code, employee.name, manualType_ == 0 ? "Vào" : "Ra"});
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Hủy", ImVec2(155, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void drawStaff() {
        drawPageHeader("Quản lý nhân viên");
        ImGui::PushFont(fonts_.body);
        ImGui::SetCursorPosX(16);
        std::string pendingEdit;
        std::string pendingDelete;

        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (ImGui::BeginTable("staff", 4, flags, ImVec2(ImGui::GetWindowSize().x - 32, available.y - 8))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Mã", ImGuiTableColumnFlags_WidthFixed, 62);
            ImGui::TableSetupColumn("Họ tên", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Phòng ban", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 112);
            ImGui::TableHeadersRow();

            for (const Employee& employee : employees_.all()) {
                ImGui::PushID(employee.code.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(employee.code.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(employee.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(employee.department.c_str());
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Sửa")) {
                    pendingEdit = employee.code;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Xóa")) {
                    pendingDelete = employee.code;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (!pendingEdit.empty()) {
            const Employee* employee = employees_.find(pendingEdit);
            selectedCode_ = pendingEdit;
            std::snprintf(editName_.data(), editName_.size(), "%s", employee->name.c_str());
            std::snprintf(editDepartment_.data(), editDepartment_.size(), "%s",
                          employee->department.c_str());
            focusEditField_ = true;
            ImGui::OpenPopup("Sửa nhân viên");
        }
        if (!pendingDelete.empty()) {
            selectedCode_ = pendingDelete;
            ImGui::OpenPopup("Xóa nhân viên");
        }
        drawEditPopup();
        drawDeletePopup();
        ImGui::PopFont();
    }

    void drawEditPopup() {
        if (!ImGui::BeginPopupModal("Sửa nhân viên", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextUnformatted("Họ và tên");
        nativeInput("editName", editName_.data(), editName_.size(), 320, focusEditField_);
        focusEditField_ = false;
        ImGui::TextUnformatted("Phòng ban");
        nativeInput("editDepartment", editDepartment_.data(), editDepartment_.size(), 320);
        ImGui::BeginDisabled(editName_[0] == '\0');
        if (ImGui::Button("Lưu", ImVec2(155, 0))) {
            employees_.update(selectedCode_, editName_.data(), editDepartment_.data());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Hủy", ImVec2(155, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void drawDeletePopup() {
        if (!ImGui::BeginPopupModal("Xóa nhân viên", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::Text("Xóa %s và toàn bộ ảnh khuôn mặt?", displayName(selectedCode_).c_str());
        ImGui::TextUnformatted("Lịch sử chấm công vẫn được giữ lại.");
        if (ImGui::Button("Xóa", ImVec2(155, 0))) {
            employees_.remove(selectedCode_);
            fs::remove_all(fs::path(KNOWN_FACES_DIR) / selectedCode_);
            engine_.loadKnownFaces(KNOWN_FACES_DIR);
            reloadAvatars();
            tracker_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Hủy", ImVec2(155, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Tao anh dai dien tu anh dau tien cua moi nguoi.
    void reloadAvatars() {
        for (const auto& avatar : avatars_) {
            glDeleteTextures(1, &avatar.second);
        }
        avatars_.clear();
        if (!fs::is_directory(KNOWN_FACES_DIR)) {
            return;
        }

        for (const auto& person : fs::directory_iterator(KNOWN_FACES_DIR)) {
            cv::Mat image = cv::imread((person.path() / "1.jpg").string());
            if (image.empty()) {
                continue;
            }
            std::vector<FaceResult> faces = engine_.process(image);
            if (faces.empty()) {
                continue;
            }
            const cv::Rect box = faces[0].box;
            const int side = cvRound(std::max(box.width, box.height) * 1.3);
            const cv::Rect square(box.x + box.width / 2 - side / 2, box.y + box.height / 2 - side / 2,
                                  side, side);
            const cv::Rect visible = square & cv::Rect(0, 0, image.cols, image.rows);
            cv::Mat crop;
            cv::resize(image(visible), crop, cv::Size(160, 160));
            GLuint texture = createTexture();
            uploadTexture(texture, crop);
            avatars_[person.path().filename().string()] = texture;
        }
    }

    Options options_;
    Fonts fonts_;
    FaceEngine engine_;
    EmployeeStore employees_;
    AttendanceBook attendance_;
    StableTracker tracker_;
    LiveSmoother liveSmoother_;
    Page page_;

    cv::VideoCapture camera_;
    bool cameraOk_ = false;
    Clock::time_point lastCameraTry_{};
    cv::Mat frame_;
    GLuint cameraTexture_ = 0;
    std::map<std::string, GLuint> avatars_;
    ShownResult shown_;
    std::string pendingCode_;  // nguoi dang cho xac nhan cham cong
    std::string pendingType_;
    Clock::time_point pendingLastSeen_{};

    std::array<char, 33> newCode_{};
    std::array<char, 128> newName_{};
    std::array<char, 128> newDepartment_{};
    std::string addError_;
    bool focusNameField_ = true;
    bool focusEditField_ = false;
    bool scanning_ = false;
    std::string scanCode_;
    int scanSaved_ = 0;
    Clock::time_point lastShot_{};

    std::array<char, 11> historyDate_{};
    int manualEmployee_ = 0;
    int manualType_ = 0;
    std::array<char, 20> manualTime_{};
    std::string manualError_;
    std::string selectedCode_;
    std::array<char, 128> editName_{};
    std::array<char, 128> editDepartment_{};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) {
            throw std::runtime_error("Thieu gia tri cho " + arg);
        }
        const std::string value = argv[++i];
        if (arg == "--index") {
            options.cameraIndex = std::stoi(value);
        } else if (arg == "--video") {
            options.video = value;
        } else if (arg == "--screenshot") {
            options.screenshot = value;
        } else if (arg == "--frames") {
            options.frames = std::stoi(value);
        } else if (arg == "--add-name") {
            options.addName = value;
        } else if (arg == "--input") {
            options.input = value;
        } else if (arg == "--open") {
            if (value == "add") {
                options.page = Page::AddPerson;
            } else if (value == "history") {
                options.page = Page::History;
            } else if (value == "staff") {
                options.page = Page::Staff;
            } else {
                throw std::runtime_error("Trang khong hop le: " + value);
            }
        } else {
            throw std::runtime_error("Tham so khong hop le: " + arg);
        }
    }
    return options;
}

void saveScreenshot(GLFWwindow* window, const std::string& path) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    cv::Mat image(height, width, CV_8UC4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.data);
    cv::flip(image, image, 0);
    cv::cvtColor(image, image, cv::COLOR_RGBA2BGR);
    cv::imwrite(path, image);
}

int run(const Options& options) {
    if (!glfwInit()) {
        throw std::runtime_error("Khong khoi tao duoc cua so");
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    // Cua so doc nhu man hinh may cham cong, thu nho neu man hinh thap
    int workX = 0, workY = 0, workWidth = 0, workHeight = 0;
    glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &workX, &workY, &workWidth, &workHeight);
    const int height = std::min(860, workHeight - 60);
    GLFWwindow* window = glfwCreateWindow(height * 3 / 5, height, WINDOW_TITLE, nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        throw std::runtime_error("Khong tao duoc cua so");
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwFocusWindow(window);  // nhan ban phim ngay khi mo

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    applyStyle();

    float dpiScale = 1.0f;
    glfwGetWindowContentScale(window, &dpiScale, nullptr);
    const Fonts fonts = loadFonts(dpiScale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    {
        KioskApp app(options, fonts);
        const std::vector<InputStep> inputSteps = parseInputScript(options.input);
        int frameCount = 0;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            applyInputSteps(inputSteps, frameCount);
            ImGui::NewFrame();
            app.frame();
            macTextFieldsEndFrame();
            ImGui::Render();

            int width = 0;
            int bufferHeight = 0;
            glfwGetFramebufferSize(window, &width, &bufferHeight);
            glViewport(0, 0, width, bufferHeight);
            const ImVec4 clear = ImColor(COLOR_BACKGROUND);
            glClearColor(clear.x, clear.y, clear.z, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            ++frameCount;
            if (frameCount == 2) {
                macActivateApp();  // cua so da ve xong moi xin nhan ban phim
            }
            if (!options.screenshot.empty() && frameCount >= options.frames) {
                saveScreenshot(window, options.screenshot);
                break;
            }
            glfwSwapBuffers(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        // Mo bang Finder hoac lenh open thi thu muc hien tai la /, phai ve thu muc du an
        if (!fs::exists("models")) {
            fs::current_path(PROJECT_ROOT);
        }
        // Mo bang phim tat hoac bieu tuong thi chua ai bat MySQL. May chu dang chay thi lenh nay khong lam gi
        if (fs::exists("scripts/mysql.sh")) {
            std::system("bash scripts/mysql.sh start > /dev/null 2>&1");
        }
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "Loi: " << error.what() << "\n";
        return 1;
    }
}

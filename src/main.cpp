// Chuong trinh nhan dien khuon mat.
//   face_app image <anh> [--out <anh ra>] [--no-show]   nhan dien trong mot anh
//   face_app camera [--index <so>]                       nhan dien truc tiep qua webcam
//   face_app scan <ten> [--count <so>] [--index <so>]    quet mat qua webcam roi luu lai
//   (camera va scan nhan them --video <file> de dung file video thay cho webcam)
//   face_app enroll <ten> --image <anh>                  dang ky nguoi moi bang anh co san

#include "face_engine.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* WINDOW_TITLE = "Nhan dien khuon mat";
const int DEFAULT_SCAN_COUNT = 10;
const int MIN_SCAN_FACE_SIZE = 120;      // mat qua nho thi anh luu lai khong du net
const double SCAN_INTERVAL_SECONDS = 0.7;  // cho giua hai lan chup de nguoi dung kip doi goc mat

void printUsage() {
    std::cout << "Cach dung:\n"
              << "  face_app image <anh> [--out <anh ra>] [--no-show]\n"
              << "  face_app camera [--index <so> | --video <file>]\n"
              << "  face_app scan <ten> [--count <so>] [--index <so> | --video <file>]\n"
              << "  face_app enroll <ten> --image <anh>\n";
}

// Tach "--ten gia_tri" thanh map, "--no-show" co gia tri rong.
std::map<std::string, std::string> parseOptions(const std::vector<std::string>& args) {
    std::map<std::string, std::string> options;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--no-show") {
            options[args[i]] = "";
        } else if ((args[i] == "--out" || args[i] == "--index" || args[i] == "--image" ||
                    args[i] == "--count" || args[i] == "--video") &&
                   i + 1 < args.size()) {
            options[args[i]] = args[i + 1];
            ++i;
        } else {
            throw std::runtime_error("Tham so khong hop le: " + args[i]);
        }
    }
    return options;
}

int cameraIndex(const std::map<std::string, std::string>& options) {
    auto it = options.find("--index");
    return it == options.end() ? 0 : std::stoi(it->second);
}

void loadKnown(FaceEngine& engine) {
    int images = engine.loadKnownFaces(KNOWN_FACES_DIR);
    std::cout << "Da nap " << engine.knownPeopleCount() << " nguoi tu " << images << " anh\n";
}

int runImage(const std::string& path, const std::map<std::string, std::string>& options) {
    cv::Mat image = cv::imread(path);
    if (image.empty()) {
        throw std::runtime_error("Khong doc duoc anh: " + path);
    }

    FaceEngine engine = makeDefaultEngine();
    loadKnown(engine);
    std::vector<FaceResult> results = engine.process(image);

    std::cout << "Tim thay " << results.size() << " khuon mat\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const FaceResult& face = results[i];
        std::cout << "  #" << i + 1 << " " << face.name << " | vi tri " << face.box.x << ","
                  << face.box.y << " " << face.box.width << "x" << face.box.height
                  << " | tin cay " << cvRound(face.detectScore * 100) << "%"
                  << " | giong " << cvRound(face.similarity * 100) << "%"
                  << " | that " << cvRound(face.liveScore * 100) << "%\n";
    }

    drawResults(image, results);
    auto out = options.find("--out");
    if (out != options.end()) {
        if (!cv::imwrite(out->second, image)) {
            throw std::runtime_error("Khong ghi duoc anh: " + out->second);
        }
        std::cout << "Da luu ket qua: " << out->second << "\n";
    }
    if (options.count("--no-show") == 0) {
        cv::imshow(WINDOW_TITLE, image);
        cv::waitKey(0);
    }
    return 0;
}

cv::VideoCapture openCamera(const std::map<std::string, std::string>& options) {
    auto video = options.find("--video");
    if (video != options.end()) {
        cv::VideoCapture file(video->second);
        if (!file.isOpened()) {
            throw std::runtime_error("Khong mo duoc video: " + video->second);
        }
        return file;
    }

    cv::VideoCapture camera(cameraIndex(options));
    if (!camera.isOpened()) {
        throw std::runtime_error(
            "Khong mo duoc webcam. Tren macOS hay cap quyen Camera cho ung dung Terminal/VS Code"
            " trong System Settings > Privacy & Security > Camera.");
    }
    return camera;
}

int runCamera(const std::map<std::string, std::string>& options) {
    FaceEngine engine = makeDefaultEngine();
    loadKnown(engine);
    cv::VideoCapture camera = openCamera(options);

    std::cout << "Nhan q hoac ESC de thoat\n";
    cv::Mat frame;
    auto last = std::chrono::steady_clock::now();
    double fps = 0.0;

    while (camera.read(frame) && !frame.empty()) {
        // Khong lat guong: anh da dang ky la anh goc, lat se lam giam do giong
        std::vector<FaceResult> results = engine.process(frame);
        drawResults(frame, results);

        auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - last).count();
        last = now;
        fps = fps == 0.0 ? 1.0 / seconds : 0.9 * fps + 0.1 / seconds;

        const std::string status =
            "So nguoi: " + std::to_string(results.size()) + "  FPS: " + std::to_string(cvRound(fps));
        cv::putText(frame, status, cv::Point(12, 32), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
        cv::imshow(WINDOW_TITLE, frame);

        const int key = cv::waitKey(1);
        if (key == 'q' || key == 27) {
            break;
        }
    }
    return 0;
}

void checkName(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos || name[0] == '.') {
        throw std::runtime_error("Ten khong hop le: " + name);
    }
}

// Mo webcam, tu chup khi trong khung co dung mot khuon mat du lon, moi lan mot goc mat.
int runScan(const std::string& name, const std::map<std::string, std::string>& options) {
    checkName(name);
    auto countOption = options.find("--count");
    const int count = countOption == options.end() ? DEFAULT_SCAN_COUNT
                                                   : std::stoi(countOption->second);
    if (count < 1) {
        throw std::runtime_error("--count phai lon hon 0");
    }

    const std::vector<std::string> poses = {"Nhin thang", "Quay trai", "Quay phai",
                                            "Ngang len", "Cui xuong"};

    FaceEngine engine = makeDefaultEngine();
    cv::VideoCapture camera = openCamera(options);
    std::cout << "Lam theo huong dan tren man hinh, nhan q hoac ESC de dung\n";

    cv::Mat frame, display;
    int saved = 0;
    auto lastShot = std::chrono::steady_clock::now();

    while (saved < count && camera.read(frame) && !frame.empty()) {
        // Man hinh lat guong de de lam theo huong dan, nhung luu anh goc khong khung, khong chu
        cv::flip(frame, display, 1);
        std::vector<FaceResult> results = engine.process(display);

        std::string message = poses[saved % poses.size()];
        cv::Scalar color(40, 60, 230);
        if (results.empty()) {
            message = "Khong thay khuon mat";
        } else if (results.size() > 1) {
            message = "Chi de mot nguoi trong khung";
        } else if (std::min(results[0].box.width, results[0].box.height) < MIN_SCAN_FACE_SIZE) {
            message = "Lai gan camera hon";
        } else {
            color = cv::Scalar(80, 200, 60);
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - lastShot).count() >= SCAN_INTERVAL_SECONDS) {
                saveKnownImage(name, frame);
                ++saved;
                lastShot = now;
            }
        }

        for (const FaceResult& face : results) {
            cv::rectangle(display, face.box, color, 2);
        }
        const std::string status =
            "Da quet " + std::to_string(saved) + "/" + std::to_string(count) + " - " + message;
        cv::putText(display, status, cv::Point(12, 32), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
        cv::imshow(WINDOW_TITLE, display);

        const int key = cv::waitKey(1);
        if (key == 'q' || key == 27) {
            break;
        }
    }

    std::cout << "Da luu " << saved << " anh cua " << name << " vao "
              << (fs::path(KNOWN_FACES_DIR) / name).string() << "\n";
    return saved > 0 ? 0 : 1;
}

int runEnroll(const std::string& name, const std::map<std::string, std::string>& options) {
    checkName(name);
    auto source = options.find("--image");
    if (source == options.end()) {
        throw std::runtime_error("Thieu --image. Muon dung webcam thi dung lenh scan.");
    }
    cv::Mat image = cv::imread(source->second);
    if (image.empty()) {
        throw std::runtime_error("Khong doc duoc anh: " + source->second);
    }

    FaceEngine engine = makeDefaultEngine();
    const size_t faces = engine.process(image).size();
    if (faces != 1) {
        throw std::runtime_error("Anh dang ky phai co dung 1 khuon mat, nhung tim thay " +
                                 std::to_string(faces));
    }

    const fs::path target = saveKnownImage(name, image);
    std::cout << "Da dang ky " << name << ": " << target.string() << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    try {
        if (command == "image" && !args.empty()) {
            const std::string path = args[0];
            return runImage(path, parseOptions({args.begin() + 1, args.end()}));
        }
        if (command == "camera") {
            return runCamera(parseOptions(args));
        }
        if (command == "scan" && !args.empty()) {
            const std::string name = args[0];
            return runScan(name, parseOptions({args.begin() + 1, args.end()}));
        }
        if (command == "enroll" && !args.empty()) {
            const std::string name = args[0];
            return runEnroll(name, parseOptions({args.begin() + 1, args.end()}));
        }
    } catch (const std::exception& error) {
        std::cerr << "Loi: " << error.what() << "\n";
        return 1;
    }

    printUsage();
    return 1;
}

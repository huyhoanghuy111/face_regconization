#include "face_engine.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

const std::string FaceEngine::UNKNOWN_NAME = "Unknown";

FaceEngine::FaceEngine(const std::string& detectorModel, const std::string& recognizerModel,
                       float detectThreshold, double matchThreshold)
    : matchThreshold_(matchThreshold) {
    detector_ = cv::FaceDetectorYN::create(detectorModel, "", cv::Size(320, 320), detectThreshold);
    recognizer_ = cv::FaceRecognizerSF::create(recognizerModel, "");
}

void FaceEngine::loadLivenessModels(const std::string& closeModel, const std::string& wideModel) {
    livenessClose_ = cv::dnn::readNetFromONNX(closeModel);
    livenessWide_ = cv::dnn::readNetFromONNX(wideModel);
}

namespace {

// Vung cat quanh khuon mat, mo rong theo scale nhung khong vuot qua anh.
cv::Mat cropAround(const cv::Mat& image, const cv::Rect& box, double scale) {
    const double limit = std::min({(image.rows - 1.0) / box.height, (image.cols - 1.0) / box.width, scale});
    const double width = box.width * limit;
    const double height = box.height * limit;
    const double centerX = box.x + box.width / 2.0;
    const double centerY = box.y + box.height / 2.0;
    const cv::Rect area(cv::Point(cvFloor(centerX - width / 2), cvFloor(centerY - height / 2)),
                        cv::Size(cvCeil(width), cvCeil(height)));
    return image(area & cv::Rect(0, 0, image.cols, image.rows));
}

// Mo hinh tra ve 3 lop, lop so 1 la nguoi that.
float realProbability(cv::dnn::Net& net, const cv::Mat& crop) {
    net.setInput(cv::dnn::blobFromImage(crop, 1.0, cv::Size(80, 80), cv::Scalar(), false, false));
    const cv::Mat logits = net.forward();
    double highest = 0;
    cv::minMaxLoc(logits, nullptr, &highest);
    float total = 0.f;
    float real = 0.f;
    for (int i = 0; i < 3; ++i) {
        const float probability = std::exp(logits.at<float>(0, i) - static_cast<float>(highest));
        total += probability;
        if (i == 1) {
            real = probability;
        }
    }
    return real / total;
}

}  // namespace

float FaceEngine::liveScore(const cv::Mat& image, const cv::Rect& box) {
    if (livenessClose_.empty() || box.empty()) {
        return 0.f;
    }
    // image co the la vung cat cua khung hinh lon: lay lai ca khung de nhin thay vien dien thoai
    cv::Size wholeSize;
    cv::Point offset;
    image.locateROI(wholeSize, offset);
    const cv::Mat whole(wholeSize, image.type(),
                        const_cast<uchar*>(image.ptr()) - offset.y * image.step - offset.x * image.elemSize(),
                        image.step);
    const cv::Rect boxInWhole = box + offset;
    return (realProbability(livenessClose_, cropAround(whole, boxInWhole, 2.7)) +
            realProbability(livenessWide_, cropAround(whole, boxInWhole, 4.0))) / 2.f;
}

cv::Mat FaceEngine::detect(const cv::Mat& image) {
    detector_->setInputSize(image.size());
    cv::Mat faces;
    detector_->detect(image, faces);
    return faces;  // moi hang la mot khuon mat
}

cv::Mat FaceEngine::extractFeature(const cv::Mat& image, const cv::Mat& faceRow) {
    cv::Mat aligned, feature;
    recognizer_->alignCrop(image, faceRow, aligned);
    recognizer_->feature(aligned, feature);
    return feature.clone();
}

int FaceEngine::loadKnownFaces(const std::string& directory) {
    known_.clear();
    if (!fs::is_directory(directory)) {
        return 0;
    }

    int used = 0;
    for (const auto& person : fs::directory_iterator(directory)) {
        if (!person.is_directory()) {
            continue;
        }
        const std::string name = person.path().filename().string();
        for (const auto& file : fs::directory_iterator(person.path())) {
            if (!file.is_regular_file() || file.path().filename().string()[0] == '.') {
                continue;
            }
            cv::Mat image = cv::imread(file.path().string());
            if (image.empty()) {
                std::cerr << "Bo qua (khong doc duoc anh): " << file.path() << "\n";
                continue;
            }
            cv::Mat faces = detect(image);
            if (faces.rows != 1) {
                // Anh dang ky phai ro mot nguoi, neu khong se khong biet khuon mat nao la cua ai
                std::cerr << "Bo qua (can dung 1 khuon mat, thay " << faces.rows
                          << "): " << file.path() << "\n";
                continue;
            }
            known_.push_back({name, extractFeature(image, faces.row(0))});
            ++used;
        }
    }
    return used;
}

std::vector<FaceResult> FaceEngine::process(const cv::Mat& image) {
    std::vector<FaceResult> results;
    cv::Mat faces = detect(image);

    for (int i = 0; i < faces.rows; ++i) {
        FaceResult result;
        const float x = faces.at<float>(i, 0);
        const float y = faces.at<float>(i, 1);
        const float w = faces.at<float>(i, 2);
        const float h = faces.at<float>(i, 3);
        result.box = cv::Rect(cv::Point(cvRound(x), cvRound(y)), cv::Size(cvRound(w), cvRound(h))) &
                     cv::Rect(0, 0, image.cols, image.rows);
        result.detectScore = faces.at<float>(i, 14);
        result.name = UNKNOWN_NAME;
        result.liveScore = liveScore(image, result.box);

        if (!known_.empty()) {
            cv::Mat feature = extractFeature(image, faces.row(i));
            double best = -1.0;
            const KnownFace* bestFace = nullptr;
            for (const KnownFace& candidate : known_) {
                double score = recognizer_->match(feature, candidate.feature,
                                                  cv::FaceRecognizerSF::FR_COSINE);
                if (score > best) {
                    best = score;
                    bestFace = &candidate;
                }
            }
            result.similarity = best;
            if (best >= matchThreshold_) {
                result.name = bestFace->name;
            }
        }
        results.push_back(result);
    }
    return results;
}

int FaceEngine::knownPeopleCount() const {
    std::set<std::string> names;
    for (const KnownFace& face : known_) {
        names.insert(face.name);
    }
    return static_cast<int>(names.size());
}

FaceEngine makeDefaultEngine() {
    const std::string detectorModel = "models/face_detection_yunet_2023mar.onnx";
    const std::string recognizerModel = "models/face_recognition_sface_2021dec.onnx";
    const std::string livenessCloseModel = "models/MiniFASNetV2.onnx";
    const std::string livenessWideModel = "models/MiniFASNetV1SE.onnx";
    const float detectThreshold = 0.8f;
    const double matchThreshold = 0.363;  // nguong cosine do tac gia mo hinh SFace de xuat

    for (const std::string& model : {detectorModel, recognizerModel, livenessCloseModel, livenessWideModel}) {
        if (!fs::exists(model)) {
            throw std::runtime_error("Thieu mo hinh " + model +
                                     ". Hay chay chuong trinh tu thu muc goc cua du an"
                                     " va chay scripts/setup_deps.sh truoc.");
        }
    }
    FaceEngine engine(detectorModel, recognizerModel, detectThreshold, matchThreshold);
    engine.loadLivenessModels(livenessCloseModel, livenessWideModel);
    return engine;
}

fs::path saveKnownImage(const std::string& name, const cv::Mat& image) {
    const fs::path folder = fs::path(KNOWN_FACES_DIR) / name;
    fs::create_directories(folder);
    int number = 1;
    fs::path target;
    do {
        target = folder / (std::to_string(number++) + ".jpg");
    } while (fs::exists(target));

    if (!cv::imwrite(target.string(), image)) {
        throw std::runtime_error("Khong ghi duoc anh: " + target.string());
    }
    return target;
}

void drawResults(cv::Mat& image, const std::vector<FaceResult>& results) {
    // Co net va chu theo kich thuoc anh de anh lon van nhin ro
    const double scale = std::max(0.5, std::min(image.cols, image.rows) / 800.0);
    const int thickness = std::max(1, cvRound(2 * scale));

    for (const FaceResult& face : results) {
        const bool known = face.name != FaceEngine::UNKNOWN_NAME;
        const cv::Scalar color = known ? cv::Scalar(80, 200, 60) : cv::Scalar(40, 60, 230);
        cv::rectangle(image, face.box, color, thickness);

        std::string label = face.name;
        if (known) {
            label += " " + std::to_string(cvRound(face.similarity * 100)) + "%";
        }
        int baseline = 0;
        cv::Size textSize =
            cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.6 * scale, thickness, &baseline);
        const int top = std::max(textSize.height + baseline, face.box.y);
        // Day nhan vao trong anh neu khuon mat nam sat mep phai
        const int left = std::max(0, std::min(face.box.x, image.cols - textSize.width));
        cv::rectangle(image, cv::Point(left, top - textSize.height - baseline),
                      cv::Point(left + textSize.width, top), color, cv::FILLED);
        cv::putText(image, label, cv::Point(left, top - baseline), cv::FONT_HERSHEY_SIMPLEX,
                    0.6 * scale, cv::Scalar(255, 255, 255), thickness, cv::LINE_AA);
    }
}

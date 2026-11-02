#pragma once

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/objdetect/face.hpp>

#include <filesystem>
#include <string>
#include <vector>

inline const std::string KNOWN_FACES_DIR = "known_faces";

// Ket qua cho mot khuon mat tim thay trong anh.
struct FaceResult {
    cv::Rect box;
    float detectScore = 0.f;  // do tin cay cua buoc tim khuon mat (0..1)
    std::string name;         // ten nguoi, hoac UNKNOWN_NAME neu khong khop ai
    double similarity = 0.0;  // do giong voi nguoi gan nhat (cosine, -1..1)
    float liveScore = 0.f;    // kha nang la nguoi that, khong phai anh tren giay/dien thoai (0..1)
};

class FaceEngine {
public:
    static const std::string UNKNOWN_NAME;

    FaceEngine(const std::string& detectorModel, const std::string& recognizerModel,
               float detectThreshold, double matchThreshold);

    // Nap hai mo hinh chong gia mao MiniFASNet, moi mo hinh nhin mot vung quanh mat khac nhau.
    void loadLivenessModels(const std::string& closeModel, const std::string& wideModel);

    // Doc thu muc dang known_faces/<ten nguoi>/<anh>. Tra ve so anh dung duoc.
    int loadKnownFaces(const std::string& directory);

    // Tim tat ca khuon mat trong anh va gan ten cho tung khuon mat.
    std::vector<FaceResult> process(const cv::Mat& image);

    int knownPeopleCount() const;

private:
    struct KnownFace {
        std::string name;
        cv::Mat feature;
    };

    cv::Mat detect(const cv::Mat& image);
    cv::Mat extractFeature(const cv::Mat& image, const cv::Mat& faceRow);
    float liveScore(const cv::Mat& image, const cv::Rect& box);

    cv::Ptr<cv::FaceDetectorYN> detector_;
    cv::Ptr<cv::FaceRecognizerSF> recognizer_;
    cv::dnn::Net livenessClose_;
    cv::dnn::Net livenessWide_;
    std::vector<KnownFace> known_;
    double matchThreshold_;
};

// Tao engine voi hai mo hinh trong models/. Bao loi neu thieu file.
FaceEngine makeDefaultEngine();

// Luu anh vao known_faces/<ten>/ voi so thu tu tiep theo.
std::filesystem::path saveKnownImage(const std::string& name, const cv::Mat& image);

// Ve khung va ten len anh.
void drawResults(cv::Mat& image, const std::vector<FaceResult>& results);

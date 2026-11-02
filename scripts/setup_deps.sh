#!/usr/bin/env bash
# Tai 2 mo hinh va build OpenCV vao thu muc deps/ (khong can sudo, khong can brew).
# Chi can chay mot lan. Lan dau mat khoang 10-20 phut.
set -euo pipefail

OPENCV_VERSION=4.13.0
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/deps"
PREFIX="$DEPS/opencv"
ZOO=https://github.com/opencv/opencv_zoo/raw/main/models

mkdir -p "$DEPS" "$ROOT/models"

download_model() {
    local target="$ROOT/models/$(basename "$1")"
    [ -s "$target" ] || curl -fL --retry 3 -o "$target" "$ZOO/$1"
}
download_model face_detection_yunet/face_detection_yunet_2023mar.onnx
download_model face_recognition_sface/face_recognition_sface_2021dec.onnx

# Mo hinh chong gia mao bang anh (MiniFASNet, Apache-2.0)
for model in MiniFASNetV2 MiniFASNetV1SE; do
    [ -s "$ROOT/models/$model.onnx" ] || curl -fL --retry 3 -o "$ROOT/models/$model.onnx" \
        "https://github.com/yakhyo/face-anti-spoofing/releases/download/weights/$model.onnx"
done

# Thu vien giao dien cho face_kiosk, duoc build chung voi chuong trinh
clone_source() {
    [ -d "$DEPS/$3" ] || git clone --depth 1 --branch "$2" "$1" "$DEPS/$3"
}
clone_source https://github.com/ocornut/imgui.git v1.91.9 imgui
clone_source https://github.com/glfw/glfw.git 3.4 glfw

if [ -f "$PREFIX/lib/cmake/opencv4/OpenCVConfig.cmake" ]; then
    echo "OpenCV da co san tai $PREFIX"
else
    # cmake va ninja lay qua pip de khong phai cai vao he thong
    python3 -m venv "$DEPS/venv"
    "$DEPS/venv/bin/pip" install --quiet cmake ninja

    if [ ! -d "$DEPS/opencv-src" ]; then
        git clone --depth 1 --branch "$OPENCV_VERSION" \
            https://github.com/opencv/opencv.git "$DEPS/opencv-src"
    fi

    # Chi build cac module chuong trinh can de giam thoi gian build
    PATH="$DEPS/venv/bin:$PATH" cmake -G Ninja \
        -S "$DEPS/opencv-src" -B "$DEPS/opencv-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_LIST=core,imgproc,imgcodecs,videoio,highgui,dnn,objdetect,calib3d,features2d,flann \
        -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
        -DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python3=OFF \
        -DWITH_FFMPEG=OFF -DWITH_OPENEXR=OFF -DWITH_IPP=OFF -DWITH_ITT=OFF \
        -DWITH_ADE=OFF

    PATH="$DEPS/venv/bin:$PATH" cmake --build "$DEPS/opencv-build" --target install
fi

echo "Xong. Build chuong trinh bang:"
echo "  $DEPS/venv/bin/cmake -S . -B build -DOpenCV_DIR=$PREFIX/lib/cmake/opencv4"
echo "  $DEPS/venv/bin/cmake --build build"

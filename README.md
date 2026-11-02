# Chương trình nhận diện khuôn mặt (C++ / OpenCV)

Chương trình làm được:

- Tìm khuôn mặt người trong ảnh.
- Tìm khuôn mặt trực tiếp qua webcam.
- Xử lý nhiều người trong cùng một khung hình.
- Quét mặt qua webcam rồi lưu lại để lần sau nhận ra.
- Nhận ra tên từng người đã đăng ký; người lạ hiện là `Unknown`.

## Máy chấm công có giao diện

```bash
open build/face_kiosk.app
```

Ứng dụng tự bật MySQL khi mở. Lần đầu macOS sẽ hỏi quyền Camera, hãy chọn cho phép.

Có thể mở nhanh bằng phím tắt **Control + Option + K** ở bất kỳ đâu, hoặc biểu tượng **Máy chấm công** trên Desktop:

```bash
bash scripts/hotkey.sh install     # cài phím tắt và biểu tượng
bash scripts/hotkey.sh uninstall   # gỡ
```

Phím tắt do một tiến trình nền rất nhỏ (`build/face_hotkey`) xử lý, tự chạy khi đăng nhập.

- Màn hình chính hiện camera, ngày giờ, trạng thái và thông tin người vừa quét.
- **Thêm người:** nhập mã nhân viên, họ tên, phòng ban rồi quét 10 ảnh khuôn mặt. Mã được điền sẵn và sửa được, không được trùng.
- **Quét lại:** xóa kết quả đang hiện và nhận diện lại.
- **Lịch sử chấm công:** xem theo ngày hoặc tất cả. Nút **+ Ghi thủ công** cho phép nhập tay một lượt chấm công.
- **Quản lý nhân viên:** sửa thông tin hoặc xóa nhân viên.

Quy tắc chấm công: sau khi thấy cùng một người 8 khung hình liên tiếp, chương trình hỏi xác nhận. Bấm **Xác nhận chấm công** hoặc nhấn Enter để ghi, bấm **Hủy** hoặc nhấn Esc để bỏ qua. Lần đầu trong ngày là **Vào**, các lần sau là **Ra**. Trong 60 giây sau một lần chấm công thì không ghi trùng. Nhiều người trong khung được hỏi lần lượt.

Chống giả mạo: hai mô hình MiniFASNet (Apache-2.0) chấm điểm mỗi khuôn mặt có phải người thật không. Khuôn mặt dưới ngưỡng 50% là ảnh trên điện thoại hoặc giấy: màn hình báo đỏ, không hiện tên, không chấm công và không đăng ký được. Mô hình không tuyệt đối, nên độ chính xác phụ thuộc vào camera, ánh sáng và chất lượng màn hình điện thoại.

Nhân viên và lịch sử chấm công lưu trong MySQL, cơ sở dữ liệu `face_attendance` (bảng `employees` và `attendance`). Ảnh khuôn mặt lưu trong `known_faces/<mã nhân viên>/`.

Máy chủ MySQL nằm trong dự án và chỉ nhận kết nối từ chính máy này. Mật khẩu được tạo ngẫu nhiên ở lần cài đầu tiên và lưu trong `data/db.conf`. Phải bật máy chủ trước khi mở `face_kiosk`:

```bash
bash scripts/mysql.sh start    # lần đầu sẽ tải và cài MySQL
bash scripts/mysql.sh stop
bash scripts/mysql.sh shell    # mở dòng lệnh SQL
```

Sau khi cài xong lần đầu, cần chạy lại lệnh biên dịch ở trên để liên kết với thư viện MySQL.

Ví dụ câu lệnh SQL:

```sql
SELECT * FROM employees;
SELECT FROM_UNIXTIME(checked_at) AS thoi_gian, code, name, type FROM attendance ORDER BY id DESC;
```

Kiểm thử logic chấm công: `./build/attendance_test`

## Cách hoạt động

1. **Tìm khuôn mặt:** mô hình YuNet (`cv::FaceDetectorYN`) trả về vị trí mọi khuôn mặt trong ảnh.
2. **Trích đặc trưng:** mô hình SFace (`cv::FaceRecognizerSF`) căn thẳng khuôn mặt rồi chuyển thành một vector số.
3. **So khớp:** so vector đó với từng ảnh trong `known_faces/` bằng độ tương đồng cosine. Lấy người giống nhất; nếu điểm dưới ngưỡng 0.363 thì coi là `Unknown`.

## Cài đặt

Cần có trình biên dịch C++ và Python 3. Chỉ cần chạy một lần (khoảng 10–20 phút):

```bash
bash scripts/setup_deps.sh
```

Script tải hai mô hình vào `models/` và build OpenCV vào `deps/`, không cài gì vào hệ thống.

## Biên dịch

```bash
deps/venv/bin/cmake -S . -B build -DOpenCV_DIR="$PWD/deps/opencv/lib/cmake/opencv4"
deps/venv/bin/cmake --build build
```

## Sử dụng

Chạy từ thư mục gốc của dự án.

```bash
# Quét mặt qua webcam rồi lưu: chương trình tự chụp 10 ảnh, làm theo hướng dẫn trên màn hình
./build/face_app scan Huy

# Quét nhiều ảnh hơn
./build/face_app scan Huy --count 20

# Đăng ký người mới bằng ảnh có sẵn (ảnh phải có đúng 1 khuôn mặt)
./build/face_app enroll Huy --image anh_cua_huy.jpg

# Nhận diện trong ảnh, hiện cửa sổ kết quả và lưu ra file
./build/face_app image anh_nhom.jpg --out ket_qua.jpg

# Chỉ in kết quả, không mở cửa sổ
./build/face_app image anh_nhom.jpg --out ket_qua.jpg --no-show

# Nhận diện qua webcam: nhấn q hoặc ESC để thoát
./build/face_app camera
```

Dùng `--index 1` nếu máy có nhiều camera, hoặc `--video <file>` để dùng file video thay cho webcam (với `camera` và `scan`). Đăng ký vài ảnh cho mỗi người (góc mặt, ánh sáng khác nhau) sẽ nhận ra chính xác hơn. Tên không dấu sẽ hiển thị đúng, vì chữ vẽ lên ảnh không hỗ trợ tiếng Việt có dấu.

Trên macOS, lần đầu dùng webcam cần cấp quyền trong **System Settings > Privacy & Security > Camera**.

## Cấu trúc thư mục

```
src/kiosk_main.cpp    giao diện máy chấm công
src/attendance.*      nhân viên, sổ chấm công, xác nhận khuôn mặt ổn định
src/main.cpp          chương trình dòng lệnh: ảnh, webcam, đăng ký
src/face_engine.*     tìm khuôn mặt, nhận diện, vẽ kết quả
tests/                kiểm thử logic chấm công
scripts/setup_deps.sh tải mô hình, thư viện giao diện và build OpenCV
scripts/mysql.sh      cài, bật, tắt máy chủ MySQL
known_faces/<tên>/    ảnh của từng người đã đăng ký
models/               hai file mô hình .onnx
```

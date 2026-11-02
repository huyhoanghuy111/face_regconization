#pragma once

#include <cstddef>

// Dua ung dung ra phia truoc de cua so nhan ban phim.
void macActivateApp();

// O nhap goc cua macOS dat de len cua so: go duoc Telex, dan bang Cmd+V.
// Goi moi khung hinh khi can hien o, noi dung duoc chep vao buffer.
void macTextField(const char* id, float x, float y, float width, float height, char* buffer,
                  size_t bufferSize, bool focus);
// Goi cuoi moi khung hinh, an nhung o khong duoc goi trong khung hinh do.
void macTextFieldsEndFrame();
void macSetTextField(const char* id, const char* text);

// Tao phim ngay trong ung dung de kiem thu duong nhap lieu cua macOS.
void macPrintInputState();
void macSendKey(char character);
void macSelectInputSource(const char* sourceId);

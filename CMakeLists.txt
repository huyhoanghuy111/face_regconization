cmake_minimum_required(VERSION 3.16)
project(FaceRecognition C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(OpenCV 4.8 REQUIRED COMPONENTS core imgproc imgcodecs videoio highgui dnn objdetect)
find_package(OpenGL REQUIRED)

# Thu vien ket noi MySQL di kem may chu do scripts/mysql.sh cai vao deps/mysql
set(MYSQL_DIR ${CMAKE_SOURCE_DIR}/deps/mysql)
find_library(MYSQL_CLIENT mysqlclient PATHS ${MYSQL_DIR}/lib NO_DEFAULT_PATH REQUIRED)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw EXCLUDE_FROM_ALL)

set(IMGUI_DIR deps/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# Chuong trinh dong lenh
add_executable(face_app src/main.cpp src/face_engine.cpp)
target_link_libraries(face_app PRIVATE ${OpenCV_LIBS})
target_include_directories(face_app PRIVATE ${OpenCV_INCLUDE_DIRS})
target_compile_options(face_app PRIVATE -Wall -Wextra)

# May cham cong co giao dien
add_executable(face_kiosk MACOSX_BUNDLE src/kiosk_main.cpp src/mac_input.mm src/face_engine.cpp src/attendance.cpp)
target_link_libraries(face_kiosk PRIVATE ${OpenCV_LIBS} imgui ${MYSQL_CLIENT}
    "-framework AppKit" "-framework Carbon")
target_include_directories(face_kiosk PRIVATE ${OpenCV_INCLUDE_DIRS} ${MYSQL_DIR}/include)
set_target_properties(face_kiosk PROPERTIES
    BUILD_RPATH ${MYSQL_DIR}/lib
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist)
target_compile_definitions(face_kiosk PRIVATE GL_SILENCE_DEPRECATION PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_options(face_kiosk PRIVATE -Wall -Wextra)
if(APPLE)
    # Ky lai ca goi .app de macOS doc duoc mo ta quyen Camera trong Info.plist
    add_custom_command(TARGET face_kiosk POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:face_kiosk>)
endif()

# Phim tat toan he thong de mo may cham cong
add_executable(face_hotkey src/hotkey_agent.mm)
target_compile_definitions(face_hotkey PRIVATE KIOSK_APP_PATH="$<TARGET_BUNDLE_DIR:face_kiosk>")
target_link_libraries(face_hotkey PRIVATE "-framework AppKit" "-framework Carbon")

add_executable(attendance_test tests/attendance_test.cpp src/attendance.cpp)
target_include_directories(attendance_test PRIVATE src ${MYSQL_DIR}/include)
target_link_libraries(attendance_test PRIVATE ${MYSQL_CLIENT})
set_target_properties(attendance_test PROPERTIES BUILD_RPATH ${MYSQL_DIR}/lib)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(unofficial-libmupdf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(CURL REQUIRED)

# --- util -------------------------------------------------------------------
add_library(pdfread_util STATIC
    util/log.cpp
    util/sha256.cpp
    util/thread_name.cpp
    util/thread_pool.cpp
)
target_include_directories(pdfread_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pdfread_util
    PUBLIC Threads::Threads
    PRIVATE pdfread_options
)

# --- text: tầng 2 — làm sạch, thứ tự đọc, đoạn, câu (không phụ thuộc MuPDF) --
add_library(pdfread_text STATIC
    text/normalize.cpp
    text/page_text.cpp
    text/protect.cpp
    text/reading_order.cpp
    text/search.cpp
    text/sentence_splitter.cpp
)
target_link_libraries(pdfread_text
    PUBLIC pdfread_util
    PRIVATE pdfread_options
)

# --- terminology: tầng 1 — lĩnh vực, glossary, trích thuật ngữ ------------------
add_library(pdfread_terminology STATIC
    terminology/classifier.cpp
    terminology/domains.cpp
    terminology/glossary.cpp
    terminology/glossary_store.cpp
    terminology/term_extractor.cpp
    terminology/words.cpp
)
target_link_libraries(pdfread_terminology
    PUBLIC pdfread_text
    PRIVATE pdfread_options nlohmann_json::nlohmann_json
)
# DomainScore nằm trong translate/translator.hpp (chỉ header).
target_include_directories(pdfread_terminology PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- pdf: lớp bọc MuPDF -----------------------------------------------------
add_library(pdfread_pdf STATIC
    pdf/context.cpp
    pdf/document.cpp
    pdf/render_service.cpp
    pdf/search_service.cpp
    pdf/text_page.cpp
    pdf/text_service.cpp
)
target_link_libraries(pdfread_pdf
    PUBLIC pdfread_util pdfread_text
    PRIVATE pdfread_options unofficial::libmupdf::libmupdf
)

# --- translate: interface Translator, backend llama.cpp, luồng suy luận ------
add_library(pdfread_translate STATIC
    translate/backend.cpp
    translate/llama_translator.cpp
    translate/prompt.cpp
    translate/translation_service.cpp
)
target_link_libraries(pdfread_translate
    PUBLIC pdfread_util
    PRIVATE pdfread_options llama ggml
)


# --- storage: SQLite (cài đặt, cache bản dịch, thông tin tài liệu) ------------
add_library(pdfread_storage STATIC
    storage/database.cpp
    storage/store.cpp
)
target_link_libraries(pdfread_storage
    PUBLIC pdfread_translate
    PRIVATE pdfread_options unofficial::sqlite3::sqlite3
)

# --- download: tải mô hình lần đầu (libcurl) -----------------------------------
add_library(pdfread_download STATIC
    download/downloader.cpp
)
target_link_libraries(pdfread_download
    PUBLIC pdfread_util
    PRIVATE pdfread_options CURL::libcurl
)

# --- context: tầng 3 — vùng chọn -> kế hoạch dịch ----------------------------
add_library(pdfread_context STATIC
    context/translation_plan.cpp
)
target_link_libraries(pdfread_context
    PUBLIC pdfread_text pdfread_translate pdfread_terminology
    PRIVATE pdfread_options
)

# --- ui: viewer, layout, texture cache, font, popup dịch --------------------
add_library(pdfread_ui STATIC
    ui/fonts.cpp
    ui/glossary_window.cpp
    ui/model_window.cpp
    ui/page_layout.cpp
    ui/selection.cpp
    ui/settings_window.cpp
    ui/texture_cache.cpp
    ui/theme.cpp
    ui/translation_popup.cpp
    ui/viewer.cpp
)
target_link_libraries(pdfread_ui
    PUBLIC pdfread_pdf pdfread_translate pdfread_context pdfread_storage pdfread_download imgui::imgui glfw OpenGL::GL nfd::nfd
    PRIVATE pdfread_options
)

# --- công cụ dịch thử từ dòng lệnh -------------------------------------------
add_executable(pdfread_translate_cli ${PROJECT_SOURCE_DIR}/tools/translate_cli/main.cpp)
set_target_properties(pdfread_translate_cli PROPERTIES OUTPUT_NAME pdfread-translate)
target_link_libraries(pdfread_translate_cli PRIVATE pdfread_translate pdfread_options)

# --- chương trình đánh giá chất lượng dịch (tools/eval) -------------------------
add_executable(pdfread_eval ${PROJECT_SOURCE_DIR}/tools/eval/main.cpp)
set_target_properties(pdfread_eval PROPERTIES OUTPUT_NAME pdfread-eval)
target_link_libraries(pdfread_eval PRIVATE pdfread_translate pdfread_terminology pdfread_options)

# --- app --------------------------------------------------------------------
add_executable(pdfread
    app/app.cpp
    app/app_reader.cpp
    app/main.cpp
)
target_compile_definitions(pdfread PRIVATE
    PDFREAD_VERSION="${PROJECT_VERSION}"
    PDFREAD_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
)
target_link_libraries(pdfread PRIVATE pdfread_ui nfd::nfd pdfread_options)
# Tên hiển thị của app: HUSTpdf (file chạy HUSTpdf.exe); tên target nội bộ vẫn là pdfread.
set_target_properties(pdfread PROPERTIES OUTPUT_NAME HUSTpdf)
if(WIN32)
    # Icon (res/HUSTpdf.ico) + thông tin phiên bản của file exe.
    configure_file(app/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(pdfread PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/app.rc APPEND PROPERTY OBJECT_DEPENDS
                 ${PROJECT_SOURCE_DIR}/res/HUSTpdf.ico)
endif()

# Release: ứng dụng GUI (không mở cửa sổ console). Debug giữ console để xem log.
if(WIN32)
    set_target_properties(pdfread PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    if(MSVC)
        target_link_options(pdfread PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()

# Chép data/ (font, domains.json, glossary) cạnh file chạy.
add_custom_command(TARGET pdfread POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:pdfread>/data"
    VERBATIM
)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(unofficial-libmupdf CONFIG REQUIRED)

# --- util -------------------------------------------------------------------
add_library(pdfread_util STATIC
    util/log.cpp
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
    text/reading_order.cpp
    text/sentence_splitter.cpp
)
target_link_libraries(pdfread_text
    PUBLIC pdfread_util
    PRIVATE pdfread_options
)

# --- pdf: lớp bọc MuPDF -----------------------------------------------------
add_library(pdfread_pdf STATIC
    pdf/context.cpp
    pdf/document.cpp
    pdf/render_service.cpp
    pdf/text_page.cpp
    pdf/text_service.cpp
)
target_link_libraries(pdfread_pdf
    PUBLIC pdfread_util pdfread_text
    PRIVATE pdfread_options unofficial::libmupdf::libmupdf
)

# --- translate: interface Translator, backend llama.cpp, luồng suy luận ------
add_library(pdfread_translate STATIC
    translate/llama_translator.cpp
    translate/prompt.cpp
    translate/translation_service.cpp
)
target_link_libraries(pdfread_translate
    PUBLIC pdfread_util
    PRIVATE pdfread_options llama ggml
)

# --- context: tầng 3 — vùng chọn -> kế hoạch dịch ----------------------------
add_library(pdfread_context STATIC
    context/translation_plan.cpp
)
target_link_libraries(pdfread_context
    PUBLIC pdfread_text pdfread_translate
    PRIVATE pdfread_options
)

# --- ui: viewer, layout, texture cache, font, popup dịch --------------------
add_library(pdfread_ui STATIC
    ui/fonts.cpp
    ui/page_layout.cpp
    ui/selection.cpp
    ui/texture_cache.cpp
    ui/translation_popup.cpp
    ui/viewer.cpp
)
target_link_libraries(pdfread_ui
    PUBLIC pdfread_pdf pdfread_translate pdfread_context imgui::imgui glfw OpenGL::GL
    PRIVATE pdfread_options
)

# --- công cụ dịch thử từ dòng lệnh -------------------------------------------
add_executable(pdfread_translate_cli ${PROJECT_SOURCE_DIR}/tools/translate_cli/main.cpp)
set_target_properties(pdfread_translate_cli PROPERTIES OUTPUT_NAME pdfread-translate)
target_link_libraries(pdfread_translate_cli PRIVATE pdfread_translate pdfread_options)

# --- app --------------------------------------------------------------------
add_executable(pdfread
    app/app.cpp
    app/main.cpp
)
target_compile_definitions(pdfread PRIVATE
    PDFREAD_VERSION="${PROJECT_VERSION}"
    PDFREAD_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
)
target_link_libraries(pdfread PRIVATE pdfread_ui nfd::nfd pdfread_options)

# Release: ứng dụng GUI (không mở cửa sổ console). Debug giữ console để xem log.
if(WIN32)
    set_target_properties(pdfread PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    if(MSVC)
        target_link_options(pdfread PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()

# Chép font (và dữ liệu khác) cạnh file chạy.
add_custom_command(TARGET pdfread POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/data/fonts" "$<TARGET_FILE_DIR:pdfread>/data/fonts"
    VERBATIM
)

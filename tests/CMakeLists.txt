find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# Tạo PDF mẫu (dùng chung cho test và công cụ make_sample_pdf).
add_library(pdfread_test_support STATIC support/sample_pdf.cpp)
target_include_directories(pdfread_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pdfread_test_support PRIVATE pdfread_options)

add_executable(pdfread_tests
    util/test_concurrent_queue.cpp
    util/test_latest_slot.cpp
    util/test_thread_pool.cpp
    util/test_log_timer.cpp
    util/test_sha256.cpp
    storage/test_store.cpp
    download/test_downloader.cpp
    ui/test_page_layout.cpp
    ui/test_view_transform.cpp
    pdf/test_tiles.cpp
    pdf/test_document.cpp
    pdf/test_text_page.cpp
    translate/test_prompt.cpp
    translate/test_translation_service.cpp
    text/test_sentence_splitter.cpp
    text/test_page_text.cpp
    text/test_search.cpp
    pdf/test_navigation.cpp
    context/test_translation_plan.cpp
    text/test_protect.cpp
    terminology/test_glossary.cpp
    terminology/test_classifier.cpp
    terminology/test_glossary_store.cpp
    terminology/test_data_files.cpp
)
target_link_libraries(pdfread_tests PRIVATE
    pdfread_ui pdfread_test_support pdfread_options Catch2::Catch2WithMain)

target_compile_definitions(pdfread_tests PRIVATE PDFREAD_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

catch_discover_tests(pdfread_tests)

add_executable(make_sample_pdf ${PROJECT_SOURCE_DIR}/tools/sample_pdf/main.cpp)
target_link_libraries(make_sample_pdf PRIVATE pdfread_test_support pdfread_options)

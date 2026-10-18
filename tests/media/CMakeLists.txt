lumenis_add_test(tst_media SOURCES tst_media.cpp LIBS lumenis::media)
target_compile_definitions(tst_media PRIVATE LUMENIS_PATENT_CODECS=$<BOOL:${LUMENIS_PATENT_CODECS}>)
if(WIN32)
    target_link_libraries(tst_media PRIVATE ole32)  # soundLeavesComAlone
endif()

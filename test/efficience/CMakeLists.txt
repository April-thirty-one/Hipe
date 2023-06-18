aux_source_directory(. TEST_EFFICIENCE_SRC_LIST)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin/efficience)

foreach(TEST_SRC ${TEST_EFFICIENCE_SRC_LIST})
    STRING(REGEX REPLACE "^\\./|\\.c[a-zA-Z0-9_]*$" "" TEST_EXE_NAME ${TEST_SRC})
    message("=== 添加测试程序:${TEST_EXE_NAME} ===")
    add_executable(${TEST_EXE_NAME} ${TEST_SRC})
endforeach(TEST_SRC ${TEST_EFFICIENCE_SRC_LIST})
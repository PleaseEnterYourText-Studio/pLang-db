cmake_minimum_required(VERSION 3.25)
project(pdb LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------
# 定位 LLVM/LLDB 前缀（与 pLang 仓库同策略：llvm-config → 候选路径）
# ------------------------------------------------------------------
find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config llvm-config-22 llvm-config-21 llvm-config-20
          llvm-config-19 llvm-config-18 llvm-config-17 llvm-config-16
    PATHS
        "D:/llvm/bin"
        "C:/Program Files/LLVM/bin"
        "C:/LLVM/bin"
        "$ENV{LLVM_HOME}/bin"
)

if(LLVM_CONFIG_EXECUTABLE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --prefix
        OUTPUT_VARIABLE LLVM_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
        OUTPUT_VARIABLE LLVM_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "LLVM/LLDB prefix: ${LLVM_PREFIX} (version ${LLVM_VERSION})")
endif()

if(NOT LLVM_PREFIX)
    set(LLVM_CANDIDATE_PREFIXES
        # macOS (Homebrew)
        "/usr/local/opt/llvm"
        "/opt/homebrew/opt/llvm"
        # Linux
        "/usr/lib/llvm-22" "/usr/lib/llvm-21" "/usr/lib/llvm-20"
        "/usr/lib/llvm-19" "/usr/lib/llvm-18" "/usr/lib/llvm-17"
        "/usr/lib/llvm-16"
        # Windows
        "D:/llvm" "C:/Program Files/LLVM" "C:/LLVM"
        # 环境变量
        "$ENV{LLVM_HOME}"
    )
    foreach(candidate ${LLVM_CANDIDATE_PREFIXES})
        if(EXISTS "${candidate}/include/lldb/API/LLDB.h")
            set(LLVM_PREFIX "${candidate}")
            message(STATUS "LLVM/LLDB prefix: ${LLVM_PREFIX}")
            break()
        endif()
    endforeach()
endif()

if(NOT LLVM_PREFIX OR NOT EXISTS "${LLVM_PREFIX}/include/lldb/API/LLDB.h")
    message(FATAL_ERROR "
        ================================================================
        LLDB not found! pdb needs the LLDB C++ API headers.
        Install LLVM (which bundles LLDB) or set LLVM_HOME / LLVM_PREFIX.
        ================================================================
    ")
endif()

set(LLDB_INCLUDE_DIR "${LLVM_PREFIX}/include")
set(LLDB_LIBRARY_DIR "${LLVM_PREFIX}/lib")

# liblldb 库名：macOS/Linux 为 lldb，Windows 为 lldb.lib
find_library(LLDB_LIBRARY
    NAMES lldb liblldb lldb.lib
    PATHS "${LLDB_LIBRARY_DIR}" "${LLVM_PREFIX}/lib"
    NO_DEFAULT_PATH
)

if(NOT LLDB_LIBRARY)
    message(FATAL_ERROR "liblldb not found under ${LLDB_LIBRARY_DIR}")
endif()
message(STATUS "   LLDB library: ${LLDB_LIBRARY}")

# ------------------------------------------------------------------
# pdb 可执行文件
# ------------------------------------------------------------------
add_executable(pdb
    src/main.cpp
)

target_include_directories(pdb PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLDB_INCLUDE_DIR}
)

target_link_libraries(pdb PRIVATE ${LLDB_LIBRARY})

# macOS：liblldb 的依赖（curses/z/xml2 等）由 dylib 内部解析，这里补 rpath
if(APPLE)
    set_target_properties(pdb PROPERTIES
        BUILD_RPATH "${LLDB_LIBRARY_DIR}"
        INSTALL_RPATH "${LLDB_LIBRARY_DIR}"
    )
elseif(UNIX)
    set_target_properties(pdb PROPERTIES
        BUILD_RPATH "${LLDB_LIBRARY_DIR}"
    )
endif()

message(STATUS "pdb configured with LLDB at ${LLVM_PREFIX}")

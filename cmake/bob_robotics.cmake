cmake_minimum_required(VERSION 3.1)

# Build a module from sources in current folder. All *.cc files are compiled
# into a static library.
macro(BoB_module)
    BoB_module_custom(${ARGN})
    BoB_build()
endmacro()

# Build a "project" in the current folder (e.g. example, etc.). Each *.cc file
# found is compiled into a separate executable.
macro(BoB_project)
    BoB_init()

    include(CMakeParseArguments)
    cmake_parse_arguments(PARSED_ARGS
                          "GENN_CPU_ONLY"
                          "EXECUTABLE;GENN_MODEL"
                          "SOURCES;BOB_MODULES;EXTERNAL_LIBS;THIRD_PARTY;PLATFORMS"
                          "${ARGV}")

    # Check we're on a supported platform
    check_platform(${PARSED_ARGS_PLATFORMS})

    if(PARSED_ARGS_EXECUTABLE)
        set(NAME ${PARSED_ARGS_EXECUTABLE})
    else()
        # Use current folder as project name
        get_filename_component(NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    endif()
    project(${NAME})

    # Include local *.h files in project. We don't strictly need to do this, but
    # if we don't then they won't be included in generated Visual Studio
    # projects.
    file(GLOB H_FILES "*.h")

    if(PARSED_ARGS_SOURCES)
        # Build a single executable from these source files
        add_executable(${NAME} "${PARSED_ARGS_SOURCES}" "${H_FILES}")
        set(BOB_TARGETS ${NAME})
    else()
        # Build each *.cc file as a separate executable
        file(GLOB CC_FILES "*.cc")
        foreach(file IN LISTS CC_FILES)
            get_filename_component(shortname ${file} NAME)
            string(REGEX REPLACE "\\.[^.]*$" "" target ${shortname})
            add_executable(${target} "${file}" "${H_FILES}")
            list(APPEND BOB_TARGETS ${target})
        endforeach()
    endif()

    if(PARSED_ARGS_GENN_MODEL)
        get_filename_component(genn_model_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
        set(genn_model_dir "${CMAKE_CURRENT_BINARY_DIR}/${genn_model_name}_CODE")
        set(genn_model_src "${CMAKE_CURRENT_SOURCE_DIR}/${PARSED_ARGS_GENN_MODEL}")
        set(genn_model_dest "${genn_model_dir}/runner.cc")

        if(DEFINED ENV{CPU_ONLY} AND NOT $ENV{CPU_ONLY} STREQUAL 0)
            set(GENN_CPU_ONLY TRUE)
        else()
            set(GENN_CPU_ONLY ${PARSED_ARGS_GENN_CPU_ONLY})
        endif()
        if(GENN_CPU_ONLY)
            add_library(${PROJECT_NAME}_genn_model STATIC ${genn_model_dest})
            add_definitions(-DCPU_ONLY)
            set(CPU_FLAG -c)
        else() # Build with CUDA
            find_package(CUDA REQUIRED)

            # This is required as by default cmake builds files not ending in .cu
            # with the default compiler
            set_source_files_properties("${genn_model_dest}" PROPERTIES CUDA_SOURCE_PROPERTY_FORMAT OBJ)
            cuda_add_library(${PROJECT_NAME}_genn_model STATIC "${genn_model_dest}")
            set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -x cu -arch sm_30")
        endif()

        add_custom_command(OUTPUT ${genn_model_dest}
                           DEPENDS ${genn_model_src}
                           COMMAND $ENV{GENN_PATH}/lib/bin/genn-buildmodel.sh
                                   ${genn_model_src}
                                   ${CPU_FLAG}
                                   -i ${BOB_ROBOTICS_PATH}:${BOB_ROBOTICS_PATH}/include
                           COMMENT "Generating source code with GeNN")

        add_custom_target(${PROJECT_NAME}_genn_model_src ALL DEPENDS ${genn_model_dest})
        add_dependencies(${PROJECT_NAME}_genn_model ${PROJECT_NAME}_genn_model_src)

        foreach(target IN LISTS BOB_TARGETS)
            BoB_add_link_libraries(${PROJECT_NAME}_genn_model)
        endforeach()

        # We need GeNN support
        BoB_external_libraries(genn)

        # So code can access headers in the *_CODE folder
        BoB_add_include_directories(${CMAKE_CURRENT_BINARY_DIR})
    endif()

    # Do linking etc.
    BoB_build()

    # Copy all DLLs over from vcpkg dir. We don't necessarily need all of them,
    # but it would be a hassle to figure out which ones we need.
    if(WIN32)
        file(GLOB dll_files "$ENV{VCPKG_ROOT}/installed/${CMAKE_GENERATOR_PLATFORM}-windows/bin/*.dll")
        foreach(file IN LISTS dll_files)
            get_filename_component(filename "${file}" NAME)
            if(NOT EXISTS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${filename}")
                message("Copying ${filename} to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}...")
                file(COPY "${file}"
                     DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
            endif()
        endforeach()

        link_directories("${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
endmacro()

# Build a module with extra libraries etc. Currently used by robots/bebop
# module because the stock BoB_module() isn't flexible enough.
macro(BoB_module_custom)
    BoB_init()

    include(CMakeParseArguments)
    cmake_parse_arguments(PARSED_ARGS
                          ""
                          ""
                          "BOB_MODULES;EXTERNAL_LIBS;THIRD_PARTY;PLATFORMS"
                          "${ARGV}")

    # Check we're on a supported platform
    check_platform(${PARSED_ARGS_PLATFORMS})

    # Module name is based on path relative to src/
    file(RELATIVE_PATH NAME "${BOB_ROBOTICS_PATH}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
    set(BOB_TARGETS bob_${NAME})
    string(REPLACE / _ BOB_TARGETS ${BOB_TARGETS})
    project(${BOB_TARGETS})

    file(GLOB SRC_FILES
        "${BOB_ROBOTICS_PATH}/include/${NAME}/*.h"
        "*.cc"
    )
    add_library(${BOB_TARGETS} STATIC ${SRC_FILES})
    set_target_properties(${BOB_TARGETS} PROPERTIES PREFIX ./lib)
    add_definitions(-DNO_HEADER_DEFINITIONS)
endmacro()

macro(BoB_init)
    # CMake defaults to 32-bit builds on Windows
    if(WIN32 AND NOT CMAKE_GENERATOR_PLATFORM)
        message(WARNING "CMAKE_GENERATOR_PLATFORM is set to x86. This is probably not what you want!")
    endif()

    # For release builds, CMake disables assertions, but a) this isn't what we
    # want and b) it will break code.
    if(MSVC)
        string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    else()
        string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    endif()
endmacro()

macro(add_compile_flags EXTRA_ARGS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EXTRA_ARGS}")
endmacro()

macro(add_linker_flags EXTRA_ARGS)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EXTRA_ARGS}")
endmacro()

macro(always_included_packages)
    # Assume we always want threading
    find_package(Threads REQUIRED)

    # Annoyingly, these packages export a target rather than simply variables
    # with the include path and link flags and it seems that this target isn't
    # "passed up" by add_subdirectory(), so we always include these packages on
    # the off-chance we need them.
    if(NOT TARGET Eigen3::Eigen)
        find_package(Eigen3)
    endif()
    if(NOT TARGET OpenMP::OpenMP_CXX)
        find_package(OpenMP)
    endif()
    if(NOT TARGET GLEW::GLEW)
        find_package(GLEW)
    endif()
endmacro()

macro(BoB_build)
    # Don't build i2c code if NO_I2C environment variable is set
    if(NOT I2C_MESSAGE_DISPLAYED AND (NO_I2C OR (DEFINED ENV{NO_I2C} AND NOT ENV{NO_I2C} EQUAL 0)))
        set(I2C_MESSAGE_DISPLAYED TRUE)
        message("NO_I2C is set: not building i2c code")
        set(NO_I2C TRUE)
        add_definitions(-DNO_I2C)
    endif()

    # Default to building release type
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
    endif()

    # Set DEBUG macro when compiling in debug mode
    add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>")

    # Use C++14
    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Flags for gcc and clang
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        # Default to building with -march=native
        if(NOT DEFINED ENV{ARCH})
            set(ENV{ARCH} native)
        endif()

        # Enable warnings and set architecture
        add_compile_flags("-Wall -Wpedantic -Wextra -march=$ENV{ARCH}")

        # Disable optimisation, enable debug symbols
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

        # Enable optimisations at level O2
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
    endif()

    # Set include dirs and link libraries for this module/project
    always_included_packages()
    BoB_modules(${PARSED_ARGS_BOB_MODULES})
    BoB_external_libraries(${PARSED_ARGS_EXTERNAL_LIBS})
    BoB_third_party(${PARSED_ARGS_THIRD_PARTY})

    # Link threading lib
    BoB_add_link_libraries(${CMAKE_THREAD_LIBS_INIT})

    # The list of linked libraries can end up very long with lots of duplicate
    # entries and this can break ld, so remove them. We remove from the start,
    # so that dependencies will always (I think!) be in the right order.
    list(REVERSE ${PROJECT_NAME}_LIBRARIES)
    list(REMOVE_DUPLICATES ${PROJECT_NAME}_LIBRARIES)
    list(REVERSE ${PROJECT_NAME}_LIBRARIES)

    # Link all targets against the libraries
    foreach(target IN LISTS BOB_TARGETS)
        target_link_libraries(${target} ${${PROJECT_NAME}_LIBRARIES})
    endforeach()
endmacro()

function(check_platform)
    # If it's an empty list, return
    if(${ARGC} EQUAL 0)
        return()
    endif()

    # Check the platforms are valid
    foreach(platform IN LISTS ARGV)
        if(NOT "${platform}" STREQUAL unix
           AND NOT "${platform}" STREQUAL linux
           AND NOT "${platform}" STREQUAL windows
           AND NOT "${platform}" STREQUAL all)
            message(FATAL_ERROR "Bad platform: ${platform}. Possible platforms are unix, linux, windows or all.")
        endif()
    endforeach()

    # Check for platform all
    list(FIND ARGV all index)
    if(${index} GREATER -1)
        return()
    endif()

    # Check for platform unix
    if(UNIX)
        list(FIND ARGV unix index)
        if(${index} GREATER -1)
            return()
        endif()

        # Check for platform linux
        if("${CMAKE_SYSTEM_NAME}" STREQUAL Linux)
            list(FIND ARGV linux index)
            if(${index} GREATER -1)
                return()
            endif()
        endif()
    endif()

    # Check for platform windows
    if(WIN32)
        list(FIND ARGV windows index)
        if(${index} GREATER -1)
            return()
        endif()
    endif()

    # Otherwise we haven't matched any platforms
    message(FATAL_ERROR "This machine is not one of the supported platforms for this project (${ARGV}).")
endfunction()

macro(BoB_add_link_libraries)
    set(${PROJECT_NAME}_LIBRARIES "${${PROJECT_NAME}_LIBRARIES};${ARGV}"
        CACHE INTERNAL "${PROJECT_NAME}: Libraries" FORCE)
endmacro()

function(BoB_add_include_directories)
    # Include directory locally...
    include_directories(${ARGV})

    # ...and export to parent project
    set(${PROJECT_NAME}_INCLUDE_DIRS "${${PROJECT_NAME}_INCLUDE_DIRS};${ARGV}"
        CACHE INTERNAL "${PROJECT_NAME}: Include Directories" FORCE)
    list(REMOVE_DUPLICATES ${PROJECT_NAME}_INCLUDE_DIRS)
endfunction()

function(BoB_modules)
    foreach(module IN LISTS ARGV)
        set(module_path ${BOB_ROBOTICS_PATH}/src/${module})

        # Some (sub)modules have a slash in the name; replace with underscore
        string(REPLACE / _ module_name ${module})

        # All of our targets depend on this module
        foreach(target IN LISTS BOB_TARGETS)
            add_dependencies(${target} bob_${module_name})
        endforeach()

        # Build subdirectory
        if(NOT TARGET bob_${module_name})
            add_subdirectory(${module_path} "${BOB_DIR}/modules/${module_name}")
        endif()

        # Link against BoB module static lib + its dependencies
        BoB_add_link_libraries(bob_${module_name} ${bob_${module_name}_LIBRARIES})
        BoB_add_include_directories(${bob_${module_name}_INCLUDE_DIRS})
    endforeach()
endfunction()

function(BoB_find_package)
    find_package(${ARGV})
    BoB_add_include_directories(${${ARGV0}_INCLUDE_DIRS})
    BoB_add_link_libraries(${${ARGV0}_LIBS} ${${ARGV0}_LIBRARIES})
endfunction()

function(BoB_add_pkg_config_libraries)
    find_package(PkgConfig)
    foreach(lib IN LISTS ARGV)
        pkg_check_modules(${lib} REQUIRED ${lib})
        BoB_add_include_directories(${${lib}_INCLUDE_DIRS})
        BoB_add_link_libraries(${${lib}_LIBRARIES})
    endforeach()
endfunction()

function(BoB_external_libraries)
    foreach(lib IN LISTS ARGV)
        if(${lib} STREQUAL i2c)
            # With cmake you don't get errors for linking against a non-existent
            # library, but we might as well not bother if we definitely don't
            # need it.
            if(NOT WIN32 AND NOT NO_I2C)
                BoB_add_link_libraries("i2c")
            endif()
        elseif(${lib} STREQUAL opencv)
            BoB_find_package(OpenCV REQUIRED)
        elseif(${lib} STREQUAL eigen3)
            if(NOT TARGET Eigen3::Eigen)
                message(FATAL_ERROR "Eigen 3 not found")
            endif()
            BoB_add_link_libraries(Eigen3::Eigen)

            # For CMake < 3.9, we need to make the target ourselves
            if(NOT OpenMP_CXX_FOUND)
                find_package(Threads REQUIRED)
                add_library(OpenMP::OpenMP_CXX IMPORTED INTERFACE)
                set_property(TARGET OpenMP::OpenMP_CXX
                             PROPERTY INTERFACE_COMPILE_OPTIONS ${OpenMP_CXX_FLAGS})

                # Only works if the same flag is passed to the linker; use CMake 3.9+ otherwise (Intel, AppleClang)
                set_property(TARGET OpenMP::OpenMP_CXX
                             PROPERTY INTERFACE_LINK_LIBRARIES ${OpenMP_CXX_FLAGS} Threads::Threads)

                BoB_add_link_libraries(OpenMP::OpenMP_CXX)
            endif()
        elseif(${lib} STREQUAL sfml-graphics)
            if(UNIX)
                BoB_add_pkg_config_libraries(sfml-graphics)
            else()
                find_package(SFML REQUIRED graphics)
                BoB_add_include_directories(${SFML_INCLUDE_DIR})
                BoB_add_link_libraries(${SFML_LIBRARIES} ${SFML_DEPENDENCIES})
            endif()
        elseif(${lib} STREQUAL sdl2)
            if(UNIX)
                BoB_add_pkg_config_libraries(sdl2)
            else()
                find_package(SDL2 REQUIRED)
                BoB_add_include_directories(${SDL2_INCLUDE_DIRS})
                BoB_add_link_libraries(${SDL2_LIBRARIES})
            endif()
        elseif(${lib} STREQUAL glfw3)
            find_package(glfw3 REQUIRED)
            BoB_add_link_libraries(glfw)
            BoB_external_libraries(opengl)
        elseif(${lib} STREQUAL glew)
            if(NOT TARGET GLEW::GLEW)
                message(FATAL_ERROR "Could not find glew")
            endif()

            BoB_add_link_libraries(GLEW::GLEW)
            BoB_external_libraries(opengl)
        elseif(${lib} STREQUAL opengl)
            # Newer versions of cmake give a deprecation warning
            set(OpenGL_GL_PREFERENCE LEGACY)

            find_package(OpenGL REQUIRED)
            BoB_add_include_directories(${OPENGL_INCLUDE_DIR})
            BoB_add_link_libraries(${OPENGL_gl_LIBRARY} ${OPENGL_glu_LIBRARY})
        elseif(${lib} STREQUAL genn)
            if(NOT DEFINED ENV{GENN_PATH})
                message(FATAL_ERROR "GENN_PATH environment variable is not set")
            endif()

            BoB_add_include_directories("$ENV{GENN_PATH}/lib/include" "$ENV{GENN_PATH}/userproject/include")
            if(GENN_CPU_ONLY)
                BoB_add_link_libraries("$ENV{GENN_PATH}/lib/lib/libgenn_CPU_ONLY${CMAKE_STATIC_LIBRARY_SUFFIX}")
            else()
                BoB_add_link_libraries("$ENV{GENN_PATH}/lib/lib/libgenn${CMAKE_STATIC_LIBRARY_SUFFIX}")
                BoB_external_libraries(cuda)
            endif()
        elseif(${lib} STREQUAL cuda)
            BoB_find_package(CUDA REQUIRED)
        elseif(${lib} STREQUAL gtest)
            find_package(GTest REQUIRED)
            BoB_add_include_directories(${GTEST_INCLUDE_DIRS})
            BoB_add_link_libraries(${GTEST_LIBRARIES})
        else()
            message(FATAL_ERROR "${lib} is not a recognised library name")
        endif()
    endforeach()
endfunction()

macro(exec_or_fail)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE rv OUTPUT_VARIABLE SHELL_OUTPUT)
    if(NOT ${rv} EQUAL 0)
        message(FATAL_ERROR "Error while executing: ${ARGV}")
    endif()
endmacro()

function(BoB_third_party)
    foreach(module IN LISTS ARGV)
        if("${module}" STREQUAL matplotlibcpp)
            find_package(PythonLibs REQUIRED)
            BoB_add_include_directories(${PYTHON_INCLUDE_DIRS})
            BoB_add_link_libraries(${PYTHON_LIBRARIES})

            # Also include numpy headers on *nix (gives better performance)
            if(WIN32)
                add_definitions(-DWITHOUT_NUMPY)
            else()
                exec_or_fail("python" "${BOB_ROBOTICS_PATH}/cmake/find_numpy.py")
                BoB_add_include_directories(${SHELL_OUTPUT})
            endif()
        else()
            # Checkout git submodules under this path
            find_package(Git REQUIRED)
            exec_or_fail(${GIT_EXECUTABLE} submodule update --init --recursive third_party/${module}
                         WORKING_DIRECTORY ${BOB_ROBOTICS_PATH})

            # If this folder is a cmake project, then build it
            set(module_path ${BOB_ROBOTICS_PATH}/third_party/${module})
            if(EXISTS ${module_path}/CMakeLists.txt)
                add_subdirectory(${module_path} "${BOB_DIR}/third_party/${module}")
            endif()

            # Add to include path
            set(module_path ${BOB_ROBOTICS_PATH}/third_party/${module})
            include_directories(${module_path} ${module_path}/include)

            # Extra actions
            if(${module} STREQUAL ev3dev-lang-cpp)
                BoB_add_link_libraries(ev3dev)
            elseif(${module} STREQUAL imgui)
                BoB_add_link_libraries(imgui)
            endif()
        endif()
    endforeach()
endfunction()

# Don't allow in-source builds
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source builds not allowed.
    Please make a new directory (called a build directory) and run CMake from there.
    You may need to remove CMakeCache.txt." )
endif()

# Set output directories for libs and executables
set(BOB_ROBOTICS_PATH "${CMAKE_CURRENT_LIST_DIR}/..")

# If this var is defined then this project is being included in another build
if(NOT DEFINED BOB_DIR)
    if(WIN32)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BOB_ROBOTICS_PATH}/bin)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BOB_ROBOTICS_PATH}/bin)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BOB_ROBOTICS_PATH}/bin)
    else()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()

    # Folder to build BoB modules + third-party modules
    set(BOB_DIR "${CMAKE_CURRENT_BINARY_DIR}/BoB")
endif()

# Use vcpkg on Windows
if(WIN32)
    # Use vcpkg's cmake toolchain
    if(DEFINED ENV{VCPKG_ROOT})
        if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
            set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
                CACHE STRING "")
        endif()
    else()
        message(FATAL_ERROR "The environment VCPKG_ROOT must be set on Windows")
    endif()

    # The vcpkg toolchain in theory should do something like this already, but
    # if we don't do this, then cmake can't find any of vcpkg's packages
    file(GLOB children "$ENV{VCPKG_ROOT}/installed/${CMAKE_GENERATOR_PLATFORM}-windows")
    foreach(child IN LISTS children)
        if(IS_DIRECTORY "${child}")
            list(APPEND CMAKE_PREFIX_PATH "${child}")
        endif()
    endforeach()

    # Suppress warnings about std::getenv being insecure
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# Assume we always need plog
BoB_third_party(plog)

# Default include paths
include_directories(${BOB_ROBOTICS_PATH}
                    ${BOB_ROBOTICS_PATH}/include)

# Disable some of the units types in units.h for faster compilation
add_definitions(
    -DDISABLE_PREDEFINED_UNITS
    -DENABLE_PREDEFINED_LENGTH_UNITS
    -DENABLE_PREDEFINED_TIME_UNITS
    -DENABLE_PREDEFINED_ANGLE_UNITS
    -DENABLE_PREDEFINED_VELOCITY_UNITS
    -DENABLE_PREDEFINED_ANGULAR_VELOCITY_UNITS
)

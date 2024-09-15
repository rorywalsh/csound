set(USE_PORTAUDIO 0)
set(USE_PORTMIDI 0)
set(HAVE_SPRINTF_L 0)
set(USE_IPMIDI 0)
set(BUILD_TESTS 0)
set(BUILD_STATIC_LIBRARY 1)
set(BUILD_JAVA_INTERFACE OFF)
set(INSTALL_PYTHON_INTERFACE OFF)
set(BUILD_UTILITIES OFF)
set(USE_DOUBLE 0)

set(BUILD_CSBEATS ON)
set(BUILD_CSOUND_COMMAND ON)


if(${CMAKE_BUILD_TYPE} MATCHES "Release")
    set(CMAKE_C_FLAGS "-fno-exceptions -sUSE_PTHREADS=1 -sSUPPORT_LONGJMP='wasm' -sWASM_BIGINT -Wall -fPIC")
    set(CMAKE_CXX_FLAGS "-fno-exceptions -sUSE_PTHREADS=1 -sSUPPORT_LONGJMP='wasm' -sWASM_BIGINT -Wall -fPIC")
    set(CMAKE_SHARED_LINKER_FLAGS "-sSIDE_MODULE=1 -sEXPORT_ALL")
    set(CMAKE_STATIC_LINKER_FLAGS "")
else()
    set(CMAKE_C_FLAGS "-fno-exceptions -sUSE_PTHREADS=1 -sSUPPORT_LONGJMP='wasm' -sWASM_BIGINT -Wall -fPIC -g")
    set(CMAKE_CXX_FLAGS "-fno-exceptions -sUSE_PTHREADS=1 -sSUPPORT_LONGJMP='wasm' -sWASM_BIGINT -Wall -fPIC -g")
    set(CMAKE_SHARED_LINKER_FLAGS "-sSIDE_MODULE=1 -sEXPORT_ALL")
    set(CMAKE_STATIC_LINKER_FLAGS "")
endif()

set(CMAKE_EXE_LINKER_FLAGS "-sNODERAWFS=1 -sMAIN_MODULE")
set(CMAKE_EXECUTABLE_SUFFIX ".js")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".wasm")

file(GLOB_RECURSE POSEIDON_GLES32_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

set(POSEIDON_GLES32_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/EngineGLES32.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GLVertexAttribLayouts.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SDLEventWindow.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TextureGLES32.hpp
)

add_library(PoseidonGLES32 STATIC
    ${POSEIDON_GLES32_SOURCES}
    ${POSEIDON_GLES32_HEADERS}
    ${CMAKE_SOURCE_DIR}/thirdparty/glad-es/src/gles2.c
    GraphicsBackendGLES32.cpp
)

target_include_directories(PoseidonGLES32 PUBLIC
    ${CMAKE_SOURCE_DIR}/thirdparty/glad-es/include
)

target_link_libraries(PoseidonGLES32 PUBLIC Poseidon)

target_compile_definitions(PoseidonGLES32 PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
if(WIN32)
    target_compile_definitions(PoseidonGLES32 PRIVATE
        __MSC__=1100
        _WINDOWS
    )
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    target_compile_options(PoseidonGLES32 PRIVATE
        /GR /Oi /GF
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Ob1>
        $<$<CONFIG:Debug>:/MDd>
        /permissive
        /wd4291 /wd5030 /wd4799 /wd4068 /wd4996
        /wd4244 /wd4146 /wd4018 /wd4101 /wd4305 /wd4838
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error=non-pod-varargs>
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error=c++11-narrowing>
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error=ordered-compare-function-pointers>
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error>
    )
else()
    target_compile_options(PoseidonGLES32 PRIVATE
        -Wno-error
        -Wno-c++11-narrowing
        -Wno-non-pod-varargs
        -Wno-char-subscripts
        -Wno-deprecated-volatile
        -Wno-missing-field-initializers
        -Wno-int-to-void-pointer-cast
        -Wno-void-pointer-to-int-cast
    )
endif()

if(ANDROID)
    target_link_libraries(PoseidonGLES32 PUBLIC GLESv3 EGL)
elseif(WIN32)
    target_link_libraries(PoseidonGLES32 PUBLIC opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(PoseidonGLES32 PUBLIC OpenGL::GL)
endif()

set_target_properties(PoseidonGLES32 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    OUTPUT_NAME "PoseidonGLES32"
)

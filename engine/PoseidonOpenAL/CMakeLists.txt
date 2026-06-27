set(POSEIDON_OPENAL_SOURCES
    SoundSystemOAL.cpp
    WaveOAL.cpp
    Voice/VoNCaptureOpenAL.cpp
    Voice/MicLoopbackOpenAL.cpp
    Voice/VoiceBackendOpenAL.cpp
)

set(POSEIDON_OPENAL_HEADERS
    EFXPresets.hpp
    OpenALRuntime.hpp
    SoundSystemOAL.hpp
    WaveOAL.hpp
    Voice/VoNCaptureOpenAL.hpp
    Voice/VoNSpeakerOpenAL.hpp
    Voice/MicLoopbackOpenAL.hpp
)

add_library(PoseidonOpenAL STATIC
    ${POSEIDON_OPENAL_SOURCES}
    ${POSEIDON_OPENAL_HEADERS}
)

if(ANDROID)
    find_library(OBOE_LIB oboe)
    add_library(oboe::oboe STATIC IMPORTED)
    set_target_properties(oboe::oboe PROPERTIES
        IMPORTED_LOCATION ${OBOE_LIB}
    )
endif()
find_package(OpenAL CONFIG REQUIRED)
set_target_properties(OpenAL::OpenAL PROPERTIES IMPORTED_GLOBAL TRUE)

target_link_libraries(PoseidonOpenAL PUBLIC Poseidon)
if(ANDROID)
    target_link_libraries(PoseidonOpenAL PUBLIC OpenAL::OpenAL)
endif()

if(WIN32)
    target_link_libraries(PoseidonOpenAL PUBLIC Avrt)
endif()

target_include_directories(PoseidonOpenAL PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
)
target_include_directories(PoseidonOpenAL PRIVATE
    $<TARGET_PROPERTY:OpenAL::OpenAL,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_definitions(PoseidonOpenAL PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

set_target_properties(PoseidonOpenAL PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "PoseidonOpenAL"
)

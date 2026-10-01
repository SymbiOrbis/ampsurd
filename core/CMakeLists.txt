# ---------------------------------------------------------------------------
# nam_core: the official NeuralAmpModelerCore sources, compiled unmodified.
# Every architecture that NAM Core supports (A2 Full/Lite, slimmable A2
# containers, A1 WaveNet, LSTM, ConvNet, Linear) is available through
# nam::get_dsp(). MONSTROSITY adds no wrapper format and no conversion step.
# ---------------------------------------------------------------------------
file(GLOB NAM_SOURCES_TOP CONFIGURE_DEPENDS "${namcore_SOURCE_DIR}/NAM/*.cpp")
file(GLOB NAM_SOURCES_SUB CONFIGURE_DEPENDS "${namcore_SOURCE_DIR}/NAM/*/*.cpp")

add_library(nam_core STATIC ${NAM_SOURCES_TOP} ${NAM_SOURCES_SUB})
target_include_directories(nam_core SYSTEM PUBLIC
  "${namcore_SOURCE_DIR}"
  "${namcore_SOURCE_DIR}/Dependencies/nlohmann"
  "${eigen_SOURCE_DIR}")
target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)
if(WIN32)
  target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
  target_compile_options(nam_core PRIVATE /bigobj /W0 "$<$<CONFIG:Release>:/O2>")
else()
  target_compile_options(nam_core PRIVATE -w "$<$<CONFIG:Release>:-O3>")
endif()

# ---------------------------------------------------------------------------
# monstrosity_core: framework-independent engine (no JUCE). Reusable by the
# plugin, by the command-line test tools and by any future product.
# ---------------------------------------------------------------------------
add_library(monstrosity_core STATIC
  src/CaptureModel.cpp
  src/CaptureSlot.cpp)
target_include_directories(monstrosity_core PUBLIC include)
target_include_directories(monstrosity_core SYSTEM PRIVATE "${audiodsptools_SOURCE_DIR}/dsp")
# NAM Core registers each architecture's loader through static initialisers. A plain
# static-library link lets the linker drop those as "unused", after which nothing
# loads ("No config parser registered"). WHOLE_ARCHIVE keeps every architecture.
target_link_libraries(monstrosity_core PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
if(MSVC)
  target_compile_options(monstrosity_core PRIVATE /W4 /bigobj)
else()
  target_compile_options(monstrosity_core PRIVATE -Wall -Wextra)
endif()

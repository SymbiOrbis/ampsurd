# Third-party dependencies, all pinned to exact commits so every build is reproducible.
# CMake downloads them automatically on the first configure.
#
# To build offline or from local clones, pass e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=C:/src/JUCE
#   -DFETCHCONTENT_SOURCE_DIR_NAMCORE=...  -DFETCHCONTENT_SOURCE_DIR_AUDIODSPTOOLS=...  -DFETCHCONTENT_SOURCE_DIR_EIGEN=...

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- JUCE 9.0.3 (plugin framework, includes the MIT-licensed VST3 SDK) -------------
if(MONSTROSITY_BUILD_PLUGIN)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        be29c81492b6151c8ea8d14c840e1311963b3a83 # 9.0.3
    GIT_SHALLOW    FALSE
    GIT_PROGRESS   TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

# --- NeuralAmpModelerCore v0.6.0 (official NAM inference engine, MIT) ---------------
# SOURCE_SUBDIR points at a non-existent folder so CMake only downloads the sources;
# we compile them ourselves (NAM's own CMakeLists builds test tools and sets global flags).
FetchContent_Declare(NAMCORE
  GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG        0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842 # v0.6.0
  GIT_SUBMODULES ""
  SOURCE_SUBDIR  _monstrosity_do_not_build)
FetchContent_MakeAvailable(NAMCORE)

# --- AudioDSPTools (resampler used by the official NAM plugin, MIT + iPlug2 zlib-style) --
# Same commit NeuralAmpModelerCore v0.6.0 pins as its submodule.
FetchContent_Declare(AUDIODSPTOOLS
  GIT_REPOSITORY https://github.com/sdatkinson/AudioDSPTools.git
  GIT_TAG        0827c6c2fc0deced568536142ea86f189e0b98a1
  SOURCE_SUBDIR  _monstrosity_do_not_build)
FetchContent_MakeAvailable(AUDIODSPTOOLS)

# --- Eigen (linear algebra used by NAM, MPL-2.0) -------------------------------------
# Same commit NeuralAmpModelerCore v0.6.0 pins. Official home is GitLab; a GitHub mirror
# can be selected with -DMONSTROSITY_EIGEN_GIT=https://github.com/eigen-mirror/eigen.git
set(MONSTROSITY_EIGEN_GIT "https://gitlab.com/libeigen/eigen.git" CACHE STRING "Eigen git URL")
FetchContent_Declare(EIGEN
  GIT_REPOSITORY ${MONSTROSITY_EIGEN_GIT}
  GIT_TAG        bc3b39870ecb690a623a3f49149a358b95c5781d
  SOURCE_SUBDIR  _monstrosity_do_not_build)
FetchContent_MakeAvailable(EIGEN)

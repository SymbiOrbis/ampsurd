set(ADT_WAV "${audiodsptools_SOURCE_DIR}/dsp/wav.cpp")

foreach(tool ampsurd_render ampsurd_bench)
  add_executable(${tool} ${tool}.cpp ${ADT_WAV})
  target_include_directories(${tool} PRIVATE "${audiodsptools_SOURCE_DIR}/dsp" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${tool} PRIVATE ampsurd_core)
  if(NOT MSVC)
    find_package(Threads REQUIRED)
    target_link_libraries(${tool} PRIVATE Threads::Threads)
  endif()
endforeach()

add_executable(limiter_test limiter_test.cpp)
target_link_libraries(limiter_test PRIVATE ampsurd_core)

add_executable(engine_test engine_test.cpp)
target_link_libraries(engine_test PRIVATE ampsurd_core)

add_executable(mix_experiment mix_experiment.cpp)
target_link_libraries(mix_experiment PRIVATE ampsurd_core)

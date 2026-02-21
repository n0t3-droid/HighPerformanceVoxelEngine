# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-src")
  file(MAKE_DIRECTORY "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-build"
  "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-subbuild/fast_noise_lite-populate-prefix"
  "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-subbuild/fast_noise_lite-populate-prefix/tmp"
  "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-subbuild/fast_noise_lite-populate-prefix/src/fast_noise_lite-populate-stamp"
  "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-subbuild/fast_noise_lite-populate-prefix/src"
  "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-subbuild/fast_noise_lite-populate-prefix/src/fast_noise_lite-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-subbuild/fast_noise_lite-populate-prefix/src/fast_noise_lite-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/ylber/Downloads/r9-hyper-core/HighPerformanceVoxelEngine/build/_deps/fast_noise_lite-subbuild/fast_noise_lite-populate-prefix/src/fast_noise_lite-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()

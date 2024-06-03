# CMakeList.txt : CMake project for url_router, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.28)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("url_router")
find_package(ctre CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS url)

add_executable (url_router url_router.cpp url_router.h)

target_link_libraries(url_router PRIVATE ctre::ctre Boost::headers Boost::url)

set_property(TARGET url_router PROPERTY CXX_STANDARD 23)

# TODO: Add tests and install targets if needed.

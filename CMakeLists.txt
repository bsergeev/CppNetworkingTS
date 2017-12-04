cmake_minimum_required(VERSION 3.0)
set(REQUIRED_CPP_VERSION 17)

set(PROJECT_NAME try)
project(${PROJECT_NAME} CXX)
add_executable(${PROJECT_NAME} main.cpp)

find_package(Git)
if (NOT GIT_FOUND)
    message(FATAL_ERROR "Git not found, aborting...")
endif()

# Clone or update chriskohlhoff/networking-ts-impl repo
set(NETTS_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/netts)
find_path(NETTS_INCLUDE_DIR "net" NO_DEFAULT_PATH PATHS "${NETTS_LOCATION}/include/experimental")
if (NOT NETTS_INCLUDE_DIR) # The repo wasn't found => clone it
    execute_process(COMMAND git clone https://github.com/chriskohlhoff/networking-ts-impl.git ${NETTS_LOCATION})
else() # Pull the repo, in case it was updated
    execute_process(COMMAND git pull WORKING_DIRECTORY ${NETTS_LOCATION})
endif()
# Just in case, check again
find_path(NETTS_INCLUDE_DIR "net" NO_DEFAULT_PATH PATHS "${NETTS_LOCATION}/include/experimental")
if (NOT NETTS_INCLUDE_DIR)
    message(FATAL_ERROR "Something wrong with networking-ts-impl.git, aborting...")
endif()

include_directories("${NETTS_LOCATION}/include")

if (WIN32 OR WIN64)
    set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD ${REQUIRED_CPP_VERSION})
    set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
elseif(UNIX)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++${REQUIRED_CPP_VERSION} -stdlib=libc++")
    target_link_libraries(${PROJECT_NAME} c++ c++abi pthread)
endif()

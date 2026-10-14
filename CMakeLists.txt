cmake_minimum_required(VERSION 3.20)
project(EmotionChat VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Emotion 0.9 CONFIG REQUIRED)

add_library(emotion-chat-request STATIC src/request.c)
target_include_directories(emotion-chat-request PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(emotion-chat-request PUBLIC Emotion::core)

add_executable(emotion-chat
  src/main.c
  src/openai.c
)
target_include_directories(emotion-chat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(emotion-chat PRIVATE
  emotion-chat-request
  Emotion::http
  Emotion::data
  Emotion::auth
  Emotion::ui
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(emotion-chat-request PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(emotion-chat PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
  add_executable(test-emotion-chat-request tests/test_request.c)
  target_include_directories(test-emotion-chat-request PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(test-emotion-chat-request PRIVATE emotion-chat-request)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test-emotion-chat-request PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME emotion-chat-request COMMAND test-emotion-chat-request)
endif()

install(TARGETS emotion-chat RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

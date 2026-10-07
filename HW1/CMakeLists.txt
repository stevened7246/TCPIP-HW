cmake_minimum_required(VERSION 3.16)
# 建置 Windows 聊天室、共用協定與測試程式。
project(WinsockChat LANGUAGES CXX RC)
if(NOT WIN32)
  message(FATAL_ERROR "This project requires Windows Winsock")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(chat_protocol service/protocol/PacketParser.cpp service/protocol/PacketSerializer.cpp)
# 收送協定由伺服器、命令列及 GUI 共用，ws2_32 提供 Winsock。
target_include_directories(chat_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(chat_protocol PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
# PUBLIC 會傳給連結此函式庫的目標；NOMINMAX 避免 Windows 巨集干擾 std::min/max。
target_link_libraries(chat_protocol PUBLIC ws2_32)
add_executable(chat_server
  main.cpp
  service/Server.cpp
  service/AuthService.cpp
  service/RoomService.cpp
  service/RoomSessionService.cpp
  service/MessageService.cpp
  service/FileService.cpp
  repository/UserRepository.cpp
  repository/RoomRepository.cpp
  repository/MessageRepository.cpp
  API/AuthHandler.cpp
  API/RoomHandler.cpp
  API/MessageHandler.cpp
  API/FileHandler.cpp
)
target_link_libraries(chat_server PRIVATE chat_protocol)
add_executable(chat_client public/userinterface/main.cpp)
target_link_libraries(chat_client PRIVATE chat_protocol)
if(MSVC)
 target_compile_options(chat_protocol PUBLIC /W4 /utf-8)
else()
 target_compile_options(chat_protocol PUBLIC -Wall -Wextra -Wpedantic)
endif()

include(CTest)
# 找到 Python 時，註冊使用真實執行檔的整合測試。
if(BUILD_TESTING)
 find_package(Python3 COMPONENTS Interpreter)
 if(Python3_FOUND)
  add_test(NAME winsock_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py $<TARGET_FILE:chat_server> $<TARGET_FILE:chat_client> $<TARGET_FILE:gui_connection_test>)
  set_tests_properties(winsock_integration PROPERTIES TIMEOUT 90)
 endif()
endif()

# Embed the original artwork so launching from another directory still works.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/asset/聊天室背景.png" "${CMAKE_CURRENT_BINARY_DIR}/chat_background.png" COPYONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/chat_gui.rc" "101 RCDATA \"${CMAKE_CURRENT_BINARY_DIR}/chat_background.png\"\n")
add_executable(chat_gui WIN32
  public/userinterface/InlineImage.cpp
  public/userinterface/GuiMain.cpp
  public/userinterface/GuiConnection.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/chat_gui.rc"
)
target_compile_definitions(chat_gui PRIVATE UNICODE _UNICODE)
target_link_libraries(chat_gui PRIVATE chat_protocol uuid gdiplus comctl32 comdlg32 ole32 shell32)

set_target_properties(chat_gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gui")
# GUI 使用獨立輸出目錄；單一組態 MinGW 的程式位於 build/gui/chat_gui.exe。

if(BUILD_TESTING)
 add_executable(gui_connection_test tests/gui_connection_test.cpp public/userinterface/GuiConnection.cpp)
 target_link_libraries(gui_connection_test PRIVATE chat_protocol)
endif()

if(BUILD_TESTING)
 add_executable(inline_image_test tests/inline_image_test.cpp public/userinterface/InlineImage.cpp)
 target_link_libraries(inline_image_test PRIVATE chat_protocol uuid gdiplus ole32)
 add_test(NAME inline_image_preview COMMAND inline_image_test)
endif()

add_executable(examp_slider slider.cpp "notify.cpp") 

# target_include_directories(examp_slider PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(examp_slider PRIVATE 
        ftxui::screen
        ftxui::dom
        ftxui::component
)

add_executable(examp_notification notify.cpp "notify.cpp") 

# target_include_directories(examp_slider PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(examp_notification PRIVATE 
        ftxui::screen
        ftxui::dom
        ftxui::component
)

add_executable(file_test file_test.cpp)
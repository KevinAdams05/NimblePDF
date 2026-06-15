# Qt 6 frontend — Linux + Windows.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(NimblePDF
	main.cpp
	MainWindow.cpp
	MainWindow.h
)
target_link_libraries(NimblePDF PRIVATE Qt6::Widgets nimblepdf-core)
set_target_properties(NimblePDF PROPERTIES WIN32_EXECUTABLE ON)

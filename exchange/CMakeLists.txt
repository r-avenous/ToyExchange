add_executable(exchange main.cpp)
target_link_libraries(exchange PRIVATE common Threads::Threads)

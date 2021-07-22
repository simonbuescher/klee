#
add_executable(klee-symbolic-optimizer main.cpp JsonPrinter.cpp)

set(KLEE_LIBS
    kleeCore
)

target_link_libraries(klee-symbolic-optimizer ${KLEE_LIBS})

find_package(nlohmann_json REQUIRED)
target_link_libraries(klee-symbolic-optimizer nlohmann_json)

install(TARGETS klee-symbolic-optimizer RUNTIME DESTINATION bin)

# The KLEE binary depends on the runtimes
add_dependencies(klee-symbolic-optimizer BuildKLEERuntimes)

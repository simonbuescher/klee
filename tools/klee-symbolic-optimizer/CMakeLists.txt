#
add_executable(klee-symbolic-optimizer main.cpp)

set(KLEE_LIBS
    kleeCore
)

target_link_libraries(klee-symbolic-optimizer ${KLEE_LIBS})

install(TARGETS klee-symbolic-optimizer RUNTIME DESTINATION bin)

# The KLEE binary depends on the runtimes
add_dependencies(klee-symbolic-optimizer BuildKLEERuntimes)

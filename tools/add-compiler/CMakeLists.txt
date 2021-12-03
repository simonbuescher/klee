#
add_executable(add-compiler main.cpp Runner.cpp JsonPrinter.cpp ADDCodeGenerator.cpp ADDCodeGenerator.h ExpressionTreeCodeGenerator.cpp ExpressionTreeCodeGenerator.h ValueMap.cpp ValueMap.h)

set(KLEE_LIBS
    kleeCore
)

target_link_libraries(add-compiler ${KLEE_LIBS})

find_package(nlohmann_json REQUIRED)
target_link_libraries(add-compiler nlohmann_json)

install(TARGETS add-compiler RUNTIME DESTINATION bin)

# The KLEE binary depends on the runtimes
add_dependencies(add-compiler BuildKLEERuntimes)

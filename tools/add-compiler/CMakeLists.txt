#
add_executable(add-compiler main.cpp Runner.cpp JsonPrinter.cpp code-generation/ADDCodeGenerator.cpp code-generation/ADDCodeGenerator.h code-generation/ExpressionTreeCodeGenerator.cpp code-generation/ExpressionTreeCodeGenerator.h code-generation/ValueMap.cpp code-generation/ValueMap.h code-generation/CodeGenerator.cpp code-generation/CodeGenerator.h)

set(KLEE_LIBS
    kleeCore
)

target_link_libraries(add-compiler ${KLEE_LIBS})

find_package(nlohmann_json REQUIRED)
target_link_libraries(add-compiler nlohmann_json)

install(TARGETS add-compiler RUNTIME DESTINATION bin)

# The KLEE binary depends on the runtimes
add_dependencies(add-compiler BuildKLEERuntimes)

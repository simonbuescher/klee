//
// Created by simon on 29.06.21.
//

#include "Runner.h"


int main(int argc, char **argv) {
    Runner runner(argc, argv, "run-out");

    runner.init();
    runner.run();
}

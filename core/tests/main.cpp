#include "mpx_test.h"

int main(int argc, char** argv) {
    return mpxtest::runAll(argc > 1 ? argv[1] : nullptr);
}

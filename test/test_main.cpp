// Host test runner: ./fluante_tests [filter]
#include "strobe_cases.h"
#include "phase_lock_cases.h"
#include "spinner_cases.h"
#include "tree_cases.h"

int main(int argc, char** argv) { return check::run(argc > 1 ? argv[1] : nullptr) ? 1 : 0; }

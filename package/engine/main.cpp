// Deliberately tiny: the engine finds every REGISTER_MODULE'd module by itself.
#include "engine/engine.h"

int main(int argc, char** argv) {
    engine::Engine e;
    return e.run(argc, argv);
}

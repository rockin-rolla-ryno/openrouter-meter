// Desktop entry point: runs the real firmware setup()/loop() against SDL.
#include "sim.h"

void setup();
void loop();

int main(int, char**) {
    setup();
    while (!sim_should_quit()) {
        sim_pump();
        loop();
    }
    return 0;
}

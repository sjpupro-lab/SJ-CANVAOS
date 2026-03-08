#include "../include/canvasos_engine.h"
#include <stdio.h>

int main(void) {
    static CanvasEngine eng;
    engine_init(&eng);
    gate_open(&eng, 0);
    eng.grid[CANVAS_CY][CANVAS_CX].a_word = MAKE_AWORD(0x00, OP_OPEN, 0x0001);
    eng.grid[CANVAS_CY][CANVAS_CX].r = 0xFF;
    eng.grid[CANVAS_CY][CANVAS_CX].g = 0x80;
    eng.grid[CANVAS_CY][CANVAS_CX].b = 0x00;
    int r = cvp_save(&eng, "hello.cvp");
    if (r == 0) printf("hello.cvp created OK\n");
    else printf("error: %d\n", r);
    return r;
}

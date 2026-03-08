#ifndef TERVAS_RENDER_H
#define TERVAS_RENDER_H
#include "tervas_core.h"
#include "tervas_render_cell.h"
#include "tervas_projection.h"
/* Renderer는 TvFrame을 그리기만 한다 (Spec §11) */
int tv_render_frame(const Tervas *tv);
#endif

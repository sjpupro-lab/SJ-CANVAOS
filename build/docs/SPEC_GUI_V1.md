# CanvasOS GUI System v1

## Architecture

Two-layer pixel-buffer GUI with zero external dependencies.

### Inner Layer (SystemCanvas)
- Developer/system layer for placing rectangular elements
- Elements: Panel, Label, Image, Button, Custom
- Free positioning, z-order, drag/resize support
- Up to 128 elements, insertion-sort by z_order

### Outer Layer (HomeScreen)
- User-facing layer (mobile home screen style)
- Grid-based icon layout with auto-positioning
- Status bar with title and clock area
- Icons render initial letter + label below

## Rendering Pipeline

```
gui_render(ctx)
  ├── gui_home_render()    → background + statusbar + icons
  └── gui_sys_render()     → sorted elements on top
         ├── fill background
         ├── draw border
         ├── blit image (if IMAGE type)
         ├── draw label text (5×7 bitmap font)
         └── custom_draw callback
```

## Data Model

- **GuiPixel**: RGBA 4 bytes
- **GuiBuffer**: Heap-allocated pixel array (w × h)
- **GuiElement**: id, type, rect, z_order, colors, label, callbacks
- **GuiSystemCanvas**: Fixed array of elements + focus/drag state
- **GuiHomeScreen**: Icon grid + status bar + layout params
- **GuiContext**: sys + home + framebuffer (top-level)

## Event System

| Event | Description |
|-------|-------------|
| TOUCH_DOWN | Finger/mouse press → hit test → focus + drag start |
| TOUCH_MOVE | Drag active element |
| TOUCH_UP | Release drag |
| KEY_DOWN/UP | Keyboard input |

## Image Output

24-bit BMP (no compression), bottom-up row order.
Cross-platform readable by any image viewer.

## Build

```bash
gcc -std=c11 -Wall -Wextra -Iinclude \
  src/canvasos_gui.c tests/test_gui.c -o tests/test_gui
./tests/test_gui
```

## Test Coverage (G1–G17)

Buffer ops, pixel I/O, fill/outline, alpha blit, font rendering,
element add/remove/move/resize, z-order, home screen layout/render,
touch/drag events, full pipeline BMP output + header validation.

# Copilot Build Instructions

## Emscripten / WebGL 2.0 Build

### Prerequisites

```bash
sudo apt-get install -y emscripten
```

### Configure

```bash
mkdir -p /tmp/embuild && cd /tmp/embuild
emcmake cmake /path/to/ryzomcore \
  -DWITH_STATIC=ON \
  -DWITH_STATIC_DRIVERS=ON \
  -DWITH_DRIVER_OPENGL=OFF \
  -DWITH_DRIVER_OPENGLES=OFF \
  -DWITH_DRIVER_OPENGL3=OFF \
  -DWITH_DRIVER_OPENGLES3=ON \
  -DWITH_NEL_SAMPLES=ON \
  -DWITH_NEL_TOOLS=OFF \
  -DWITH_NEL_TESTS=OFF \
  -DWITH_RYZOM=OFF \
  -DWITH_RYZOM_CLIENT=OFF \
  -DWITH_RYZOM_SERVER=OFF \
  -DWITH_RYZOM_TOOLS=OFF \
  -DWITH_NELNS=OFF \
  -DWITH_SNOWBALLS=OFF \
  -DWITH_GUI=OFF \
  -DWITH_SOUND=OFF \
  -DWITH_LOGIC=OFF \
  -DWITH_PACS=OFF \
  -DWITH_GEORGES=OFF \
  -DWITH_LIGO=OFF \
  -DWITH_NET=OFF \
  -DWITH_WEB=OFF \
  -DCMAKE_BUILD_TYPE=Release
```

### Build samples

```bash
cd /tmp/embuild
emmake make -j$(nproc) nl_sample_clip_plane nl_sample_nelvp nl_sample_planar_reflection nl_sample_ppl
```

### Serve and test in browser

Create an HTML host page per sample (replace `SAMPLE_NAME`):

```html
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>NeL Sample - WebGL 2.0</title>
<style>body{margin:0;background:#000}canvas{display:block;margin:auto;width:800px;height:600px}</style>
</head>
<body>
<canvas id="canvas" width="800" height="600" oncontextmenu="event.preventDefault()"></canvas>
<script>var Module={canvas:document.getElementById('canvas')};</script>
<script src="SAMPLE_NAME.js"></script>
</body>
</html>
```

Serve from the build output directory:

```bash
cd /tmp/embuild/bin
python3 -m http.server 8888
```

Then navigate Playwright to `http://localhost:8888/SAMPLE_NAME.html`.

### Notes

- WebGL 2.0 `glMapBufferRange` only supports `GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT`
- UBOs must be pre-allocated to full declared size before first draw call
- SSO (Separate Shader Objects) is not available in WebGL 2.0 / GLES 3.0
- Builtin (non-mega) shaders are never used under GLES 3.0; only linked mega shaders
- `CTime::getPerformanceTime()` uses `clock_gettime(CLOCK_MONOTONIC)` on Emscripten; without this, `ticksToSecond()` returns NaN and all matrices become invalid
- Non-UBO uniforms are not used in the linked shader path; everything uses UBOs
- The linked mega shader path always has priority even when SSO is enabled; SSO is just a fallback
- Client-side index arrays (glDrawElements with CPU pointers) work on desktop GL but NOT on WebGL 2.0; the driver uses a scratch element buffer for GLES3
- Nelvp-converted vertex programs use a transparent UBO for constant registers; callers still use setUniform* which the driver routes through the UBO internally
- The nelvp sample has link warnings on WebGL 2.0 due to interface mismatches between nelvp-converted VP outputs and mega PP inputs; clip_plane, ppl, and planar_reflection all render correctly

## Desktop OpenGL 3.3 Build

```bash
sudo apt-get install -y libx11-dev libxrandr-dev libxrender-dev \
  libxcursor-dev libxinerama-dev libgl-dev libfreetype-dev \
  libxml2-dev libpng-dev zlib1g-dev libcurl4-openssl-dev

mkdir -p /tmp/build && cd /tmp/build
cmake /path/to/ryzomcore \
  -DWITH_STATIC=ON \
  -DWITH_STATIC_DRIVERS=ON \
  -DWITH_NEL_SAMPLES=ON \
  -DWITH_NEL_TOOLS=OFF \
  -DWITH_NEL_TESTS=OFF \
  -DWITH_RYZOM=OFF \
  -DWITH_NELNS=OFF \
  -DWITH_SNOWBALLS=OFF \
  -DWITH_GUI=OFF \
  -DWITH_SOUND=OFF \
  -DWITH_LOGIC=OFF \
  -DWITH_PACS=OFF \
  -DWITH_GEORGES=OFF \
  -DWITH_LIGO=OFF \
  -DWITH_NET=OFF \
  -DWITH_WEB=OFF \
  -DCMAKE_BUILD_TYPE=Release

make -j$(nproc) nl_sample_clip_plane nl_sample_nelvp nl_sample_planar_reflection nl_sample_ppl
```

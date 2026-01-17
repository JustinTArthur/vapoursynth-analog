## Building vapoursynth-analog

### Build Dependencies

- [Meson](https://mesonbuild.com/) build system
- C++20 compiler (GCC 10+, Clang 12+, or MSVC 2019+)
- [VapourSynth](https://www.vapoursynth.com/) (>= R55) development headers
- [Qt6](https://www.qt.io/) (Core module)
- [FFTW3](http://www.fftw.org/)
- [SQLite3](https://www.sqlite.org/)

#### Installing Build Dependencies

**macOS (Homebrew):**
```bash
brew install meson qt6 fftw sqlite vapoursynth
```

**Ubuntu/Debian:**
```bash
sudo apt install meson build-essential qt6-base-dev libfftw3-dev libsqlite3-dev vapoursynth-dev
```

**Fedora:**
```bash
sudo dnf install meson gcc-c++ qt6-qtbase-devel fftw-devel sqlite-devel vapoursynth-devel
```

**Arch Linux:**
```bash
sudo pacman -S meson qt6-base fftw sqlite vapoursynth
```

### Building

```bash
git clone --recursive https://github.com/yourusername/vapoursynth-analog.git
cd vapoursynth-analog
meson setup build
meson compile -C build
```

The plugin will be built as:
- `build/vsanalog.dylib` (macOS)
- `build/vsanalog.so` (Linux)
- `build/vsanalog.dll` (Windows)

### Installation

Copy the built plugin to your VapourSynth plugins directory:

**macOS:**
```bash
cp build/vsanalog.dylib ~/Library/Application\ Support/VapourSynth/plugins64/
```

**Linux:**
```bash
cp build/vsanalog.so ~/.local/lib/vapoursynth/
```

Or load it explicitly in your VapourSynth script:
```python
core.std.LoadPlugin("/path/to/vsanalog.dylib")
```

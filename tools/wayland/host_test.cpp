// Host-side harness for the compositor. It runs the real wayland::Server (the same code that ships
// in the app) on Linux, so real Wayland clients (foot, weston demos, wev, GTK, Qt) can be run
// against it without a headset. Frames are kept in memory and written out as PNG on request.
//
//   host_test --dir /run/wl [--scale 2] [--trace] script.txt
//
// Script commands (one per line, '#' comments):
//   wait <seconds>          keep dispatching for a while
//   dump <file.png>         composite the current toplevel (surface, subsurfaces, popups)
//   list                    log toplevels
//   focus                   give the current toplevel keyboard focus
//   configure <w> <h>       ask the client for a logical size (0 0 = its choice)
//   motion <x> <y>          pointer position in logical pixels of the toplevel
//   button <code> <0|1>     evdev button (272 = left)
//   key <code> <0|1>        evdev key (30 = A)
//   type <text>             press and release keys for lowercase letters, digits, space, Enter
//   axis <value>            scroll
//   dismiss                 close open popups
//   close                   ask the client to close the toplevel
//   scale <n>               change the output scale
//   quit
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "wayland/server.h"

using namespace pxr::wl;

namespace {

struct Frame {
    int w = 0, h = 0;
    bool alpha = true;
    std::vector<uint8_t> bgra;  // premultiplied
    uint64_t updates = 0;
};

class DumpPresenter : public Presenter {
public:
    void update(uint32_t uid, int width, int height, int stride, bool hasAlpha, const uint8_t* pixels,
                int x, int y, int w, int h, bool full) override {
        Frame& f = frames[uid];
        if (full || f.w != width || f.h != height) {
            f.w = width;
            f.h = height;
            f.bgra.assign(static_cast<size_t>(width) * height * 4, 0);
        }
        f.alpha = hasAlpha;
        for (int row = y; row < y + h; ++row) {
            memcpy(&f.bgra[(static_cast<size_t>(row) * width + x) * 4],
                   pixels + static_cast<size_t>(row) * stride + static_cast<size_t>(x) * 4,
                   static_cast<size_t>(w) * 4);
        }
        ++f.updates;
    }
    void remove(uint32_t uid) override { frames.erase(uid); }
    std::map<uint32_t, Frame> frames;
};

void put32(std::string& s, uint32_t v) {
    for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::string raw;
    raw.reserve(static_cast<size_t>(h) * (w * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.append(reinterpret_cast<const char*>(&rgb[static_cast<size_t>(y) * w * 3]), static_cast<size_t>(w) * 3);
    }
    uLongf zlen = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        return false;
    }
    std::string out("\x89PNG\r\n\x1a\n", 8);
    auto chunk = [&](const char* type, const std::string& data) {
        put32(out, static_cast<uint32_t>(data.size()));
        std::string body = std::string(type, 4) + data;
        out += body;
        put32(out, static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()))));
    };
    std::string ihdr;
    put32(ihdr, static_cast<uint32_t>(w));
    put32(ihdr, static_cast<uint32_t>(h));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5);
    chunk("IHDR", ihdr);
    chunk("IDAT", std::string(reinterpret_cast<char*>(z.data()), zlen));
    chunk("IEND", "");
    std::ofstream f(path, std::ios::binary);
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    return f.good();
}

double now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = "/tmp/wl", script;
    int scale = 2;
    bool trace = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = atoi(argv[++i]);
        else if (a == "--trace") trace = true;
        else script = a;
    }
    signal(SIGPIPE, SIG_IGN);

    DumpPresenter presenter;
    Server server;
    server.setPresenter(&presenter);
    server.setTrace(trace);
    server.setOutput(1280, 800, scale);
    if (!server.start(dir, "wayland-0")) return 1;

    std::vector<std::string> lines;
    {
        std::ifstream f(script);
        for (std::string l; std::getline(f, l);) {
            if (!l.empty() && l[0] != '#') lines.push_back(l);
        }
    }

    uint32_t current = 0;     // the toplevel commands act on
    uint64_t seenRevision = 0;
    size_t next = 0;
    double waitUntil = 0;
    bool quit = false;
    uint32_t lastMapped = 0;

    auto toplevel = [&]() -> Toplevel* { return current ? server.findToplevel(current) : nullptr; };

    auto dump = [&](const std::string& path) {
        Toplevel* t = toplevel();
        if (!t || !t->mapped) {
            fprintf(stderr, "dump: no mapped toplevel\n");
            return;
        }
        std::vector<Visual> vis;
        server.visuals(t->uid, vis);
        const int sc = std::max(1, t->scale);
        int W = t->bufferW, H = t->bufferH;
        for (const Visual& v : vis) {
            W = std::max(W, static_cast<int>((v.x + v.w) * sc));
            H = std::max(H, static_cast<int>((v.y + v.h) * sc));
        }
        std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const uint8_t g = ((x / 16 + y / 16) & 1) ? 0x60 : 0x70;  // checkerboard shows transparency
                uint8_t* p = &rgb[(static_cast<size_t>(y) * W + x) * 3];
                p[0] = p[1] = p[2] = g;
            }
        }
        for (const Visual& v : vis) {
            auto it = presenter.frames.find(v.surfaceUid);
            if (it == presenter.frames.end()) continue;
            const Frame& f = it->second;
            const int ox = static_cast<int>(v.x * sc), oy = static_cast<int>(v.y * sc);
            for (int y = 0; y < f.h; ++y) {
                for (int x = 0; x < f.w; ++x) {
                    const int dx = ox + x, dy = oy + y;
                    if (dx < 0 || dy < 0 || dx >= W || dy >= H) continue;
                    const uint8_t* s = &f.bgra[(static_cast<size_t>(y) * f.w + x) * 4];
                    uint8_t* d = &rgb[(static_cast<size_t>(dy) * W + dx) * 3];
                    const int a = f.alpha ? s[3] : 255;
                    d[0] = static_cast<uint8_t>(s[2] + d[0] * (255 - a) / 255);
                    d[1] = static_cast<uint8_t>(s[1] + d[1] * (255 - a) / 255);
                    d[2] = static_cast<uint8_t>(s[0] + d[2] * (255 - a) / 255);
                }
            }
        }
        fprintf(stderr, "dump %s: %dx%d (%zu surfaces)\n", path.c_str(), W, H, vis.size());
        writePng(path, W, H, rgb);
    };

    static const std::map<char, int> keys = {
        {'q', 16}, {'w', 17}, {'e', 18}, {'r', 19}, {'t', 20}, {'y', 21}, {'u', 22}, {'i', 23},
        {'o', 24}, {'p', 25}, {'a', 30}, {'s', 31}, {'d', 32}, {'f', 33}, {'g', 34}, {'h', 35},
        {'j', 36}, {'k', 37}, {'l', 38}, {'z', 44}, {'x', 45}, {'c', 46}, {'v', 47}, {'b', 48},
        {'n', 49}, {'m', 50}, {'1', 2},  {'2', 3},  {'3', 4},  {'4', 5},  {'5', 6},  {'6', 7},
        {'7', 8},  {'8', 9},  {'9', 10}, {'0', 11}, {' ', 57}, {'\n', 28}, {'-', 12}, {'.', 52},
        {'/', 53}};

    const double start = now();
    while (!quit) {
        server.dispatch();
        server.frameDone();

        if (server.revision() != seenRevision) {
            seenRevision = server.revision();
            for (Toplevel* t : server.toplevels()) {
                if (t->mapped && t->uid != lastMapped && t->uid > lastMapped) {
                    lastMapped = t->uid;
                    current = t->uid;
                    fprintf(stderr, "toplevel %u mapped: \"%s\" app_id=\"%s\" %dx%d px scale %d\n", t->uid,
                            t->title.c_str(), t->appId.c_str(), t->bufferW, t->bufferH, t->scale);
                }
            }
        }

        if (now() >= waitUntil && next < lines.size()) {
            std::istringstream is(lines[next++]);
            std::string cmd;
            is >> cmd;
            if (cmd == "wait") {
                double s = 0;
                is >> s;
                waitUntil = now() + s;
            } else if (cmd == "dump") {
                std::string p;
                is >> p;
                dump(p);
            } else if (cmd == "list") {
                for (Toplevel* t : server.toplevels()) {
                    fprintf(stderr, "toplevel %u \"%s\" app_id=\"%s\" mapped=%d %dx%d logical, min %dx%d\n",
                            t->uid, t->title.c_str(), t->appId.c_str(), t->mapped, t->width, t->height,
                            t->minW, t->minH);
                }
            } else if (cmd == "focus") {
                server.setKeyboardFocus(current);
            } else if (cmd == "configure") {
                int w = 0, h = 0;
                is >> w >> h;
                server.configureToplevel(current, w, h, true, true);
            } else if (cmd == "motion") {
                double x = 0, y = 0;
                is >> x >> y;
                server.pointerMotion(current, x, y);
            } else if (cmd == "button") {
                int c = 0, d = 0;
                is >> c >> d;
                server.pointerButton(static_cast<uint32_t>(c), d != 0);
            } else if (cmd == "key") {
                int c = 0, d = 0;
                is >> c >> d;
                server.key(static_cast<uint32_t>(c), d != 0);
            } else if (cmd == "type") {
                std::string text;
                std::getline(is >> std::ws, text);
                for (char ch : text) {
                    auto it = keys.find(ch);
                    if (it == keys.end()) continue;
                    server.key(static_cast<uint32_t>(it->second), true);
                    server.dispatch();
                    server.key(static_cast<uint32_t>(it->second), false);
                    server.dispatch();
                }
            } else if (cmd == "axis") {
                double v = 0;
                is >> v;
                server.pointerAxis(v);
            } else if (cmd == "dismiss") {
                server.dismissPopups(current);
            } else if (cmd == "close") {
                server.requestClose(current);
            } else if (cmd == "scale") {
                int s = 2;
                is >> s;
                server.setOutput(1280, 800, s);
            } else if (cmd == "quit") {
                quit = true;
            }
        }
        if (now() - start > 120) {
            fprintf(stderr, "harness timeout\n");
            break;
        }
        usleep(4000);
    }
    server.stop();
    return 0;
}

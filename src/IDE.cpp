// =============================================================================
// IDE.cpp  --  Simple++ IDE
// A Processing-style creative coding IDE built with the Processing.h API.
// =============================================================================

#include "Processing.h"
#include "Platform.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <mutex>
#include <atomic>

// On Linux, guard POSIX-only headers
#ifdef PLAT_LINUX
#  include <dirent.h>
#  include <sys/stat.h>
#  include <errno.h>
#endif

namespace Processing {

// =============================================================================
// LAYOUT CONSTANTS
// =============================================================================

static const int MENUBAR_H     = 26;
static const int TOOLBAR_H     = 40;
static const int STATUS_H      = 20;
static const int GUTTER_W      = 56;
static const int TAB_H         = 22;

static int  CONSOLE_H          = 170;
static const int CONSOLE_H_MIN = 60;
static const int CONSOLE_H_MAX = 600;

static int  SIDEBAR_W          = 200;
static const int SIDEBAR_W_MIN = 120;
static const int SIDEBAR_W_MAX = 400;

static int  TERM_SIDE_W           = 340;
static const int TERM_SIDE_W_MIN  = 180;
static const int TERM_SIDE_W_MAX  = 700;

enum class TermPos { Bottom, Right };
static TermPos terminalPos = TermPos::Bottom;

static bool sidebarVisible = true;

// Font sizes (adjustable with Ctrl+= / Ctrl+-)
static float FS  = 14.0f;   // editor
static float FSS = 13.0f;   // console / toolbar
static float FST = 12.0f;   // menus / status

// Derived layout helpers
static int   sbW()         { return sidebarVisible ? SIDEBAR_W : 0; }
static float lineH()       { return FS * 1.6f; }
static int   editorX()     { return sbW() + GUTTER_W; }
static int   editorFullW() { return (terminalPos == TermPos::Right) ? width - sbW() - TERM_SIDE_W : width - sbW(); }
static int   editorY()     { return MENUBAR_H + TOOLBAR_H; }
static int   editorH()     { return (terminalPos == TermPos::Bottom) ? height - editorY() - STATUS_H - CONSOLE_H : height - editorY() - STATUS_H; }
static int   statusY()     { return editorY() + editorH(); }
static int   consoleY()    { return (terminalPos == TermPos::Bottom) ? statusY() + STATUS_H : editorY(); }
static int   consoleX()    { return (terminalPos == TermPos::Right)  ? width - TERM_SIDE_W : 0; }
static int   consoleW()    { return (terminalPos == TermPos::Right)  ? TERM_SIDE_W : width; }
static int   visLines()    { return std::max(1, (int)(editorH() / lineH())); }

// =============================================================================
// RESIZE DRAG STATE
// =============================================================================

static bool consoleResizing       = false;
static int  consoleResizeAnchorY  = 0;
static int  consoleResizeAnchorH  = 0;

static bool sidebarResizing       = false;
static int  sidebarResizeAnchorX  = 0;
static int  sidebarResizeAnchorW  = 0;

static bool termSideResizing      = false;
static int  termSideAnchorX       = 0;
static int  termSideAnchorW       = 0;

// =============================================================================
// FILE TREE (SIDEBAR)
// =============================================================================

struct FTEntry { std::string name; bool isDir; bool expanded; int depth; };
static std::vector<FTEntry> ftEntries;
static int   ftScroll = 0;
static std::string ftRoot = ".";

// List a directory -- returns {name, isDir} pairs
static std::vector<std::pair<std::string,bool>> listDir(const std::string& path) {
    std::vector<std::pair<std::string,bool>> out;
#ifdef PLAT_WINDOWS
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        if (!n.empty() && n[0] == '.') continue;
        out.push_back({ n, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        if (!n.empty() && n[0] == '.') continue;
        struct stat st;
        stat((path + "/" + n).c_str(), &st);
        out.push_back({ n, S_ISDIR(st.st_mode) != 0 });
    }
    closedir(d);
#endif
    return out;
}

static void populateTree() {
    ftEntries.clear();
    auto walk = [&](const std::string& path, int depth, auto& self) -> void {
        if (depth > 2) return;
        auto entries = listDir(path);
        std::vector<std::string> dirs, files;
        for (auto& [name, isDir] : entries) {
            if (isDir) dirs.push_back(name);
            else       files.push_back(name);
        }
        std::sort(dirs.begin(),  dirs.end());
        std::sort(files.begin(), files.end());
        for (auto& d : dirs) {
            bool exp = false;
            for (auto& e : ftEntries) if (e.name == d && e.isDir) { exp = e.expanded; break; }
            ftEntries.push_back({ d, true, exp, depth });
            if (exp) self(path + "/" + d, depth + 1, self);
        }
        for (auto& f : files)
            ftEntries.push_back({ f, false, false, depth });
    };
    walk(ftRoot, 0, walk);
}

// =============================================================================
// EDITOR STATE
// =============================================================================

static std::vector<std::string> code = {
    "// run once",
    "void setup() {",
    "  size(640, 360);",
    "}",
    "",
    "// loops forever",
    "void draw() {",
    "  background(102);",
    "  fill(255);",
    "  ellipse(mouseX, mouseY, 40, 40);",
    "}"
};

static int  curLine   = 0;
static int  curCol    = 0;
static int  selLine   = -1;
static int  selCol    = -1;
static int  scrollTop = 0;

// =============================================================================
// TERMINAL TABS
// =============================================================================

struct Terminal {
    std::string              name;
    std::vector<std::string> lines;
    int                      scroll   = 0;
    bool                     hasError = false;
};

static std::vector<Terminal> terminals = { { "Output", {}, 0, false } };
static int activeTab       = 0;
static int consoleSelLine  = -1;

// Convenience refs to tab 0 (build/run output)
static std::vector<std::string>& outLines  = terminals[0].lines;
static int&                      outScroll = terminals[0].scroll;
static bool&                     hasError  = terminals[0].hasError;

// =============================================================================
// SKETCH / BUILD STATE
// =============================================================================

static bool        modified    = false;
static std::string currentFile = "";
static std::string sketchBin   = "SketchApp";

static std::string getDefaultBuildFlags() {
#ifdef __APPLE__
    auto brewPrefix = [](const char* pkg) {
        char buf[256] = {};
        FILE* p = popen((std::string("brew --prefix ") + pkg + " 2>/dev/null").c_str(), "r");
        if (!p) return std::string();
        if (fgets(buf, sizeof(buf), p)) {}
        pclose(p);
        std::string s(buf);
        while (!s.empty() && (s.back()=='\n'||s.back()=='\r'||s.back()==' ')) s.pop_back();
        return s;
    };
    std::string glew = brewPrefix("glew"), glfw = brewPrefix("glfw"), flags;
    if (!glew.empty()) flags += "-I" + glew + "/include -L" + glew + "/lib ";
    if (!glfw.empty()) flags += "-I" + glfw + "/include -L" + glfw + "/lib ";
    flags += "-lglfw -lGLEW -framework OpenGL";
    return flags;
#elif defined(_WIN32)
    return "-lglfw3 -lglew32 -lopengl32 -lglu32 -lcomdlg32 -lshell32 -lole32 -luuid -mwindows -pthread -D_USE_MATH_DEFINES";
#else
    return "-lglfw -lGLEW -lGL -lGLU -lm -pthread";
#endif
}
static std::string buildFlags = getDefaultBuildFlags();

// Sketch process (pipe capture)
static plat_proc_t       sketchProc    = plat_proc_invalid();
static std::thread       sketchThread;
static std::mutex        outMutex;
static std::atomic<bool> sketchRunning { false };
static const int         WRAP_COLS = 120;

// =============================================================================
// UNDO / REDO
// =============================================================================

using Snapshot = std::pair<std::vector<std::string>, std::pair<int,int>>;
static std::vector<Snapshot> undoStack;
static std::vector<Snapshot> redoStack;

static void pushUndo() {
    undoStack.push_back({ code, { curLine, curCol } });
    if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
    redoStack.clear();
    modified = true;
}

// =============================================================================
// VIM MODE
// =============================================================================

static bool vimMode   = false;
static bool vimInsert = false;

enum class VimState { NORMAL, INSERT, VISUAL, VISUAL_LINE };
static VimState vimState        = VimState::NORMAL;
static int      vimAnchorLine   = 0;
static int      vimAnchorCol    = 0;
static std::string vimCmd       = "";

// =============================================================================
// MENU STATE
// =============================================================================

enum class Menu { None, File, Edit, Sketch, Tools, Libraries };
static Menu openMenu = Menu::None;

// =============================================================================
// SERIAL MONITOR
// =============================================================================

static bool              showSerial   = false;
static plat_serial_t     serialFd     = plat_serial_invalid();
static std::string       serialPort   = "";
static std::vector<std::string> serialLog;
static std::string       serialInput  = "";
static int               serialScroll = 0;

static const std::vector<int> BAUD_RATES = {
    300, 600, 1200, 2400, 4800, 9600,
    14400, 19200, 38400, 57600, 115200, 230400
};
static int baudIndex = 5; // default 9600

static void openSerial(const std::string& port, int baud) {
    if (plat_serial_ok(serialFd)) {
        plat_serial_close(serialFd);
        serialFd = plat_serial_invalid();
    }
    serialFd = plat_serial_open(port, baud);
    if (!plat_serial_ok(serialFd)) {
        serialLog.push_back("ERROR: cannot open " + port);
        return;
    }
    serialPort = port;
    serialLog.push_back("Connected: " + port + " @ " + std::to_string(baud) + " baud");
}

static void closeSerial() {
    if (plat_serial_ok(serialFd)) {
        plat_serial_close(serialFd);
        serialFd = plat_serial_invalid();
    }
    serialLog.push_back("Disconnected.");
    serialPort = "";
}

static void pollSerial() {
    if (!plat_serial_ok(serialFd)) return;
    char buf[256];
    int n;
    static std::string partial;
    while ((n = plat_serial_read(serialFd, buf, sizeof(buf)-1)) > 0) {
        buf[n] = 0;
        partial += buf;
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            serialLog.push_back(partial.substr(0, pos));
            partial = partial.substr(pos + 1);
            serialScroll = std::max(0, (int)serialLog.size() - 16);
        }
    }
}

static std::vector<std::string> listPorts() { return plat_list_ports(); }

// =============================================================================
// LIBRARY MANAGER
// =============================================================================

struct Library {
    std::string name, desc, pkg, header, installCmd, linkFlag;
    bool installed = false;
};

static std::vector<Library> libraries = {
    { "libserialport",  "Cross-platform serial (sigrok)",    "libserialport-dev",     "#include <libserialport.h>",             "", "-lserialport"  },
    { "Boost (Asio)",   "Boost.Asio serial_port",            "boost",                 "#include <boost/asio/serial_port.hpp>",  "", "-lboost_system"},
    { "Eigen",          "Linear algebra: matrices/vectors",  "libeigen3-dev",         "#include <Eigen/Dense>",                 "", ""              },
    { "glm",            "OpenGL math (vec3, mat4)",          "libglm-dev",            "#include <glm/glm.hpp>",                 "", ""              },
    { "Box2D",          "2D rigid body physics",             "libbox2d-dev",          "#include <box2d/box2d.h>",               "", "-lbox2d"       },
    { "FFTW3",          "Fast Fourier Transform",            "libfftw3-dev",          "#include <fftw3.h>",                     "", "-lfftw3"       },
    { "OpenCV",         "Computer vision",                   "libopencv-dev",         "#include <opencv2/opencv.hpp>",          "", "-lopencv_core" },
    { "PortAudio",      "Low-latency audio I/O",             "portaudio19-dev",       "#include <portaudio.h>",                 "", "-lportaudio"   },
    { "libcurl",        "HTTP/HTTPS requests",               "libcurl4-openssl-dev",  "#include <curl/curl.h>",                 "", "-lcurl"        },
    { "SQLite3",        "Embedded SQL database",             "libsqlite3-dev",        "#include <sqlite3.h>",                   "", "-lsqlite3"     },
    { "nlohmann/json",  "Header-only C++ JSON",              "",                      "#include \"json.hpp\"",
      "curl -sL https://github.com/nlohmann/json/releases/latest/download/json.hpp -o src/json.hpp", "" },
    { "stb_image",      "Header-only image loader",          "",                      "#include \"stb_image.h\"",
      "curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o src/stb_image.h", "" },
    { "Vim mode",       "Enable vim keybindings in editor",  "",                      "", "", "" },
};

static bool showLibMgr    = false;
static int  libScroll     = 0;
static int  installingLib = -1;
static std::string libStatus = "";

// Package manager helpers (Linux only)
#ifdef PLAT_LINUX
enum class PkgMgr { Unknown, Apt, Pacman, Dnf };
static PkgMgr detectPkgMgr() {
    if (system("command -v pacman  >/dev/null 2>&1") == 0) return PkgMgr::Pacman;
    if (system("command -v apt-get >/dev/null 2>&1") == 0) return PkgMgr::Apt;
    if (system("command -v dnf     >/dev/null 2>&1") == 0) return PkgMgr::Dnf;
    return PkgMgr::Unknown;
}
struct PkgMap { std::string apt, pacman, dnf; };
static const std::vector<PkgMap> PKG_MAP = {
    { "libserialport-dev",   "libserialport",  "libserialport-devel" },
    { "boost",               "boost",          "boost-devel"         },
    { "libeigen3-dev",       "eigen",          "eigen3-devel"        },
    { "libopencv-dev",       "opencv",         "opencv-devel"        },
    { "portaudio19-dev",     "portaudio",      "portaudio-devel"     },
    { "libcurl4-openssl-dev","curl",           "libcurl-devel"       },
    { "libglm-dev",          "glm",            "glm-devel"           },
    { "libbox2d-dev",        "box2d",          "box2d-devel"         },
    { "libsqlite3-dev",      "sqlite",         "sqlite-devel"        },
    { "libfftw3-dev",        "fftw",           "fftw-devel"          },
};
static std::string resolvePkg(const std::string& apt) {
    PkgMgr pm = detectPkgMgr();
    if (pm == PkgMgr::Apt) return apt;
    for (auto& p : PKG_MAP) {
        if (p.apt == apt) {
            if (pm == PkgMgr::Pacman) return p.pacman;
            if (pm == PkgMgr::Dnf)    return p.dnf;
        }
    }
    return apt;
}
static std::string buildInstallCmd(const std::string& pkg) {
    return plat_build_install_cmd(resolvePkg(pkg), pkg);
}
static bool isPkgInstalled(const std::string& pkg) {
    std::string p = resolvePkg(pkg);
    switch (detectPkgMgr()) {
        case PkgMgr::Pacman: return system(("pacman -Q " + p + " >/dev/null 2>&1").c_str()) == 0;
        case PkgMgr::Apt:    return system(("dpkg -s "   + p + " >/dev/null 2>&1").c_str()) == 0;
        case PkgMgr::Dnf:    return system(("rpm -q "    + p + " >/dev/null 2>&1").c_str()) == 0;
        default:             return false;
    }
}
#else
static std::string buildInstallCmd(const std::string& pkg) { return plat_build_install_cmd(pkg, pkg); }
static bool isPkgInstalled(const std::string&) { return false; }
#endif

static void checkInstalled() {
    for (auto& lib : libraries) {
        if (lib.pkg.empty()) {
            if (lib.name == "Vim mode") {
                lib.installed = vimMode;
            } else {
                // Header-only: check for file in src/
                std::string h = lib.header;
                size_t a = h.find('"'), b = h.rfind('"');
                if (a != std::string::npos && b != a)
                    lib.installed = plat_file_exists("src/" + h.substr(a+1, b-a-1));
            }
        } else {
            lib.installed = isPkgInstalled(lib.pkg);
        }
    }
}

// =============================================================================
// CURSOR / SELECTION HELPERS
// =============================================================================

static void clamp() {
    curLine = std::max(0, std::min(curLine, (int)code.size()-1));
    curCol  = std::max(0, std::min(curCol,  (int)code[curLine].size()));
}

static void ensureVis() {
    if (curLine < scrollTop) scrollTop = curLine;
    if (curLine >= scrollTop + visLines()) scrollTop = curLine - visLines() + 1;
    scrollTop = std::max(0, scrollTop);
}

static bool hasSel()   { return selLine >= 0; }
static void clearSel() { selLine = -1; selCol = -1; }

static void selRange(int& l0, int& c0, int& l1, int& c1) {
    if (selLine < curLine || (selLine == curLine && selCol <= curCol))
        { l0 = selLine; c0 = selCol; l1 = curLine; c1 = curCol; }
    else
        { l0 = curLine; c0 = curCol; l1 = selLine; c1 = selCol; }
}

static std::string getSelected() {
    if (!hasSel()) return "";
    int l0, c0, l1, c1;
    selRange(l0, c0, l1, c1);
    if (l0 == l1) return code[l0].substr(c0, c1 - c0);
    std::string s = code[l0].substr(c0) + "\n";
    for (int l = l0+1; l < l1; l++) s += code[l] + "\n";
    s += code[l1].substr(0, c1);
    return s;
}

static void deleteSel() {
    if (!hasSel()) return;
    int l0, c0, l1, c1;
    selRange(l0, c0, l1, c1);
    if (l0 == l1) {
        code[l0].erase(c0, c1 - c0);
    } else {
        code[l0] = code[l0].substr(0, c0) + code[l1].substr(c1);
        code.erase(code.begin() + l0 + 1, code.begin() + l1 + 1);
    }
    curLine = l0; curCol = c0;
    clearSel();
}

// =============================================================================
// FILE OPERATIONS
// =============================================================================

static void newFile() {
    code = {
        "// run once",
        "void setup() {",
        "  size(640, 360);",
        "}",
        "",
        "// loops forever",
        "void draw() {",
        "  background(102);",
        "  fill(255);",
        "  ellipse(mouseX, mouseY, 40, 40);",
        "}"
    };
    curLine = curCol = scrollTop = 0;
    clearSel();
    undoStack.clear();
    redoStack.clear();
    currentFile = "";
    sketchBin   = "SketchApp";
    modified    = false;
    outLines.push_back("New sketch.");
}

static void saveFile(const std::string& path) {
    std::string p = path;
    if (p.size() < 4 || p.substr(p.size()-4) != ".cpp") p += ".cpp";
    std::ofstream f(p);
    if (!f) { outLines.push_back("ERROR: cannot save " + p); return; }
    for (auto& l : code) f << l << "\n";
    currentFile = p;
    modified    = false;
    outLines.push_back("Saved: " + p);
    outScroll = std::max(0, (int)outLines.size() - 8);
}

static void openFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { outLines.push_back("ERROR: cannot open " + path); return; }
    code.clear();
    std::string l;
    while (std::getline(f, l)) code.push_back(l);
    if (code.empty()) code.push_back("");
    curLine = curCol = scrollTop = 0;
    clearSel();
    undoStack.clear();
    redoStack.clear();
    currentFile = path;
    modified    = false;
    outLines.push_back("Opened: " + path);
}

// File picker state (used as fallback when system dialog unavailable)
static bool fpShow  = false;
static bool fpSave  = false;
static std::string fpInput = "";

// Opens the OS native file picker dialog.
// Returns the chosen path, or "" if cancelled.
static std::string sysFileDialog(bool save, const std::string& def = "") {
    return plat_file_dialog(save, def);
}

// Open or save using system dialog; fall back to inline picker if unsupported.
static void doOpen() {
    std::string path = plat_file_dialog(false, "");
    if (!path.empty()) openFile(path);
    else { fpShow=true; fpSave=false; fpInput=""; }  // fallback
}

static void doSaveAs(const std::string& def = "") {
    std::string path = plat_file_dialog(true, def);
    if (!path.empty()) saveFile(path);
    else { fpShow=true; fpSave=true; fpInput=def; }  // fallback
}

static void doSave() {
    if (currentFile.empty()) doSaveAs();
    else saveFile(currentFile);
}

static std::vector<std::string> listSketches() {
    return plat_list_sketches();
}

// =============================================================================
// SKETCH SANITIZER  (strips BOM / smart quotes before writing Sketch_run.cpp)
// =============================================================================

static std::string sanitizeLine(const std::string& s) {
    std::string out;
    size_t i = 0;
    // Strip UTF-8 BOM
    if (s.size() >= 3 &&
        (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF) i = 3;

    for (; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out += (char)c; continue; } // plain ASCII

        // Replace common typographic substitutions with ASCII
        if (c == 0xE2 && i + 2 < s.size()) {
            unsigned char b1 = (unsigned char)s[i+1];
            unsigned char b2 = (unsigned char)s[i+2];
            if (b1 == 0x80) {
                if      (b2==0x98||b2==0x99) { out += '\''; i+=2; } // curly single quote
                else if (b2==0x9C||b2==0x9D) { out += '"';  i+=2; } // curly double quote
                else if (b2==0x93||b2==0x94) { out += '-';  i+=2; } // en/em dash
                else                         {               i+=2; } // drop other
            } else { i += 2; }
        } else if (c >= 0xC0 && c <= 0xDF && i+1 < s.size()) { i += 1; }
        else if (c >= 0xF0 && i+3 < s.size())                { i += 3; }
        // else: drop stray high byte
    }
    return out;
}

static bool writeSketch() {
    std::ofstream f("src/Sketch_run.cpp");
    if (!f) {
        outLines.push_back("ERROR: cannot write src/Sketch_run.cpp");
        hasError = true;
        return false;
    }
    bool hasNS = false;
    for (auto& l : code)
        if (l.find("namespace Processing") != std::string::npos) { hasNS = true; break; }

    f << "#include \"Processing.h\"\n";
    if (!hasNS) f << "namespace Processing {\n";
    for (auto& l : code) f << sanitizeLine(l) << "\n";
    if (!hasNS) f << "} // namespace Processing\n";
    return true;
}

// =============================================================================
// SKETCH PROCESS (background output capture)
// =============================================================================

static void stopSketch() {
    sketchRunning = false;
    plat_proc_stop(sketchProc);
    if (sketchThread.joinable()) sketchThread.detach();
}

static void sketchReaderThread(int /*unused*/) {
    char buf[4096];
    std::string partial;

    auto pushLine = [&](std::string line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Hard-wrap long lines
        while ((int)line.size() > WRAP_COLS) {
            { std::lock_guard<std::mutex> lk(outMutex);
              outLines.push_back(line.substr(0, WRAP_COLS));
              outScroll = std::max(0, (int)outLines.size() - 14); }
            line = line.substr(WRAP_COLS);
        }
        if (!line.empty()) {
            std::lock_guard<std::mutex> lk(outMutex);
            outLines.push_back(line);
            outScroll = std::max(0, (int)outLines.size() - 14);
        }
    };

    while (sketchRunning.load()) {
        int n = plat_proc_read(sketchProc, buf, (int)sizeof(buf)-1);
        if (n > 0) {
            buf[n] = 0;
            partial += buf;
            size_t pos;
            while ((pos = partial.find('\n')) != std::string::npos) {
                pushLine(partial.substr(0, pos));
                partial = partial.substr(pos + 1);
            }
            // Flush partial line if it's getting long (handles print() spam)
            while ((int)partial.size() >= WRAP_COLS) {
                pushLine(partial.substr(0, WRAP_COLS));
                partial = partial.substr(WRAP_COLS);
            }
        } else if (n == 0) {
            break; // EOF
        }
        if (!plat_proc_running(sketchProc)) {
            // Drain remaining bytes
            while ((n = plat_proc_read(sketchProc, buf, sizeof(buf)-1)) > 0) {
                buf[n] = 0; partial += buf;
                size_t pos;
                while ((pos = partial.find('\n')) != std::string::npos) {
                    pushLine(partial.substr(0, pos));
                    partial = partial.substr(pos + 1);
                }
            }
            break;
        }
        plat_sleep_ms(8);
    }
    if (!partial.empty()) pushLine(partial);
    { std::lock_guard<std::mutex> lk(outMutex);
      outLines.push_back("-- sketch exited --");
      outScroll = std::max(0, (int)outLines.size() - 14); }
    sketchRunning = false;
}

// =============================================================================
// BUILD & RUN
// =============================================================================

static void doCompile() {
    outLines.clear();
    hasError  = false;
    outScroll = 0;

    if (!writeSketch()) return;

    // Derive binary name from current file ("MySketch.cpp" -> "MySketch")
    sketchBin = "SketchApp";
    if (!currentFile.empty()) {
        std::string base = currentFile;
        size_t sl = base.rfind('/');
        if (sl == std::string::npos) sl = base.rfind('\\');
        if (sl != std::string::npos) base = base.substr(sl + 1);
        if (base.size() > 4 && base.substr(base.size()-4) == ".cpp")
            base = base.substr(0, base.size()-4);
        sketchBin = base;
    }

#ifdef _WIN32
    std::string ext = ".exe";
#else
    std::string ext = "";
#endif

    std::string outBin = sketchBin + ext;

    // Wrap output path in quotes to handle filenames with spaces
    std::string cmd =
        "g++ -std=c++17"
        " src/Processing.cpp"
        " src/Sketch_run.cpp"
        " src/Processing_defaults.cpp"
        " src/main.cpp"
        " -o "" + outBin + """ +
        " " + buildFlags +
        " 2>&1";

    outLines.push_back("Building: " + sketchBin);
    outLines.push_back("$ " + cmd);

#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        outLines.push_back("ERROR: could not launch compiler -- is g++ in PATH?");
        hasError = true;
        return;
    }

    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string s(buf);
        while (!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back();
        if (s.find("error:") != std::string::npos) hasError = true;
        if (!s.empty()) outLines.push_back(s);
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    if (!hasError) {
        outLines.push_back("Built: " + outBin + "   (Ctrl+R to run)");
    } else {
        int errCount = 0, warnCount = 0;
        for (auto& ol : outLines) {
            if (ol.find("error:")   != std::string::npos) errCount++;
            if (ol.find("warning:") != std::string::npos) warnCount++;
        }
        outLines.push_back("Build failed: " + std::to_string(errCount) + " error(s)" +
            (warnCount > 0 ? ", " + std::to_string(warnCount) + " warning(s)" : ""));

        // Jump cursor to first error line
        for (auto& ol : outLines) {
            size_t pos = ol.find("Sketch_run.cpp:");
            if (pos != std::string::npos) {
                size_t p2 = pos + 15;
                int ln = 0;
                while (p2 < ol.size() && isdigit((unsigned char)ol[p2]))
                    ln = ln * 10 + (ol[p2++] - '0');
                if (ln > 0 && ln <= (int)code.size()) {
                    curLine = ln - 1; curCol = 0;
                    clamp(); ensureVis();
                }
                break;
            }
        }
    }
    outScroll = std::max(0, (int)outLines.size() - 10);
}

static void doRun() {
    doCompile();
    if (hasError) {
        outLines.push_back("Not running -- fix errors first.");
        outScroll = std::max(0, (int)outLines.size() - 10);
        return;
    }

    stopSketch();

#ifdef _WIN32
    std::string bin = sketchBin + ".exe";
#else
    std::string bin = "./" + sketchBin;
#endif

    if (!plat_file_exists(bin)) {
        outLines.push_back("ERROR: binary not found: " + bin);
        outScroll = std::max(0, (int)outLines.size() - 10);
        return;
    }

    sketchProc = plat_proc_start(bin);
    if (!plat_proc_ok(sketchProc)) {
        outLines.push_back("ERROR: failed to launch " + bin);
        outScroll = std::max(0, (int)outLines.size() - 10);
        return;
    }

    sketchRunning = true;
    { std::lock_guard<std::mutex> lk(outMutex);
      outLines.push_back("Running: " + bin);
      outLines.push_back("------------------------------------------------------"); }

    sketchThread = std::thread(sketchReaderThread, 0);
    sketchThread.detach();
    outScroll = std::max(0, (int)outLines.size() - 10);
}

static void doStop() {
    if (sketchRunning) {
        stopSketch();
        outLines.push_back("-- Sketch stopped --");
        outScroll = std::max(0, (int)outLines.size() - 10);
    }
}

// =============================================================================
// SYNTAX HIGHLIGHTING
// =============================================================================

static const std::vector<std::string> KEYWORDS = {
    // C++ keywords
    "void","int","float","double","bool","char","auto","long","short",
    "unsigned","signed","true","false","null","nullptr","return",
    "if","else","for","while","do","switch","case","break","continue",
    "default","new","delete","class","struct","template","typename",
    "namespace","static","const","constexpr","override","virtual",
    "public","private","protected","inline","extern","typedef","using",
    // Processing API
    "size","background","fill","stroke","noStroke","noFill",
    "ellipse","rect","circle","line","triangle","quad","point","arc",
    "bezier","curve","box","sphere","translate","rotate","scale",
    "rotateX","rotateY","rotateZ","shearX","shearY",
    "pushMatrix","popMatrix","push","pop",
    "beginShape","endShape","vertex","bezierVertex","curveVertex",
    "width","height","mouseX","mouseY","pmouseX","pmouseY",
    "isMousePressed","isKeyPressed","mouseButton","keyCode",
    "frameCount","frameRate","millis","second","minute","hour",
    "day","month","year","PI","TWO_PI","HALF_PI","QUARTER_PI",
    "map","constrain","lerp","norm","sqrt","sq","abs","pow",
    "floor","ceil","round","sin","cos","tan","asin","acos",
    "atan","atan2","degrees","radians","dist","mag",
    "noise","random","randomSeed","noiseSeed",
    "color","PVector","setup","draw",
    "text","textSize","textFont","textAlign","textWidth",
    "loadImage","image","tint","noTint","blendMode","colorMode",
    "lerpColor","red","green","blue","alpha","hue","saturation","brightness",
    "println","print","save","saveFrame","exit","loop","noLoop","redraw",
    "lights","noLights","ambientLight","directionalLight",
};

struct Tok { std::string s; int r, g, b; };

static std::vector<Tok> tokenize(const std::string& ln) {
    std::vector<Tok> out;
    int n = (int)ln.size(), i = 0;
    while (i < n) {
        // Line comment
        if (i+1 < n && ln[i] == '/' && ln[i+1] == '/') {
            out.push_back({ ln.substr(i), 106, 153, 85 });
            break;
        }
        // Preprocessor
        if (ln[i] == '#') {
            out.push_back({ ln.substr(i), 197, 134, 192 });
            break;
        }
        // String literal
        if (ln[i] == '"') {
            int j = i+1;
            while (j < n && ln[j] != '"') j++;
            out.push_back({ ln.substr(i, j-i+1), 206, 145, 120 });
            i = j+1; continue;
        }
        // Angle-bracket include
        if (ln[i] == '<' && i > 0) {
            int j = i+1;
            while (j < n && ln[j] != '>') j++;
            out.push_back({ ln.substr(i, j-i+1), 206, 145, 120 });
            i = j+1; continue;
        }
        // Char literal
        if (ln[i] == '\'' && i+2 < n) {
            out.push_back({ ln.substr(i, 3), 206, 145, 120 });
            i += 3; continue;
        }
        // Number
        if (isdigit((unsigned char)ln[i])) {
            int j = i;
            while (j < n && (isdigit((unsigned char)ln[j]) || ln[j]=='.'||ln[j]=='f'||ln[j]=='x')) j++;
            out.push_back({ ln.substr(i, j-i), 181, 206, 168 });
            i = j; continue;
        }
        // Identifier / keyword / function call
        if (isalpha((unsigned char)ln[i]) || ln[i] == '_') {
            int j = i;
            while (j < n && (isalnum((unsigned char)ln[j]) || ln[j]=='_')) j++;
            std::string w = ln.substr(i, j-i);
            bool kw = std::find(KEYWORDS.begin(), KEYWORDS.end(), w) != KEYWORDS.end();
            bool fn = (j < n && ln[j] == '(');
            if      (kw) out.push_back({ w, 112, 184, 255 });
            else if (fn) out.push_back({ w, 220, 220, 170 });
            else         out.push_back({ w, 156, 220, 254 });
            i = j; continue;
        }
        out.push_back({ std::string(1, ln[i]), 200, 200, 200 });
        i++;
    }
    return out;
}

// =============================================================================
// DRAW HELPERS
// =============================================================================

static void qFill(float x, float y, float w, float h, int r, int g, int b, int a=255) {
    glColor4ub(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x,   y);   glVertex2f(x+w, y);
    glVertex2f(x+w, y+h); glVertex2f(x,   y+h);
    glEnd();
}
static void qBorder(float x, float y, float w, float h, int r, int g, int b) {
    glColor4ub(r, g, b, 255); glLineWidth(1);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x,   y);   glVertex2f(x+w, y);
    glVertex2f(x+w, y+h); glVertex2f(x,   y+h);
    glEnd();
}
static void qLine(float x1, float y1, float x2, float y2, int r, int g, int b) {
    glColor4ub(r, g, b, 255); glLineWidth(1);
    glBegin(GL_LINES); glVertex2f(x1, y1); glVertex2f(x2, y2); glEnd();
}
static void iText(const std::string& s, float x, float y, int r, int g, int b, float sz) {
    textSize(sz); fill(r, g, b); noStroke(); text(s, x, y);
}
static float xOf(int ln, int col) {
    textSize(FS);
    if (col == 0) return (float)(sbW() + GUTTER_W + 4);
    return sbW() + GUTTER_W + 4 + textWidth(code[ln].substr(0, col));
}
static void setClip(const std::string& s) {
    auto* win = glfwGetCurrentContext();
    if (win && !s.empty()) glfwSetClipboardString(win, s.c_str());
}

// =============================================================================
// SIDEBAR DRAW
// =============================================================================

static void drawSidebar() {
    if (!sidebarVisible) return;
    int sw = SIDEBAR_W;

    qFill(0, editorY(), sw, height - editorY(), 26, 26, 26);
    qLine(sw, editorY(), sw, height, 50, 50, 50);

    // Resize handle (right edge)
    bool onEdge = mouseX >= sw-4 && mouseX <= sw+2 && mouseY >= editorY();
    qFill(sw-4, editorY(), 4, height - editorY(),
          (onEdge||sidebarResizing) ? 17 : 26,
          (onEdge||sidebarResizing) ? 108 : 27,
          (onEdge||sidebarResizing) ? 179 : 36);

    // Header
    qFill(0, editorY(), sw, 24, 32, 32, 32);
    iText("EXPLORER", 10, editorY()+17, 130, 135, 165, FST);

    // Open folder button
    float bx = sw-60, by = editorY()+4, bw = 54, bh = 16;
    bool bHov = mouseX>=bx && mouseX<=bx+bw && mouseY>=by && mouseY<=by+bh;
    qFill(bx, by, bw, bh, bHov?34:26, bHov?108:30, bHov?179:46);
    qBorder(bx, by, bw, bh, bHov?60:45, bHov?160:55, bHov?240:80);
    iText("Open..", bx+4, by+bh*0.82f, bHov?230:160, bHov?240:170, bHov?255:210, 10.0f);

    // Root label
    std::string rootLabel = ftRoot;
    if (rootLabel.size() > 22) rootLabel = ".." + rootLabel.substr(rootLabel.size()-20);
    iText(rootLabel, 8, editorY()+42, 150, 175, 210, FST);

    // File tree
    int treeTop = editorY() + 52;
    float rowH  = FSS * 1.7f;
    int   vis   = (int)((height - treeTop) / rowH);

    // Show hint when no folder opened yet
    if (ftEntries.empty()) {
        iText("Click Open to",  10, treeTop + rowH,      100, 130, 170, FST);
        iText("browse files.",  10, treeTop + rowH*2.2f, 100, 130, 170, FST);
    }

    ftScroll = std::max(0, std::min(ftScroll, std::max(0, (int)ftEntries.size() - vis)));
    for (int i = 0; i < vis; i++) {
        int fi = ftScroll + i;
        if (fi >= (int)ftEntries.size()) break;
        auto& fe = ftEntries[fi];
        float ry = treeTop + i * rowH;
        float rx = 8 + fe.depth * 12;
        bool hov = mouseX >= 0 && mouseX < sw-6 && mouseY >= ry && mouseY < ry+rowH;
        if (hov) qFill(0, ry, sw-6, rowH, 38, 38, 38);
        std::string icon = fe.isDir ? (fe.expanded ? "v " : "> ") : "  ";
        int ir = fe.isDir ? 200 : 160;
        int ig = fe.isDir ? 180 : 185;
        int ib = fe.isDir ? 100 : 210;
        iText(icon + fe.name, rx, ry + rowH*0.75f, ir, ig, ib, FST);
    }
}

// =============================================================================
// MENU BAR DRAW
// =============================================================================

struct MenuItem { std::string label, shortcut; };

static void drawDropdown(float mx, float my, const std::vector<MenuItem>& items) {
    float pw = 210, ph = items.size() * 22 + 8;
    qFill(mx, my, pw, ph, 45, 45, 45);
    qBorder(mx, my, pw, ph, 70, 70, 70);
    for (int i = 0; i < (int)items.size(); i++) {
        float ry = my + 4 + i * 22;
        if (items[i].label == "---") { qFill(mx+4, ry+10, pw-8, 1, 65, 65, 65); continue; }
        bool hov = mouseX>=mx && mouseX<=mx+pw && mouseY>=ry && mouseY<=ry+22;
        if (hov) qFill(mx+1, ry, pw-2, 22, 17, 108, 179);
        iText(items[i].label, mx+10, ry+16, hov?255:215, hov?255:218, hov?255:228, FST);
        if (!items[i].shortcut.empty()) {
            textSize(FST);
            float sw = textWidth(items[i].shortcut);
            iText(items[i].shortcut, mx+pw-sw-8, ry+16, hov?220:120, hov?220:123, hov?220:145, FST);
        }
    }
}

static void drawMenuBar() {
    qFill(0, 0, width, MENUBAR_H, 36, 36, 36);
    qLine(0, MENUBAR_H-1, width, MENUBAR_H-1, 58, 58, 58);

    struct Head { std::string label; Menu id; float x=0, w=0; };
    std::vector<Head> heads = {
        {"File",Menu::File}, {"Edit",Menu::Edit},
        {"Sketch",Menu::Sketch}, {"Tools",Menu::Tools},
        {"Libraries",Menu::Libraries}
    };
    textSize(FST);
    float mx = 6;
    for (auto& h : heads) { h.x = mx; h.w = textWidth(h.label)+14; mx += h.w+2; }
    for (auto& h : heads) {
        bool open = (openMenu == h.id);
        bool hov  = mouseX>=h.x-2 && mouseX<=h.x+h.w && mouseY>=0 && mouseY<MENUBAR_H;
        if (open || hov) qFill(h.x-2, 0, h.w, MENUBAR_H, 17, 108, 179);
        iText(h.label, h.x+4, MENUBAR_H*0.74f, 228, 230, 242, FST);
    }

    if (openMenu==Menu::File)
        drawDropdown(2, MENUBAR_H, {{"New","Ctrl+N"},{"Open...","Ctrl+O"},{"---",""},{"Save","Ctrl+S"},{"Save As...","Ctrl+Shift+S"},{"---",""},{"Exit",""}});
    else if (openMenu==Menu::Edit)
        drawDropdown(42, MENUBAR_H, {{"Undo","Ctrl+Z"},{"Redo","Ctrl+Y"},{"---",""},{"Cut","Ctrl+X"},{"Copy","Ctrl+C"},{"Paste","Ctrl+V"},{"---",""},{"Select All","Ctrl+A"},{"Duplicate Line","Ctrl+D"},{"---",""},{"Toggle Comment","Ctrl+/"},{"Auto Format","Ctrl+Shift+F"}});
    else if (openMenu==Menu::Sketch)
        drawDropdown(84, MENUBAR_H, {{"Build","Ctrl+B"},{"Run","Ctrl+R"},{"Stop","Ctrl+."},{"---",""},{"Show Folder",""},{"Export Binary",""}});
    else if (openMenu==Menu::Tools)
        drawDropdown(138, MENUBAR_H, {{"Serial Monitor","Ctrl+Shift+M"},{"---",""},{"Toggle Vim Mode","Ctrl+Shift+V"},{"Auto Format","Ctrl+Shift+F"},{"---",""},{"Increase Font","Ctrl+="},{"Decrease Font","Ctrl+-"}});
    else if (openMenu==Menu::Libraries)
        drawDropdown(182, MENUBAR_H, {{"Manage Libraries...","Ctrl+Shift+L"},{"---",""},{"Add #include",""}});
}

// =============================================================================
// TOOLBAR DRAW
// =============================================================================

static void drawToolbar() {
    int ty = MENUBAR_H;
    qFill(0, ty, width, TOOLBAR_H, 42, 42, 42);
    qLine(0, ty+TOOLBAR_H-1, width, ty+TOOLBAR_H-1, 58, 58, 58);

    // File title
    std::string title = currentFile.empty() ? "untitled" : currentFile;
    if (modified) title += " *";
    iText(title, 10, ty+TOOLBAR_H*0.68f, 170, 180, 220, FSS);

    // Vim badge
    if (vimMode) {
        std::string mode = (vimState == VimState::NORMAL) ? "NORMAL" :
                           (vimState == VimState::INSERT) ? "INSERT" : "VISUAL";
        int mr = (vimState==VimState::INSERT) ? 217 : 17;
        int mg = (vimState==VimState::INSERT) ? 119 : 108;
        int mb = (vimState==VimState::INSERT) ? 87  : 179;
        textSize(FST); float mw = textWidth(mode)+12;
        qFill(textWidth(title)+18, ty+8, mw, TOOLBAR_H-16, mr, mg, mb);
        iText(mode, textWidth(title)+24, ty+TOOLBAR_H*0.68f, 255, 255, 255, FST);
    }

    // Terminal dock toggle
    float tpx = width-300, tpy = ty+6, tpw = 86, tph = TOOLBAR_H-12;
    bool  tpH = mouseX>=tpx && mouseX<=tpx+tpw && mouseY>=tpy && mouseY<=tpy+tph;
    qFill(tpx, tpy, tpw, tph, tpH?44:30, tpH?46:32, tpH?62:46);
    qBorder(tpx, tpy, tpw, tph, 65, 65, 65);
    iText(terminalPos==TermPos::Right ? "[=]Bottom" : "[|]Right",
          tpx+6, tpy+tph*0.72f, 160, 170, 210, FST);

    // Build button
    float bx = width-196, by = ty+6, bh = TOOLBAR_H-12, bw = 92;
    bool  bH = mouseX>=bx && mouseX<=bx+bw && mouseY>=by && mouseY<=by+bh;
    qFill(bx, by, bw, bh, bH?217:160, bH?119:80, bH?87:55);
    qBorder(bx, by, bw, bh, bH?217:180, bH?119:90, bH?87:55);
    iText("Build", bx+10, by+bh*0.72f, 255, 235, 225, FSS);

    // Run button
    float rx = width-96;
    bool  rH = mouseX>=rx && mouseX<=rx+bw && mouseY>=by && mouseY<=by+bh;
    qFill(rx, by, bw, bh, rH?25:17, rH?130:108, rH?210:179);
    qBorder(rx, by, bw, bh, rH?70:40, rH?160:120, rH?220:180);
    iText("Run", rx+14, by+bh*0.72f, 230, 240, 255, FSS);

    // Status dot
    if (sketchRunning)
        qFill(width-210, by+bh/2-5, 10, 10, 255, 160, 0);
    else if (hasError)
        qFill(width-210, by+bh/2-5, 10, 10, 220, 60, 60);
    else if (!outLines.empty() && outLines.back().find("Built:") != std::string::npos)
        qFill(width-210, by+bh/2-5, 10, 10, 60, 200, 60);
}

// =============================================================================
// STATUS BAR DRAW
// =============================================================================

static void drawStatus() {
    int sy = statusY();
    qFill(0, sy, width, STATUS_H, 30, 30, 30);
    qLine(0, sy, width, sy, 55, 55, 55);

    std::string s = "Ln " + std::to_string(curLine+1) +
                    "  Col " + std::to_string(curCol+1) +
                    "  |  " + std::to_string((int)code.size()) + " lines";
    if (!currentFile.empty()) s += "  |  " + currentFile;
    if (!sketchBin.empty())   s += "  >  ./" + sketchBin;
    iText(s, 8, sy+STATUS_H*0.76f, 125, 130, 158, FST);

    std::string right = (plat_serial_ok(serialFd) ? "* " + serialPort : "No port") + "  UTF-8  C++17";
    textSize(FST);
    iText(right, width - textWidth(right) - 8, sy+STATUS_H*0.76f,
          plat_serial_ok(serialFd)?80:90,
          plat_serial_ok(serialFd)?210:95,
          plat_serial_ok(serialFd)?80:110, FST);
}

// =============================================================================
// EDITOR DRAW
// =============================================================================

static void drawEditor() {
    int   ey  = editorY(), eh = editorH();
    float lh  = lineH(), asc = FS * 0.80f;
    int   ex  = sbW(), ew = editorFullW();

    qFill(ex, ey, ew, eh, 30, 30, 30);          // editor bg
    qFill(ex, ey, GUTTER_W, eh, 38, 38, 38);    // gutter bg
    qLine(ex+GUTTER_W, ey, ex+GUTTER_W, ey+eh, 58, 58, 58);

    int vis = visLines();
    for (int i = 0; i < vis; i++) {
        int   li      = scrollTop + i;
        if (li >= (int)code.size()) break;
        float rowTop  = ey + i * lh;
        float baseline= rowTop + asc + 2;

        // Current line highlight
        if (li == curLine)
            qFill(ex+GUTTER_W, rowTop, ew-GUTTER_W, lh, 44, 44, 50);

        // Selection highlight
        if (hasSel()) {
            int l0, c0, l1, c1;
            selRange(l0, c0, l1, c1);
            if (li >= l0 && li <= l1) {
                float sx  = (li == l0) ? xOf(li, c0) : (float)(ex+GUTTER_W+4);
                float ex2 = (li == l1) ? xOf(li, c1) : (float)(ex+ew-8);
                if (ex2 > sx) qFill(sx, rowTop+1, ex2-sx, lh-2, 17, 60, 110, 210);
            }
        }

        // Line number
        textSize(FST);
        std::string num = std::to_string(li+1);
        float nw = textWidth(num);
        if (li == curLine) iText(num, ex+GUTTER_W-6-nw, baseline-1, 200, 204, 215, FST);
        else               iText(num, ex+GUTTER_W-6-nw, baseline-1,  85,  90, 112, FST);

        // Syntax-highlighted tokens
        textSize(FS);
        float tx = ex + GUTTER_W + 4;
        for (auto& tok : tokenize(code[li])) {
            fill(tok.r, tok.g, tok.b); noStroke();
            text(tok.s, tx, baseline);
            tx += textWidth(tok.s);
        }

        // Cursor
        if (li == curLine) {
            float cx = xOf(li, curCol);
            float cw = (curCol < (int)code[li].size()) ? textWidth(std::string(1, code[li][curCol])) : FS*0.55f;
            if (vimState == VimState::NORMAL)
                qFill(cx, rowTop+1, cw, lh-2, 200, 190, 100, 200);
            else if ((frameCount/22) % 2 == 0)
                qFill(cx, rowTop+2, 2, lh-4, 220, 210, 160);
        }
    }

    // Minimap (right edge)
    static const int MM_W = 60;
    float mmx = (float)(ex + ew - MM_W - 8);
    qFill(mmx, ey, MM_W, eh, 24, 24, 24);
    qLine(mmx, ey, mmx, ey+eh, 48, 50, 62);
    if (!code.empty()) {
        float vpY = ey + (float)scrollTop / code.size() * eh;
        float vpH = std::max(8.0f, (float)vis / code.size() * eh);
        qFill(mmx+1, vpY, MM_W-2, vpH, 60, 80, 130, 120);
        for (int li = 0; li < (int)code.size(); li++) {
            float ly  = ey + (float)li / code.size() * eh;
            float lw2 = std::min((float)MM_W-4, (float)code[li].size() * 0.35f);
            if (lw2 > 0) qFill(mmx+2, ly, lw2, std::max(1.0f, (float)eh/code.size()), 55, 85, 120);
        }
    }

    // Vertical scrollbar
    qFill(ex+ew-8, ey, 8, eh, 36, 36, 36);
    if ((int)code.size() > vis) {
        float sbH = std::max(14.0f, (float)vis / code.size() * eh);
        float sbY = ey + (float)scrollTop / code.size() * eh;
        qFill(ex+ew-8, sbY, 8, sbH, 80, 80, 80);
    }
}

// =============================================================================
// CONSOLE DRAW
// =============================================================================

static void drawConsole() {
    // Snapshot output lines to avoid holding lock during render
    std::vector<std::string> snap;
    { std::lock_guard<std::mutex> lk(outMutex); snap = outLines; }

    // Auto-scroll while sketch is running
    if (sketchRunning) {
        float lh2 = FSS * 1.5f;
        int   vis = std::max(1, (int)((CONSOLE_H - 4 - TAB_H) / lh2));
        outScroll = std::max(0, (int)snap.size() - vis);
    }

    pollSerial();

    int   cy  = consoleY(), cx = consoleX(), cw = consoleW();
    int   consH = (terminalPos == TermPos::Right) ? height - editorY() : CONSOLE_H;
    float lh  = FSS * 1.5f;

    // Resize handle
    bool onHandle = (terminalPos == TermPos::Bottom)
        ? (mouseY >= cy && mouseY <= cy+4)
        : (mouseX >= cx && mouseX <= cx+4 && mouseY >= cy && mouseY <= cy+consH);
    int hc = (onHandle||consoleResizing||termSideResizing) ? 108 : 50;
    if (terminalPos == TermPos::Bottom)
        qFill(cx, cy, cw, 4, 17*(onHandle?1:0), hc, 179*(onHandle?1:0)+75*(onHandle?0:1));
    else
        qFill(cx, cy, 4, consH, 17*(onHandle?1:0), hc, 179*(onHandle?1:0)+75*(onHandle?0:1));

    qFill(cx+4, cy+4, cw-4, consH-4, 20, 20, 20);

    // Tab bar
    qFill(cx+4, cy+4, cw-4, TAB_H, 24, 24, 24);
    qLine(cx+4, cy+4+TAB_H, cx+cw, cy+4+TAB_H, 52, 52, 52);
    float tx = (float)(cx + 8);
    for (int i = 0; i < (int)terminals.size(); i++) {
        textSize(FST);
        float tw = textWidth(terminals[i].name) + 20;
        bool  isA = (i == activeTab);
        bool  tH  = mouseX>=tx && mouseX<=tx+tw && mouseY>=cy+4 && mouseY<=cy+4+TAB_H;
        qFill(tx, cy+4, tw, TAB_H, isA?32:(tH?30:22), isA?33:(tH?31:23), isA?46:(tH?44:32));
        if (isA) qLine(tx, cy+4+TAB_H-1, tx+tw, cy+4+TAB_H-1, 17, 108, 179);
        if (terminals[i].hasError)           qFill(tx+tw-10, cy+4+7, 6, 6, 220, 60, 60);
        else if (!terminals[i].lines.empty())qFill(tx+tw-10, cy+4+7, 6, 6, 55, 190, 55);
        fill(isA?220:140, isA?224:145, isA?240:165);
        text(terminals[i].name, tx+6, cy+4+TAB_H-6);
        tx += tw + 2;
    }
    // New tab (+) button
    bool ntH = mouseX>=tx && mouseX<=tx+20 && mouseY>=cy+4 && mouseY<=cy+4+TAB_H;
    qFill(tx, cy+4, 20, TAB_H, ntH?38:26, ntH?40:28, ntH?56:40);
    fill(ntH?200:120, ntH?210:130, ntH?255:180);
    text("+", tx+5, cy+4+TAB_H-6);

    // Running indicator
    if (sketchRunning)
        iText("* RUNNING", (float)(cx+cw-200), cy+4+TAB_H*0.75f, 80, 210, 100, FST);

    // Stop button
    if (sketchRunning) {
        float sx = (float)(cx+cw-168), sy2 = (float)(cy+3), sw2 = 60, sh = 16;
        bool sH = mouseX>=sx && mouseX<=sx+sw2 && mouseY>=sy2 && mouseY<=sy2+sh;
        qFill(sx, sy2, sw2, sh, sH?160:110, sH?40:30, sH?40:30);
        qBorder(sx, sy2, sw2, sh, 200, 60, 60);
        iText("Stop", sx+14, sy2+sh*0.82f, 255, 180, 180, 10.0f);
    }

    // Copy All button -- drawn BELOW the tab bar so its Y range doesn't
    // overlap the tab-bar click area (cy+4 .. cy+4+TAB_H).
    // Uses a fixed dark-blue background so it's always visible.
    float cbx = cx+cw-84;
    float cby = cy + 4 + TAB_H + 2;  // just below tab bar
    float cbw = 76;
    float cbh = 14;
    bool  cbH = mouseX>=cbx && mouseX<=cbx+cbw && mouseY>=cby && mouseY<=cby+cbh;
    // Background: always dark blue (visible even without hover)
    qFill(cbx, cby, cbw, cbh, 22, 28, 55);
    // Border: brighter blue on hover, dim otherwise
    qBorder(cbx, cby, cbw, cbh, cbH?80:45, cbH?130:65, cbH?220:120);
    // Text: always white-ish, brighter on hover
    iText("Copy All", cbx+6, cby+cbh*0.85f, cbH?240:190, cbH?245:198, cbH?255:220, 10.0f);

    // Output lines
    auto& tlines  = terminals[activeTab].lines;
    auto& tscroll = terminals[activeTab].scroll;
    int contentH  = consH - 4 - TAB_H;
    int visOut    = std::max(1, (int)(contentH / lh));
    tscroll = std::max(0, std::min(tscroll, std::max(0, (int)tlines.size() - visOut)));

    for (int i = 0; i < visOut; i++) {
        int li = tscroll + i;
        if (li >= (int)tlines.size()) break;
        auto& s = tlines[li];
        float rowTop  = cy + 4 + TAB_H + i * lh;
        float baseline= rowTop + lh - 3;

        bool hov = mouseX>cx+4 && mouseX<cx+cw-12 && mouseY>=rowTop && mouseY<rowTop+lh;
        bool sel = (li == consoleSelLine);
        if (sel)      qFill(cx+4, rowTop, cw-10, lh, 40, 60, 110);
        else if (hov) qFill(cx+4, rowTop, cw-10, lh, 30, 30, 30);

        // Colour-code by line type
        bool isErr  = s.find("error:")   != std::string::npos || s.find("X ") == 0;
        bool isWarn = s.find("warning:") != std::string::npos;
        bool isOk   = s.find("Built:")   != std::string::npos || s.find("OK ") == 0;
        bool isCmd  = !s.empty() && s[0] == '$';
        bool isSep  = !s.empty() && s[0] == '-';
        bool isBuild= isErr||isWarn||isOk||isCmd||isSep || s.find("Building:")!=std::string::npos;
        int r = 188, g = 192, b = 200;
        if      (isErr)  { r=255; g=90;  b=90;  }
        else if (isWarn) { r=255; g=190; b=50;  }
        else if (isOk)   { r=80;  g=215; b=100; }
        else if (isCmd)  { r=90;  g=170; b=255; }
        else if (isSep)  { r=55;  g=58;  b=72;  }
        else if (!isBuild){ r=228; g=220; b=160; } // sketch output -- warm yellow

        textSize(FSS);
        std::string disp = s;
        float maxW = (float)(cw - 30);
        if (textWidth(disp) > maxW) {
            int lo = 0, hi = (int)disp.size();
            while (lo < hi-1) {
                int mid = (lo+hi)/2;
                if (textWidth(disp.substr(0, mid)) < maxW) lo = mid; else hi = mid;
            }
            disp = disp.substr(0, lo);
            if (!disp.empty()) disp.back() = '>';
        }
        fill(r, g, b); noStroke(); text(disp, cx+14, baseline);
    }

    // Scrollbar
    if ((int)tlines.size() > visOut) {
        float th  = (float)contentH;
        float sbH = std::max(6.0f, (float)visOut / tlines.size() * th);
        float sbY = cy + 4 + TAB_H + (float)tscroll / tlines.size() * th;
        qFill(cx+cw-6, cy+4+TAB_H, 6, th, 38, 38, 38);
        qFill(cx+cw-6, sbY, 6, sbH, 80, 80, 80);
    }
}

// =============================================================================
// SERIAL MONITOR OVERLAY
// =============================================================================

static void drawSerialMonitor() {
    pollSerial();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    qFill(0, 0, width, height, 0, 0, 0, 175);

    float pw=640, ph=500, px=(width-pw)*0.5f, py=(height-ph)*0.5f;
    qFill(px, py, pw, ph, 26, 26, 26);
    qBorder(px, py, pw, ph, 65, 65, 65);
    qFill(px, py, pw, 36, 34, 34, 34);
    qLine(px, py+36, px+pw, py+36, 58, 58, 58);
    iText("Serial Monitor", px+14, py+25, 222, 228, 255, FS);

    // Close button
    bool xH = mouseX>=px+pw-32 && mouseX<=px+pw-6 && mouseY>=py+6 && mouseY<=py+30;
    qFill(px+pw-32, py+6, 26, 24, xH?190:78, 45, 52);
    iText("X", px+pw-24, py+23, 240, 242, 248, FSS);

    // Port selector
    float portY = py+46;
    qFill(px, portY, pw, 36, 30, 30, 30);
    iText("Port:", px+10, portY+24, 148, 155, 185, FST);
    auto ports = listPorts();
    float bx = px+54;
    if (ports.empty()) {
        qFill(bx, portY+6, 180, 24, 38, 40, 52);
        iText("No devices found", bx+8, portY+22, 130, 135, 160, FST);
    }
    for (int i = 0; i < (int)ports.size() && i < 4; i++) {
        float bw=148, bx2=bx+i*(bw+4);
        bool sel=(ports[i]==serialPort), hov=(mouseX>=bx2&&mouseX<=bx2+bw&&mouseY>=portY+6&&mouseY<=portY+30);
        qFill(bx2,portY+6,bw,24,sel?28:hov?38:26,sel?95:hov?42:28,sel?210:hov?58:40);
        qBorder(bx2,portY+6,bw,24,sel?60:50,sel?150:58,sel?240:78);
        iText(ports[i],bx2+8,portY+21,sel?220:190,sel?230:195,sel?255:220,FST);
    }

    // Baud selector
    float baudY = portY+42;
    qFill(px, baudY, pw, 36, 28, 28, 28);
    iText("Baud:", px+10, baudY+24, 148, 155, 185, FST);
    float bbx = px+54;
    for (int i = 0; i < (int)BAUD_RATES.size(); i++) {
        std::string bs = std::to_string(BAUD_RATES[i]);
        textSize(FST);
        float bw = textWidth(bs)+14;
        bool sel=(i==baudIndex), hov=(mouseX>=bbx&&mouseX<=bbx+bw&&mouseY>=baudY+6&&mouseY<=baudY+30);
        qFill(bbx,baudY+6,bw,24,sel?28:hov?36:22,sel?95:hov?42:26,sel?210:hov?55:38);
        iText(bs,bbx+7,baudY+21,sel?220:172,sel?230:176,sel?255:200,FST);
        bbx += bw+4;
    }

    // Connect/Disconnect button
    bool conn = plat_serial_ok(serialFd);
    float cbx2=px+pw-126, cby2=portY+6, cbw2=118, cbh2=24;
    bool cH=(mouseX>=cbx2&&mouseX<=cbx2+cbw2&&mouseY>=cby2&&mouseY<=cby2+cbh2);
    qFill(cbx2,cby2,cbw2,cbh2,conn?(cH?130:95):(cH?32:20),conn?(cH?45:35):(cH?128:96),conn?(cH?45:35):(cH?218:175));
    qBorder(cbx2,cby2,cbw2,cbh2,conn?150:45,conn?55:148,conn?55:240);
    iText(conn?"Disconnect":"Connect",cbx2+8,cby2+cbh2*0.76f,conn?255:210,conn?160:235,conn?160:255,FST);

    // Log area
    float logy=baudY+42, logh=ph-210;
    qFill(px,logy,pw,logh,16,16,16);
    qBorder(px,logy,pw,logh,50,50,50);
    float llh=FST*1.6f;
    int visLog=(int)((logh-8)/llh);
    serialScroll=std::max(0,std::min(serialScroll,std::max(0,(int)serialLog.size()-visLog)));
    for (int i=0;i<visLog;i++) {
        int li=serialScroll+i;
        if (li>=(int)serialLog.size()) break;
        auto& s=serialLog[li];
        float ry=logy+4+i*llh;
        bool isErr=s.find("ERROR")!=std::string::npos;
        bool isConn=s.find("Connected")!=std::string::npos||s.find("Disconnected")!=std::string::npos;
        bool isSend=s.size()>=2&&s[0]=='>'&&s[1]==' ';
        int r=182,g=186,b=194;
        if (isErr)  {r=255;g=100;b=100;}
        if (isConn) {r=80; g=200;b=100;}
        if (isSend) {r=100;g=180;b=255;}
        iText(s,px+8,ry+llh-3,r,g,b,FST);
    }

    // Send bar
    float sendY=logy+logh+4;
    qFill(px,sendY,pw,44,28,30,42);
    float inpW=pw-176;
    qFill(px+8,sendY+8,inpW,28,16,17,24);
    qBorder(px+8,sendY+8,inpW,28,conn?55:40,conn?85:44,conn?180:60);
    if (conn) { fill(215,220,238); textSize(FSS); text(serialInput+"_",px+14,sendY+26); }
    else iText("Connect first",px+14,sendY+26,90,95,120,FST);

    float sx2=px+pw-162, sw2=72;
    bool sbH2=(mouseX>=sx2&&mouseX<=sx2+sw2&&mouseY>=sendY+8&&mouseY<=sendY+36);
    qFill(sx2,sendY+8,sw2,28,sbH2?36:22,sbH2?110:78,sbH2?205:165);
    iText("Send",sx2+16,sendY+25,200,225,255,FSS);

    float cx2=px+pw-82, cw2=72;
    bool clH=(mouseX>=cx2&&mouseX<=cx2+cw2&&mouseY>=sendY+8&&mouseY<=sendY+36);
    qFill(cx2,sendY+8,cw2,28,clH?72:48,clH?38:28,clH?38:28);
    iText("Clear",cx2+12,sendY+25,225,175,175,FSS);
}

// =============================================================================
// LIBRARY MANAGER OVERLAY
// =============================================================================

static void drawLibMgr() {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    qFill(0, 0, width, height, 0, 0, 0, 175);

    float pw=660, ph=460, px=(width-pw)*0.5f, py=(height-ph)*0.5f;
    qFill(px,py,pw,ph,32,33,42);
    qBorder(px,py,pw,ph,72,75,92);
    qFill(px,py,pw,32,40,42,54);
    iText("Library Manager",px+12,py+23,228,232,255,FS);

    bool xH=mouseX>=px+pw-28&&mouseX<=px+pw-8&&mouseY>=py+6&&mouseY<=py+26;
    qFill(px+pw-28,py+6,20,20,xH?200:85,50,58);
    iText("X",px+pw-21,py+21,240,242,248,FSS);

    if (!libStatus.empty()) {
        qFill(px+4,py+36,pw-8,18,26,46,26);
        iText(libStatus,px+10,py+49,128,220,128,FST);
    }

    float ly=py+58, rowH=34;
    int visLib=(int)((ph-66)/rowH);
    libScroll=std::max(0,std::min(libScroll,std::max(0,(int)libraries.size()-visLib)));
    qFill(px,ly-2,pw,20,40,42,54);
    iText("Library",px+10,ly+13,148,152,178,FST);
    iText("Description",px+180,ly+13,148,152,178,FST);
    iText("Action",px+pw-108,ly+13,148,152,178,FST);
    ly += 20;

    for (int i=0;i<visLib;i++) {
        int li=libScroll+i;
        if (li>=(int)libraries.size()) break;
        auto& lib=libraries[li];
        float ry=ly+i*rowH;
        bool hov=mouseX>=px&&mouseX<=px+pw&&mouseY>=ry&&mouseY<=ry+rowH;
        qFill(px,ry,pw,rowH,hov?38:28,hov?40:29,hov?52:38);
        qLine(px,ry+rowH-1,px+pw,ry+rowH-1,48,50,62);

        std::string nm=lib.name; textSize(FST);
        while (nm.size()>1&&textWidth(nm)>165) nm.pop_back();
        iText(nm,px+10,ry+rowH*0.72f,208,212,238,FST);

        std::string desc=lib.desc;
        while (desc.size()>1&&textWidth(desc)>pw-300) desc.pop_back();
        iText(desc,px+180,ry+rowH*0.72f,152,155,178,FST);

        float bx2=px+pw-106, by2=ry+5, bw2=98, bh2=rowH-10;
        if (lib.name=="Vim mode") {
            qFill(bx2,by2,bw2,bh2,vimMode?30:25,vimMode?90:68,vimMode?40:155);
            iText(vimMode?"Enabled":"Enable",bx2+8,by2+bh2*0.72f,vimMode?140:200,vimMode?240:224,vimMode?140:255,FST);
        } else if (lib.installed) {
            bool aH=mouseX>=bx2&&mouseX<=bx2+bw2&&mouseY>=by2&&mouseY<=by2+bh2;
            qFill(bx2,by2,bw2,bh2,aH?38:28,aH?110:88,aH?40:32);
            qBorder(bx2,by2,bw2,bh2,48,158,58);
            iText("+include",bx2+8,by2+bh2*0.72f,136,238,148,FST);
        } else if (li==installingLib) {
            qFill(bx2,by2,bw2,bh2,96,78,18);
            iText("Installing..",bx2+4,by2+bh2*0.72f,238,208,98,FST);
        } else {
            bool iH=mouseX>=bx2&&mouseX<=bx2+bw2&&mouseY>=by2&&mouseY<=by2+bh2;
            qFill(bx2,by2,bw2,bh2,iH?38:22,iH?98:68,iH?198:158);
            iText("Install",bx2+12,by2+bh2*0.72f,198,222,255,FST);
        }
    }
}

// =============================================================================
// FILE PICKER (inline)
// =============================================================================


static void drawFilePicker() {
    int cy = consoleY();
    qFill(0,cy,width,CONSOLE_H,22,22,22);
    qFill(0,cy,width,3,17,108,179);
    iText(fpSave?"Save Sketch As:":"Open Sketch:",10,cy+20,188,198,238,FSS);
    qFill(10,cy+25,width-94,26,16,17,24);
    qBorder(10,cy+25,width-94,26,17,108,179);
    fill(220,224,240); textSize(FSS); text(fpInput+"_",16,cy+42);

    float bbx=width-78, bby=cy+25, bbw=68, bbh=26;
    bool bbH=mouseX>=bbx&&mouseX<=bbx+bbw&&mouseY>=bby&&mouseY<=bby+bbh;
    qFill(bbx,bby,bbw,bbh,bbH?38:22,bbH?98:68,bbH?198:158);
    iText("Browse..",bbx+4,bby+bbh*0.76f,198,218,255,FST);

    iText("Enter=confirm  Esc=cancel  or click:",10,cy+62,108,112,142,FST);
    auto files = listSketches();
    for (int i=0;i<(int)files.size()&&i<6;i++) {
        float ry=cy+74+i*19;
        bool hov=mouseX>=10&&mouseX<=380&&mouseY>=ry&&mouseY<ry+19;
        if (hov) qFill(8,ry,374,18,30,78,158);
        iText(files[i],14,ry+14,hov?240:158,hov?244:165,hov?255:198,FST);
    }
}

// =============================================================================
// SETUP / DRAW
// =============================================================================

void setup() {
    size(1080, 740);
    windowResizable(true);
    frameRate(60);
    windowTitle("Simple++");

    // Load window icon if present (icon.png or icon.jpg next to the exe)
    // stb_image is used to load the file; GLFW accepts RGBA arrays.
    {
        static const char* ICON_PATHS[] = { "icon.png","icon.jpg","icon.bmp",nullptr };
        for (int i=0; ICON_PATHS[i]; i++) {
            if (plat_file_exists(ICON_PATHS[i])) {
                PImage* img = loadImage(ICON_PATHS[i]);
                if (img && img->texID) {
                    // Convert ARGB pixels to RGBA for GLFW
                    std::vector<unsigned char> rgba(img->width * img->height * 4);
                    for (int p=0; p<img->width*img->height; p++) {
                        unsigned int px = img->pixels[p];
                        rgba[p*4+0] = (px>>16)&0xFF;
                        rgba[p*4+1] = (px>>8) &0xFF;
                        rgba[p*4+2] =  px     &0xFF;
                        rgba[p*4+3] = (px>>24)&0xFF;
                    }
                    GLFWimage glfwImg;
                    glfwImg.width  = img->width;
                    glfwImg.height = img->height;
                    glfwImg.pixels = rgba.data();
                    auto* win = glfwGetCurrentContext();
                    if (win) glfwSetWindowIcon(win, 1, &glfwImg);
                }
                break;
            }
        }
    }

    checkInstalled();
    // Tree is populated when user clicks "Open" -- not on startup
    outLines.push_back("Simple++ ready.");
    outLines.push_back("Ctrl+B build | Ctrl+R run | Ctrl+. stop | Ctrl+Shift+M serial | Ctrl+Shift+L libs");
}

void draw() {
    background(30, 30, 30);
    drawSidebar();
    if (!fpShow) { drawEditor(); drawStatus(); }
    drawConsole();
    if (fpShow)     drawFilePicker();
    if (showSerial) drawSerialMonitor();
    if (showLibMgr) drawLibMgr();
    drawToolbar();   // always on top
    drawMenuBar();
}

// =============================================================================
// MOUSE
// =============================================================================

static bool   dragging      = false;
static int    clickCount    = 0;
static double lastClickTime = 0.0;

static void mouseToLC(int& li, int& col) {
    float lh = lineH();
    li  = scrollTop + (int)((mouseY - editorY()) / lh);
    li  = std::max(0, std::min(li, (int)code.size()-1));
    textSize(FS);
    float tx = (float)(sbW() + GUTTER_W + 4);
    col = 0;
    for (int c = 0; c < (int)code[li].size(); c++) {
        float cw = textWidth(std::string(1, code[li][c]));
        if (tx + cw * 0.5f > mouseX) break;
        tx += cw; col = c+1;
    }
}

void mousePressed() {
    // --- Serial monitor ---
    if (showSerial) {
        float pw=640,ph=500,px=(width-pw)*0.5f,py=(height-ph)*0.5f;
        if (mouseX>=px+pw-32&&mouseX<=px+pw-6&&mouseY>=py+6&&mouseY<=py+30) { showSerial=false; return; }
        float portY=py+46;
        auto ports=listPorts(); float bx=px+54;
        for (int i=0;i<(int)ports.size()&&i<4;i++) {
            float bw=148,bx2=bx+i*(bw+4);
            if (mouseX>=bx2&&mouseX<=bx2+bw&&mouseY>=portY+6&&mouseY<=portY+30) { serialPort=ports[i]; return; }
        }
        float baudY=portY+42, bbx=px+54;
        for (int i=0;i<(int)BAUD_RATES.size();i++) {
            textSize(FST); float bw=textWidth(std::to_string(BAUD_RATES[i]))+14;
            if (mouseX>=bbx&&mouseX<=bbx+bw&&mouseY>=baudY+6&&mouseY<=baudY+30) { baudIndex=i; return; }
            bbx+=bw+4;
        }
        float cbx2=px+pw-126,cby2=portY+6,cbw2=118,cbh2=24;
        if (mouseX>=cbx2&&mouseX<=cbx2+cbw2&&mouseY>=cby2&&mouseY<=cby2+cbh2) {
            if (plat_serial_ok(serialFd)) closeSerial();
            else {
                auto p2 = serialPort.empty() ? (ports.empty() ? "" : ports[0]) : serialPort;
                if (!p2.empty()) openSerial(p2, BAUD_RATES[baudIndex]);
                else serialLog.push_back("ERROR: no port selected");
            }
            return;
        }
        float logy=baudY+42,logh=ph-210,sendY=logy+logh+4;
        float sx2=px+pw-162,cx2=px+pw-82,cw2=72;
        if (mouseX>=sx2&&mouseX<=sx2+72&&mouseY>=sendY+8&&mouseY<=sendY+36) {
            if (plat_serial_ok(serialFd)&&!serialInput.empty()) {
                std::string s=serialInput+"\n";
                plat_serial_write(serialFd,s.c_str(),(int)s.size());
                serialLog.push_back("> "+serialInput); serialInput="";
            }
            return;
        }
        if (mouseX>=cx2&&mouseX<=cx2+cw2&&mouseY>=sendY+8&&mouseY<=sendY+36) { serialLog.clear(); return; }
        return;
    }

    // --- Library manager ---
    if (showLibMgr) {
        float pw=660,ph=460,px=(width-pw)*0.5f,py=(height-ph)*0.5f;
        if (mouseX>=px+pw-28&&mouseX<=px+pw-8&&mouseY>=py+6&&mouseY<=py+26) { showLibMgr=false; return; }
        float ly=py+58+20, rowH=34;
        int visLib=(int)((ph-66)/rowH);
        for (int i=0;i<visLib;i++) {
            int li=libScroll+i; if(li>=(int)libraries.size()) break;
            auto& lib=libraries[li];
            float ry=ly+i*rowH, bx2=px+pw-106, by2=ry+5, bw2=98, bh2=rowH-10;
            if (mouseX>=bx2&&mouseX<=bx2+bw2&&mouseY>=by2&&mouseY<=by2+bh2) {
                if (lib.name=="Vim mode") { vimMode=!vimMode; vimInsert=false; vimState=VimState::NORMAL; lib.installed=vimMode; libStatus=vimMode?"Vim enabled":"Vim disabled"; return; }
                if (lib.installed) {
                    int ins=0; for(int ci=0;ci<(int)code.size();ci++) if(code[ci].find("#include")!=std::string::npos) ins=ci+1;
                    code.insert(code.begin()+ins, lib.header); modified=true; libStatus="Added: "+lib.header;
                } else {
                    installingLib=li; libStatus="Installing "+lib.name+"...";
                    std::string cmd=lib.pkg.empty()?lib.installCmd:buildInstallCmd(lib.pkg);
                    int ret=system(cmd.c_str()); lib.installed=(ret==0);
                    if (ret==0) { checkInstalled(); libStatus="Installed: "+lib.name; if(!lib.linkFlag.empty()&&buildFlags.find(lib.linkFlag)==std::string::npos) buildFlags+=" "+lib.linkFlag; }
                    else { libStatus="Failed: "+cmd; }
                    installingLib=-1;
                }
                return;
            }
        }
        return;
    }

    // --- Sidebar ---
    if (sidebarVisible && mouseX >= SIDEBAR_W-4 && mouseX <= SIDEBAR_W+2 && mouseY >= editorY()) {
        sidebarResizing=true; sidebarResizeAnchorX=mouseX; sidebarResizeAnchorW=SIDEBAR_W; return;
    }
    if (sidebarVisible && mouseX < SIDEBAR_W && mouseY >= editorY()+52) {
        float rowH=FSS*1.7f;
        int fi=ftScroll+(int)((mouseY-(editorY()+52))/rowH);
        if (fi>=0&&fi<(int)ftEntries.size()) {
            auto& fe=ftEntries[fi];
            if (fe.isDir) { fe.expanded=!fe.expanded; populateTree(); }
            else openFile(ftRoot+"/"+fe.name);
        }
        return;
    }
    // Open folder button in sidebar
    if (sidebarVisible && mouseX>=SIDEBAR_W-60 && mouseX<=SIDEBAR_W-6 && mouseY>=editorY()+4 && mouseY<=editorY()+20) {
        std::string chosen = plat_folder_dialog(ftRoot);
        if (!chosen.empty()) { ftRoot=chosen; ftEntries.clear(); populateTree(); }
        return;
    }

    // --- File picker ---
    if (fpShow) {
        int cy=consoleY();
        float bbx=width-78,bby=cy+25,bbw=68,bbh=26;
        if (mouseX>=bbx&&mouseX<=bbx+bbw&&mouseY>=bby&&mouseY<=bby+bbh) {
            if (fpSave) doSaveAs(fpInput); else doOpen();
            fpShow=false; return;
        }
        auto files=listSketches();
        for (int i=0;i<(int)files.size()&&i<6;i++) {
            float ry=cy+74+i*19;
            if (mouseX>=10&&mouseX<=380&&mouseY>=ry&&mouseY<ry+19) {
                if (fpSave) saveFile(files[i]); else openFile(files[i]);
                fpShow=false; return;
            }
        }
        return;
    }

    // --- Menu bar ---
    if (mouseY < MENUBAR_H) {
        struct MH { std::string label; Menu id; float x=0,w=0; };
        std::vector<MH> hs={{"File",Menu::File},{"Edit",Menu::Edit},{"Sketch",Menu::Sketch},{"Tools",Menu::Tools},{"Libraries",Menu::Libraries}};
        textSize(FST); float mx2=6;
        for (auto& h:hs){h.x=mx2;h.w=textWidth(h.label)+14;mx2+=h.w+2;}
        for (auto& h:hs) if(mouseX>=h.x-2&&mouseX<=h.x+h.w){openMenu=(openMenu==h.id)?Menu::None:h.id;return;}
        openMenu=Menu::None; return;
    }

    // --- Dropdown item clicks ---
    if (openMenu != Menu::None) {
        float mx=0; std::vector<MenuItem> items;
        if      (openMenu==Menu::File)      {mx=2;   items={{"New",""},{"Open...",""},{"---",""},{"Save",""},{"Save As...",""},{"---",""},{"Exit",""}};}
        else if (openMenu==Menu::Edit)      {mx=42;  items={{"Undo",""},{"Redo",""},{"---",""},{"Cut",""},{"Copy",""},{"Paste",""},{"---",""},{"Select All",""},{"Duplicate Line",""},{"---",""},{"Toggle Comment",""},{"Auto Format",""}};}
        else if (openMenu==Menu::Sketch)    {mx=84;  items={{"Build",""},{"Run",""},{"Stop",""},{"---",""},{"Show Folder",""},{"Export Binary",""}};}
        else if (openMenu==Menu::Tools)     {mx=138; items={{"Serial Monitor",""},{"---",""},{"Toggle Vim Mode",""},{"---",""},{"Auto Format",""},{"---",""},{"Increase Font",""},{"Decrease Font",""}};}
        else if (openMenu==Menu::Libraries) {mx=182; items={{"Manage Libraries...",""},{"---",""},{"Add #include",""}};}
        float my=MENUBAR_H, pw=210;
        for (int i=0;i<(int)items.size();i++) {
            float ry=my+4+i*22;
            if (mouseX>=mx&&mouseX<=mx+pw&&mouseY>=ry&&mouseY<=ry+22&&items[i].label!="---") {
                std::string lbl=items[i].label; openMenu=Menu::None;
                if      (lbl=="New")                newFile();
                else if (lbl=="Open...")            doOpen();
                else if (lbl=="Save")               doSave();
                else if (lbl=="Save As...")         doSaveAs(currentFile);
                else if (lbl=="Exit")               {stopSketch();exit_sketch();}
                else if (lbl=="Build")              doCompile();
                else if (lbl=="Run")                doRun();
                else if (lbl=="Stop")               doStop();
                else if (lbl=="Show Folder")        plat_open_folder(".");
                else if (lbl=="Toggle Vim Mode")    {vimMode=!vimMode;vimInsert=false;vimState=VimState::NORMAL;}
                else if (lbl=="Auto Format")        {} // handled in keyPressed
                else if (lbl=="Increase Font")      {FS=std::min(32.0f,FS+1);FSS=FS-1;FST=FS-2;}
                else if (lbl=="Decrease Font")      {FS=std::max(8.0f,FS-1);FSS=FS-1;FST=FS-2;}
                else if (lbl=="Serial Monitor")     {showSerial=true;checkInstalled();}
                else if (lbl=="Manage Libraries..."){showLibMgr=true;checkInstalled();}
                return;
            }
        }
        openMenu=Menu::None; return;
    }

    // --- Toolbar buttons ---
    int ty=MENUBAR_H; float by=ty+6, bh=TOOLBAR_H-12, bw=92;
    if (mouseY>=by&&mouseY<=by+bh) {
        if (mouseX>=width-196&&mouseX<=width-196+bw) { doCompile(); return; }
        if (mouseX>=width-96 &&mouseX<=width-96+bw)  { doRun();     return; }
        if (mouseX>=width-300&&mouseX<=width-214)    { terminalPos=(terminalPos==TermPos::Bottom)?TermPos::Right:TermPos::Bottom; return; }
    }

    // --- Console area ---
    {
        int cy=consoleY(), cx2=consoleX();

        // Resize handles
        if (terminalPos==TermPos::Bottom&&mouseY>=cy&&mouseY<=cy+4) { consoleResizing=true;consoleResizeAnchorY=mouseY;consoleResizeAnchorH=CONSOLE_H; return; }
        if (terminalPos==TermPos::Right&&mouseX>=cx2&&mouseX<=cx2+4&&mouseY>=cy&&mouseY<=cy+(height-editorY())) { termSideResizing=true;termSideAnchorX=mouseX;termSideAnchorW=TERM_SIDE_W; return; }

        // Stop button
        if (sketchRunning) {
            float sx=(float)(consoleX()+consoleW()-168), sy2=(float)(cy+3), sw2=60, sh=16;
            if (mouseX>=sx&&mouseX<=sx+sw2&&mouseY>=sy2&&mouseY<=sy2+sh) { doStop(); return; }
        }

        // Tab bar clicks
        if (mouseY>=cy+4&&mouseY<=cy+4+TAB_H) {
            float tx=(float)(consoleX()+8);
            for (int i=0;i<(int)terminals.size();i++) {
                textSize(FST); float tw=textWidth(terminals[i].name)+20;
                if (mouseX>=tx&&mouseX<=tx+tw) { activeTab=i; consoleSelLine=-1; return; }
                tx+=tw+2;
            }
            if (mouseX>=tx&&mouseX<=tx+20) { terminals.push_back({"Tab "+std::to_string(terminals.size()+1),{},0,false}); activeTab=(int)terminals.size()-1; return; }
            return;
        }

        // Copy All -- copies every line from every tab
        float cbx=(float)(consoleX()+consoleW()-84), cby2=(float)(cy+4+TAB_H+2), cbw2=76, cbh=14;
        if (mouseX>=cbx&&mouseX<=cbx+cbw2&&mouseY>=cby2&&mouseY<=cby2+cbh) {
            std::string all;
            { std::lock_guard<std::mutex> lk(outMutex);
              for (int t=0;t<(int)terminals.size();t++) {
                  if (terminals.size()>1) all += "--- " + terminals[t].name + " ---\n";
                  for (auto& l:terminals[t].lines) all+=l+"\n";
                  if (t+1<(int)terminals.size()) all+="\n";
              }
            }
            setClip(all);
            return;
        }

        // Line click -- copy line to clipboard
        if (mouseY>=cy+4+TAB_H&&mouseY<height) {
            float lh2=FSS*1.5f;
            int vi=(int)((mouseY-(cy+4+TAB_H))/lh2);
            auto& tlines=terminals[activeTab].lines;
            auto& tscroll=terminals[activeTab].scroll;
            int li=tscroll+vi;
            if (li>=0&&li<(int)tlines.size()) {
                consoleSelLine=li;
                std::lock_guard<std::mutex> lk(outMutex);
                setClip(tlines[li]);
            }
            return;
        }
    }

    // --- Editor clicks ---
    if (mouseY>=editorY()&&mouseY<editorY()+editorH()) {
        // Minimap click
        int ex2=sbW(), ew=editorFullW();
        float mmx=(float)(ex2+ew-60-8);
        if (mouseX>=mmx&&mouseX<=mmx+60) {
            float frac=(mouseY-editorY())/(float)editorH();
            curLine=(int)(frac*code.size());
            clamp(); ensureVis(); return;
        }

        // Click count detection
        double now=glfwGetTime();
        if (now-lastClickTime<0.35) clickCount++; else clickCount=1;
        lastClickTime=now;

        int li, col; mouseToLC(li, col);
        curLine = li;

        if (clickCount >= 3) {
            // Triple-click: select whole line
            selLine=li; selCol=0; curCol=(int)code[li].size(); dragging=false;
        } else if (clickCount == 2) {
            // Double-click: select word
            auto isW=[](char c){ return isalnum((unsigned char)c)||c=='_'; };
            int wl=col, wr=col;
            while (wl>0 && isW(code[li][wl-1])) wl--;
            while (wr<(int)code[li].size() && isW(code[li][wr])) wr++;
            curCol=wr; selLine=li; selCol=wl; dragging=false;
        } else {
            // Single-click
            curCol=col; selLine=li; selCol=col; dragging=true;
        }
        clamp();
    }
}

void mouseDragged() {
    if (consoleResizing) {
        int delta=consoleResizeAnchorY-mouseY;
        CONSOLE_H=std::max(CONSOLE_H_MIN,std::min(CONSOLE_H_MAX,consoleResizeAnchorH+delta));
        return;
    }
    if (termSideResizing) {
        int delta=termSideAnchorX-mouseX;
        TERM_SIDE_W=std::max(TERM_SIDE_W_MIN,std::min(TERM_SIDE_W_MAX,termSideAnchorW+delta));
        return;
    }
    if (sidebarResizing) {
        int delta=mouseX-sidebarResizeAnchorX;
        SIDEBAR_W=std::max(SIDEBAR_W_MIN,std::min(SIDEBAR_W_MAX,sidebarResizeAnchorW+delta));
        return;
    }
    if (!dragging) return;
    if (mouseY>=editorY()&&mouseY<editorY()+editorH()) {
        int li, col; mouseToLC(li, col);
        curLine=li; curCol=col; clamp(); ensureVis();
    }
}

void mouseReleased() {
    consoleResizing=false; termSideResizing=false; sidebarResizing=false; dragging=false;
    if (hasSel()&&selLine==curLine&&selCol==curCol) clearSel();
}

void mouseWheel(int delta) {
    auto* win=glfwGetCurrentContext();
    bool ctrl=win&&(glfwGetKey(win,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS);

    if (showLibMgr) { libScroll+=delta; return; }
    if (showSerial) { serialScroll+=delta; return; }
    if (ctrl) { FS=std::max(8.0f,std::min(32.0f,FS-(float)delta)); FSS=FS-1; FST=FS-2; return; }
    if (sidebarVisible&&mouseX<SIDEBAR_W) { ftScroll=std::max(0,ftScroll+delta); return; }
    if (mouseY>=editorY()&&mouseY<editorY()+editorH()) {
        scrollTop=std::max(0,std::min(scrollTop+delta*3,std::max(0,(int)code.size()-visLines()))); return;
    }
    if (mouseY>=consoleY()) {
        float lh2=FSS*1.5f; int vis=std::max(1,(int)((CONSOLE_H-4-TAB_H)/lh2));
        auto& ts=terminals[activeTab].scroll; auto& tl=terminals[activeTab].lines;
        ts=std::max(0,std::min(ts+delta*2,std::max(0,(int)tl.size()-vis)));
    }
}

// =============================================================================
// KEYBOARD
// =============================================================================

static void autoFormat() {
    pushUndo();
    int depth=0;
    for (auto& ln:code) {
        std::string t=ln;
        size_t sp=t.find_first_not_of(" \t");
        if (sp!=std::string::npos) t=t.substr(sp);
        if (!t.empty()&&t[0]=='}') depth=std::max(0,depth-1);
        ln=std::string(depth*2,' ')+t;
        for (char c:t) { if(c=='{')depth++;else if(c=='}')depth--; }
        depth=std::max(0,depth);
    }
}

void keyPressed() {
    auto* win=glfwGetCurrentContext();
    bool ctrl=win&&(glfwGetKey(win,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS);
    bool shift=win&&(glfwGetKey(win,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS);

    // --- Serial input ---
    if (showSerial) {
        if (keyCode==ESC) { showSerial=false; return; }
        if (keyCode==ENTER&&plat_serial_ok(serialFd)&&!serialInput.empty()) {
            std::string ts=serialInput+"\n"; plat_serial_write(serialFd,ts.c_str(),(int)ts.size());
            serialLog.push_back("> "+serialInput); serialInput="";
        }
        if (keyCode==BACKSPACE&&!serialInput.empty()) serialInput.pop_back();
        return;
    }

    // --- File picker input ---
    if (fpShow) {
        if (keyCode==ESC) { fpShow=false; return; }
        if (keyCode==ENTER&&!fpInput.empty()) { if(fpSave)saveFile(fpInput);else openFile(fpInput); fpShow=false; }
        if (keyCode==BACKSPACE&&!fpInput.empty()) fpInput.pop_back();
        return;
    }

    // --- ESC: close modals or exit vim modes ---
    if (keyCode==ESC) {
        if (vimMode&&vimState!=VimState::NORMAL) { vimState=VimState::NORMAL; vimInsert=false; clearSel(); clamp(); return; }
        if (showSerial||showLibMgr||fpShow) { showSerial=showLibMgr=fpShow=false; return; }
        if (openMenu!=Menu::None) { openMenu=Menu::None; return; }
        clearSel(); return;
    }

    // --- Ctrl+. stop sketch ---
    if (ctrl && keyCode==GLFW_KEY_PERIOD) { doStop(); return; }

    // --- Vim NORMAL mode ---
    if (vimMode && vimState==VimState::NORMAL) {
        static int vimN=0;
        if (key>='1'&&key<='9'&&vimN==0){vimN=key-'0';return;}
        if (key=='0'&&vimN>0){vimN*=10;return;}
        int n=(vimN>0)?vimN:1; vimN=0;

        // Visual mode ops
        bool isVis=(vimState==VimState::VISUAL||vimState==VimState::VISUAL_LINE);
        if (isVis) {
            if (key=='d'||key=='x'){pushUndo();setClip(getSelected());deleteSel();vimState=VimState::NORMAL;return;}
            if (key=='y'){setClip(getSelected());vimState=VimState::NORMAL;clearSel();return;}
            if (key=='c'){pushUndo();setClip(getSelected());deleteSel();vimState=VimState::INSERT;vimInsert=true;return;}
            if (key=='>'&&shift){pushUndo();int l0,c0,l1,c1;selRange(l0,c0,l1,c1);for(int l=l0;l<=l1;l++)code[l]="  "+code[l];vimState=VimState::NORMAL;return;}
            if (key=='<'){pushUndo();int l0,c0,l1,c1;selRange(l0,c0,l1,c1);for(int l=l0;l<=l1;l++)if(code[l].size()>=2&&code[l][0]==' '&&code[l][1]==' ')code[l].erase(0,2);vimState=VimState::NORMAL;return;}
        }

        // Motions
        if (keyCode==GLFW_KEY_H||keyCode==LEFT_KEY){for(int i=0;i<n;i++){if(curCol>0)curCol--;else if(curLine>0){curLine--;curCol=(int)code[curLine].size();}}clamp();return;}
        if (keyCode==GLFW_KEY_L||keyCode==RIGHT_KEY){for(int i=0;i<n;i++){if(curCol<(int)code[curLine].size())curCol++;else if(curLine<(int)code.size()-1){curLine++;curCol=0;}}clamp();return;}
        if (keyCode==GLFW_KEY_K||keyCode==UP){curLine-=n;clamp();ensureVis();return;}
        if (keyCode==GLFW_KEY_J||keyCode==DOWN){curLine+=n;clamp();ensureVis();return;}
        if (key=='0'){curCol=0;return;}
        if (key=='$'){curCol=(int)code[curLine].size();return;}
        if (key=='^'){curCol=0;while(curCol<(int)code[curLine].size()&&isspace((unsigned char)code[curLine][curCol]))curCol++;return;}
        if (key=='G'&&shift){curLine=(int)code.size()-1;curCol=0;ensureVis();return;}
        if (key=='w'){while(curCol<(int)code[curLine].size()&&!isspace((unsigned char)code[curLine][curCol]))curCol++;while(curCol<(int)code[curLine].size()&&isspace((unsigned char)code[curLine][curCol]))curCol++;clamp();return;}
        if (key=='b'){if(curCol==0&&curLine>0){curLine--;curCol=(int)code[curLine].size();}if(curCol>0)curCol--;while(curCol>0&&isspace((unsigned char)code[curLine][curCol-1]))curCol--;while(curCol>0&&!isspace((unsigned char)code[curLine][curCol-1]))curCol--;clamp();return;}
        if ((keyCode==GLFW_KEY_D&&ctrl)||(keyCode==GLFW_KEY_F&&ctrl)){curLine+=visLines()/2;clamp();ensureVis();return;}
        if ((keyCode==GLFW_KEY_U&&ctrl)||(keyCode==GLFW_KEY_B&&ctrl)){curLine-=visLines()/2;clamp();ensureVis();return;}

        // Enter insert mode
        if (key=='i'){vimState=VimState::INSERT;vimInsert=true;clearSel();return;}
        if (key=='I'){curCol=0;while(curCol<(int)code[curLine].size()&&isspace((unsigned char)code[curLine][curCol]))curCol++;vimState=VimState::INSERT;vimInsert=true;clearSel();return;}
        if (key=='a'){if(curCol<(int)code[curLine].size())curCol++;vimState=VimState::INSERT;vimInsert=true;clearSel();clamp();return;}
        if (key=='A'){curCol=(int)code[curLine].size();vimState=VimState::INSERT;vimInsert=true;clearSel();return;}
        if (key=='o'){pushUndo();std::string ind="";for(char c:code[curLine])if(isspace((unsigned char)c))ind+=c;else break;if(!code[curLine].empty()&&code[curLine].back()=='{')ind+="  ";code.insert(code.begin()+curLine+1,ind);curLine++;curCol=(int)ind.size();vimState=VimState::INSERT;vimInsert=true;clamp();ensureVis();return;}
        if (key=='O'){pushUndo();std::string ind="";if(curLine>0)for(char c:code[curLine])if(isspace((unsigned char)c))ind+=c;else break;code.insert(code.begin()+curLine,ind);curCol=(int)ind.size();vimState=VimState::INSERT;vimInsert=true;clamp();ensureVis();return;}

        // Edit
        if (key=='x'){pushUndo();if(curCol<(int)code[curLine].size())code[curLine].erase(curCol,1);clamp();return;}
        if (key=='X'){pushUndo();if(curCol>0){code[curLine].erase(curCol-1,1);curCol--;}return;}
        if (key=='r'){vimCmd="r";return;}
        if (key=='~'){pushUndo();if(curCol<(int)code[curLine].size()){char&c=code[curLine][curCol];c=isupper(c)?tolower(c):toupper(c);curCol++;}return;}
        if (key=='J'){pushUndo();if(curLine<(int)code.size()-1){std::string t=code[curLine+1];size_t s=t.find_first_not_of(" \t");code[curLine]+=" "+(s!=std::string::npos?t.substr(s):t);code.erase(code.begin()+curLine+1);}return;}
        if (key=='D'){pushUndo();code[curLine]=code[curLine].substr(0,curCol);return;}

        // dd, yy, cc (double-key commands)
        if (key=='d'){static bool ld=false;if(ld){pushUndo();setClip(code[curLine]);for(int i=0;i<n;i++){code.erase(code.begin()+std::min(curLine,(int)code.size()-1));if(code.empty())code.push_back("");}clamp();ld=false;}else ld=true;return;}
        if (key=='y'){static bool ly=false;if(ly){setClip(code[curLine]);ly=false;}else ly=true;return;}
        if (key=='Y'){setClip(code[curLine]);return;}
        if (key=='c'){static bool lc=false;if(lc){pushUndo();std::string ind="";for(char c:code[curLine])if(isspace((unsigned char)c))ind+=c;else break;code[curLine]=ind;curCol=(int)ind.size();vimState=VimState::INSERT;vimInsert=true;lc=false;}else lc=true;return;}
        if (key=='C'){pushUndo();code[curLine]=code[curLine].substr(0,curCol);vimState=VimState::INSERT;vimInsert=true;return;}
        if (key=='s'){pushUndo();if(curCol<(int)code[curLine].size())code[curLine].erase(curCol,1);vimState=VimState::INSERT;vimInsert=true;return;}
        if (key=='S'){pushUndo();std::string ind="";for(char c:code[curLine])if(isspace((unsigned char)c))ind+=c;else break;code[curLine]=ind;curCol=(int)ind.size();vimState=VimState::INSERT;vimInsert=true;return;}

        // Paste
        if (key=='p'){const char*cb=glfwGetClipboardString(win);if(cb){pushUndo();code.insert(code.begin()+curLine+1,std::string(cb));curLine++;clamp();ensureVis();}return;}
        if (key=='P'){const char*cb=glfwGetClipboardString(win);if(cb){pushUndo();code.insert(code.begin()+curLine,std::string(cb));ensureVis();}return;}

        // Undo/redo
        if (key=='u'){if(!undoStack.empty()){auto&u=undoStack.back();redoStack.push_back({code,{curLine,curCol}});code=u.first;curLine=u.second.first;curCol=u.second.second;undoStack.pop_back();clearSel();clamp();ensureVis();}return;}
        if (keyCode==GLFW_KEY_R&&ctrl){if(!redoStack.empty()){auto&u=redoStack.back();undoStack.push_back({code,{curLine,curCol}});code=u.first;curLine=u.second.first;curCol=u.second.second;redoStack.pop_back();clearSel();clamp();ensureVis();}return;}

        // Visual
        if (key=='v'&&!isVis){vimState=VimState::VISUAL;vimAnchorLine=curLine;vimAnchorCol=curCol;selLine=curLine;selCol=curCol;return;}
        if (key=='V'&&shift&&!isVis){vimState=VimState::VISUAL_LINE;vimAnchorLine=curLine;vimAnchorCol=0;selLine=curLine;selCol=0;curCol=(int)code[curLine].size();return;}
        if (key=='v'&&isVis){vimState=VimState::NORMAL;vimInsert=false;clearSel();return;}
        if (key=='>'&&shift){pushUndo();code[curLine]="  "+code[curLine];return;}
        if (key=='<'){pushUndo();if(code[curLine].size()>=2&&code[curLine][0]==' '&&code[curLine][1]==' ')code[curLine].erase(0,2);return;}

        return; // swallow in NORMAL
    }

    // --- Ctrl shortcuts ---
    if (ctrl) {
        if (keyCode==GLFW_KEY_N) { newFile(); return; }
        if (keyCode==GLFW_KEY_O) { doOpen(); return; }
        if (keyCode==GLFW_KEY_S) { if(shift) doSaveAs(currentFile); else doSave(); return; }
        if (keyCode==GLFW_KEY_B) { doCompile(); return; }
        if (keyCode==GLFW_KEY_R) { doRun(); return; }
        if (keyCode==GLFW_KEY_M&&shift) { showSerial=true; return; }
        if (keyCode==GLFW_KEY_L&&shift) { showLibMgr=true;checkInstalled(); return; }
        if (keyCode==GLFW_KEY_V&&shift) { vimMode=!vimMode;vimInsert=false;vimState=VimState::NORMAL; return; }
        if (keyCode==GLFW_KEY_Z&&!shift) { if(!undoStack.empty()){auto&u=undoStack.back();redoStack.push_back({code,{curLine,curCol}});code=u.first;curLine=u.second.first;curCol=u.second.second;undoStack.pop_back();clearSel();clamp();ensureVis();} return; }
        if (keyCode==GLFW_KEY_Y||(keyCode==GLFW_KEY_Z&&shift)) { if(!redoStack.empty()){auto&u=redoStack.back();undoStack.push_back({code,{curLine,curCol}});code=u.first;curLine=u.second.first;curCol=u.second.second;redoStack.pop_back();clearSel();clamp();ensureVis();} return; }
        if (keyCode==GLFW_KEY_A) { selLine=0;selCol=0;curLine=(int)code.size()-1;curCol=(int)code.back().size(); return; }
        if (keyCode==GLFW_KEY_C) { setClip(getSelected()); return; }
        if (keyCode==GLFW_KEY_X) { pushUndo();setClip(getSelected());deleteSel(); return; }
        if (keyCode==GLFW_KEY_V) {
            const char* cb=glfwGetClipboardString(win); if(!cb) return;
            pushUndo(); if(hasSel())deleteSel();
            std::istringstream ss{std::string(cb)}; std::string ln; bool first=true;
            while(std::getline(ss,ln)){if(!first){code.insert(code.begin()+curLine+1,code[curLine].substr(curCol));code[curLine]=code[curLine].substr(0,curCol);curLine++;curCol=0;}code[curLine].insert(curCol,ln);curCol+=(int)ln.size();first=false;}
            clamp();ensureVis(); return;
        }
        if (keyCode==GLFW_KEY_D) { pushUndo();code.insert(code.begin()+curLine+1,code[curLine]);curLine++;clamp();ensureVis(); return; }
        if (keyCode==GLFW_KEY_SLASH) { pushUndo();auto&ln=code[curLine];if(ln.size()>=2&&ln[0]=='/'&&ln[1]=='/')ln.erase(0,2);else ln="//"+ln; return; }
        if (keyCode==GLFW_KEY_F&&shift) { autoFormat(); return; }
        if (keyCode==GLFW_KEY_EQUAL) { FS=std::min(32.0f,FS+1);FSS=FS-1;FST=FS-2; return; }
        if (keyCode==GLFW_KEY_MINUS) { FS=std::max(8.0f,FS-1);FSS=FS-1;FST=FS-2; return; }
        if (keyCode==GLFW_KEY_HOME) { curLine=0;curCol=0;scrollTop=0;clearSel(); return; }
        if (keyCode==GLFW_KEY_END)  { curLine=(int)code.size()-1;curCol=(int)code.back().size();clearSel();ensureVis(); return; }
    }

    // --- Arrow keys / navigation ---
    auto anchor=[&](){if(shift&&!hasSel()){selLine=curLine;selCol=curCol;}else if(!shift)clearSel();};
    if      (keyCode==UP)               { anchor(); curLine--; }
    else if (keyCode==DOWN)             { anchor(); curLine++; }
    else if (keyCode==LEFT_KEY)         { anchor(); if(curCol>0)curCol--;else if(curLine>0){curLine--;curCol=(int)code[curLine].size();} }
    else if (keyCode==RIGHT_KEY)        { anchor(); if(curCol<(int)code[curLine].size())curCol++;else if(curLine<(int)code.size()-1){curLine++;curCol=0;} }
    else if (keyCode==GLFW_KEY_HOME)    { anchor(); curCol=0; }
    else if (keyCode==GLFW_KEY_END)     { anchor(); curCol=(int)code[curLine].size(); }
    else if (keyCode==GLFW_KEY_PAGE_UP) { anchor(); curLine-=visLines(); }
    else if (keyCode==GLFW_KEY_PAGE_DOWN){anchor(); curLine+=visLines(); }

    // --- Editing keys ---
    else if (keyCode==ENTER) {
        pushUndo(); if(hasSel())deleteSel();
        std::string& cur=code[curLine];
        std::string before=cur.substr(0,curCol), after=cur.substr(curCol);
        std::string ind=""; for(char c:cur){if(c==' '||c=='\t')ind+=c;else break;}
        if (!before.empty()&&before.back()=='{') ind+="  ";
        cur=before; code.insert(code.begin()+curLine+1,ind+after);
        curLine++; curCol=(int)ind.size();
    }
    else if (keyCode==BACKSPACE) {
        pushUndo(); if(hasSel())deleteSel();
        else if(curCol>0){code[curLine].erase(curCol-1,1);curCol--;}
        else if(curLine>0){int pl=(int)code[curLine-1].size();code[curLine-1]+=code[curLine];code.erase(code.begin()+curLine);curLine--;curCol=pl;}
    }
    else if (keyCode==DELETE_KEY) {
        pushUndo(); if(hasSel())deleteSel();
        else if(curCol<(int)code[curLine].size())code[curLine].erase(curCol,1);
        else if(curLine<(int)code.size()-1){code[curLine]+=code[curLine+1];code.erase(code.begin()+curLine+1);}
    }
    else if (keyCode==TAB) { pushUndo();if(hasSel())deleteSel();code[curLine].insert(curCol,"  ");curCol+=2; }

    clamp(); ensureVis();
}

void keyTyped() {
    if (showSerial) { if(key>=32&&key<127) serialInput+=key; return; }
    if (fpShow)     { if(key>=32&&key<127) fpInput+=key;     return; }
    if (vimMode && vimState != VimState::INSERT) {
        if (vimCmd=="r" && key>=32 && key<127) {
            pushUndo();
            if (curCol<(int)code[curLine].size()) code[curLine][curCol]=key;
            vimCmd="";
        }
        return;
    }
    if (key>=32&&key<127) {
        pushUndo(); if(hasSel())deleteSel();
        code[curLine].insert(curCol,1,key);
        curCol++; clamp(); ensureVis();
    }
}

void mouseMoved()   {}
void keyReleased()  {}
void windowMoved()  {}
void mouseClicked() {}
void windowResized() { /* tree populated explicitly via Open button */ }

// =============================================================================
// WINDOWS EVENT WIRING
// =============================================================================
// doWireCallbacks() assigns the IDE's event functions to the _on* function
// pointers used by Processing::run()'s event loop.
// It is defined here (at the bottom) so all event functions above are in scope.
//
// _registerWire stores a pointer to doWireCallbacks BEFORE main() runs.
// Processing::run() reads _wireCallbacksFn and calls it after setup() --
// by which point _wireCallbacksFn is guaranteed to be set.
//
// This approach has NO cross-TU static init dependency:
//   - _wireCallbacksFn is written by _registerWire (IDE.cpp's static)
//   - _wireCallbacksFn is read by run() (called from main, well after statics)
// =============================================================================

#ifdef _WIN32
static void doWireCallbacks() {
    _onKeyPressed    = keyPressed;
    _onKeyReleased   = keyReleased;
    _onKeyTyped      = keyTyped;
    _onMousePressed  = mousePressed;
    _onMouseReleased = mouseReleased;
    _onMouseClicked  = mouseClicked;
    _onMouseMoved    = mouseMoved;
    _onMouseDragged  = mouseDragged;
    _onMouseWheel    = mouseWheel;
    _onWindowMoved   = windowMoved;
    _onWindowResized = windowResized;
}

// This static initializer runs in IDE.cpp's translation unit --
// no dependency on Processing.cpp's statics. It just stores a function pointer.
static int _registerWire = ([]{ _wireCallbacksFn = doWireCallbacks; return 0; })();
#endif

} // namespace Processing

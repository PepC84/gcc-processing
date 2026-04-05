#include "Processing.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>

namespace Processing {

// ═══════════════════════════════════════════════════════════════════════════
// CONSTANTS & COLOURS  (Processing / VS Code dark theme)
// ═══════════════════════════════════════════════════════════════════════════

// Layout
static const int   MENUBAR_H = 26;
static const int   TOOLBAR_H = 40;
static const int   STATUS_H  = 20;
static int         CONSOLE_H = 170;
static const int   CONSOLE_H_MIN = 60;
static const int   CONSOLE_H_MAX = 600;
static const int   GUTTER_W  = 56;

// Console resize drag
static bool consoleResizing    = false;
static const int TAB_H = 22;  // console tab bar height
static int  consoleResizeAnchorY = 0;
static int  consoleResizeAnchorH = 0;

// Font sizes
static float FS  = 14.0f;   // editor   (Ctrl+= / Ctrl+-)
static float FSS = 13.0f;   // toolbar / labels
static float FST = 12.0f;   // menus / status

// Derived layout helpers
// Sidebar
static int  SIDEBAR_W     = 200;
static bool sidebarVisible = true;
static const int SIDEBAR_W_MIN = 120;
static const int SIDEBAR_W_MAX = 400;
static bool sidebarResizing    = false;
static int  sidebarResizeAnchorX = 0;
static int  sidebarResizeAnchorW = 0;

// File tree state
struct FTEntry { std::string name; bool isDir; bool expanded; int depth; };
static std::vector<FTEntry> ftEntries;
static int   ftScroll    = 0;
static float ftScrollF   = 0;
static std::string ftRoot = ".";  // relative to project dir

// Terminal position: bottom (default) or right panel
enum class TermPos { Bottom, Right };
static TermPos terminalPos = TermPos::Bottom;
static int  TERM_SIDE_W   = 320;  // width when docked right
static const int TERM_SIDE_W_MIN = 180;
static const int TERM_SIDE_W_MAX = 700;
static bool termSideResizing    = false;
static int  termSideAnchorX     = 0;
static int  termSideAnchorW     = 0;

// Sidebar icon IDs for click detection
static bool sbBtnHovFile = false;

// Sidebar width effective (0 if hidden)
static int sbW() { return sidebarVisible ? SIDEBAR_W : 0; }

static float lineH()    { return FS * 1.6f; }
static int   editorX()  { return sbW() + GUTTER_W; }
static int   editorFullW() { return (terminalPos==TermPos::Right ? width-sbW()-TERM_SIDE_W : width-sbW()); }
static int   editorY()  { return MENUBAR_H + TOOLBAR_H; }
static int   editorH()  { return (terminalPos==TermPos::Bottom ? height-editorY()-STATUS_H-CONSOLE_H : height-editorY()-STATUS_H); }
static int   statusY()  { return editorY() + editorH(); }
static int   consoleY() { return (terminalPos==TermPos::Bottom ? statusY()+STATUS_H : editorY()); }
static int   consoleX() { return (terminalPos==TermPos::Right  ? width-TERM_SIDE_W : 0); }
static int   consoleW() { return (terminalPos==TermPos::Right  ? TERM_SIDE_W : width); }
static int   visLines() { return std::max(1, (int)(editorH() / lineH())); }

// ═══════════════════════════════════════════════════════════════════════════
// EDITOR STATE
// ═══════════════════════════════════════════════════════════════════════════

static std::vector<std::string> code = {
    "// ── run once ────────────────────────────────────────",
    "void setup() {",
    "  size(640, 360);",
    "}",
    "",
    "// ── loops forever ───────────────────────────────────",
    "void draw() {",
    "  background(102);",
    "  fill(255);",
    "  ellipse(mouseX, mouseY, 40, 40);",
    "}"
};

static int  curLine = 0, curCol = 0;
static int  selLine = -1, selCol = -1;
static int  scrollTop = 0;

// ── Multi-terminal tab system ─────────────────────────────────────────────
struct Terminal {
    std::string name;
    std::vector<std::string> lines;
    int scroll = 0;
    bool hasError = false;
};
static std::vector<Terminal> terminals = { {"Output", {}, 0, false} };
static int activeTab = 0;

// Helpers — always operate on activeTab
static std::vector<std::string>& curTermLines()  { return terminals[activeTab].lines; }
static int&                      curTermScroll() { return terminals[activeTab].scroll; }
static bool&                     curTermError()  { return terminals[activeTab].hasError; }

// Legacy names used throughout (compile output tab is always tab 0 for build)
static std::vector<std::string>& outLines  = terminals[0].lines;
static int&                      outScroll = terminals[0].scroll;
static bool modified = false;
static bool& hasError = terminals[0].hasError;
static std::string currentFile = "";
static std::string sketchBin   = "SketchApp";
// ── macOS vs Linux build flags ────────────────────────────────────────────
static std::string getDefaultBuildFlags() {
#ifdef __APPLE__
    // Ask Homebrew for include/lib paths at runtime
    auto brewPrefix = [](const char* pkg) -> std::string {
        char buf[256] = {};
        std::string cmd = std::string("brew --prefix ") + pkg + " 2>/dev/null";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return "";
        fgets(buf, sizeof(buf), p);
        pclose(p);
        std::string s(buf);
        while (!s.empty() && (s.back()=='\n'||s.back()=='\r'||s.back()==' ')) s.pop_back();
        return s;
    };
    std::string glew = brewPrefix("glew");
    std::string glfw = brewPrefix("glfw");
    std::string flags;
    if (!glew.empty()) flags += "-I" + glew + "/include -L" + glew + "/lib ";
    if (!glfw.empty()) flags += "-I" + glfw + "/include -L" + glfw + "/lib ";
    flags += "-lglfw -lGLEW -framework OpenGL";
    return flags;
#else
    return "-lglfw -lGLEW -lGL -lGLU -lm";
#endif
}
static std::string buildFlags = getDefaultBuildFlags();

// Undo / redo stacks
using Snapshot = std::pair<std::vector<std::string>, std::pair<int,int>>;
static std::vector<Snapshot> undoStack, redoStack;

static void pushUndo() {
    undoStack.push_back({ code, { curLine, curCol } });
    if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
    redoStack.clear();
    modified = true;
}

// ═══════════════════════════════════════════════════════════════════════════
// VIM MODE
// ═══════════════════════════════════════════════════════════════════════════

static bool vimMode    = false;
static bool vimInsert  = false;   // false = NORMAL, true = INSERT
static std::string vimCmd = "";   // pending command buffer (e.g. "2d")

// ═══════════════════════════════════════════════════════════════════════════
// MENU STATE
// ═══════════════════════════════════════════════════════════════════════════

enum class Menu { None, File, Edit, Sketch, Tools, Libraries };
static Menu openMenu = Menu::None;

// ═══════════════════════════════════════════════════════════════════════════
// SERIAL MONITOR STATE
// ═══════════════════════════════════════════════════════════════════════════

static bool        showSerial    = false;
static int         serialFd      = -1;
static std::string serialPort    = "";
static int         serialBaud    = 9600;
static std::vector<std::string> serialLog;
static std::string serialInput   = "";
static int         serialScroll  = 0;

static const std::vector<int> BAUD_RATES = {
    300, 600, 1200, 2400, 4800, 9600, 14400,
    19200, 38400, 57600, 115200, 230400
};
static int baudIndex = 5;   // default 9600

// Get list of connected USB/serial ports
static std::vector<std::string> listPorts() {
    std::vector<std::string> ports;
    glob_t g;
    if (glob("/dev/ttyUSB*", 0, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; i++) ports.push_back(g.gl_pathv[i]);
    globfree(&g);
    if (glob("/dev/ttyACM*", 0, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; i++) ports.push_back(g.gl_pathv[i]);
    globfree(&g);
    if (glob("/dev/ttyS*", 0, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; i++) ports.push_back(g.gl_pathv[i]);
    globfree(&g);
    return ports;
}

static speed_t baudToSpeed(int baud) {
    switch(baud){
        case 300:    return B300;
        case 600:    return B600;
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

static void openSerial(const std::string& port, int baud) {
    if (serialFd >= 0) { close(serialFd); serialFd = -1; }
    serialFd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serialFd < 0) {
        serialLog.push_back("ERROR: cannot open " + port);
        return;
    }
    struct termios tty;
    tcgetattr(serialFd, &tty);
    speed_t sp = baudToSpeed(baud);
    cfsetispeed(&tty, sp);
    cfsetospeed(&tty, sp);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tcsetattr(serialFd, TCSANOW, &tty);
    serialPort = port;
    serialLog.push_back("Connected: " + port + " @ " + std::to_string(baud) + " baud");
}

static void closeSerial() {
    if (serialFd >= 0) { close(serialFd); serialFd = -1; }
    serialLog.push_back("Disconnected.");
    serialPort = "";
}

static void pollSerial() {
    if (serialFd < 0) return;
    char buf[256]; int n;
    static std::string partial;
    while ((n = read(serialFd, buf, sizeof(buf)-1)) > 0) {
        buf[n] = 0;
        partial += buf;
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            serialLog.push_back(partial.substr(0, pos));
            partial = partial.substr(pos+1);
            serialScroll = std::max(0, (int)serialLog.size() - 16);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// LIBRARY MANAGER
// ═══════════════════════════════════════════════════════════════════════════

struct Library {
    std::string name, desc, pkg, header, installCmd, linkFlag;
    bool installed = false;
};

// installCmd is only used for header-only libs (curl/wget).
// For pkg-based libs the command is built dynamically by buildInstallCmd().
static std::vector<Library> libraries = {
    // ── Serial / Arduino ──────────────────────────────────────────────────
    // libserialport: official cross-platform serial library by sigrok
    { "libserialport",   "Official cross-platform serial port library (sigrok)",
      "libserialport-dev","#include <libserialport.h>",  "", "-lserialport" },
    // Boost.Asio serial — part of boost (most reliable on Arch)
    { "Boost (Asio)",    "Boost.Asio serial_port — robust serial I/O",
      "boost",            "#include <boost/asio/serial_port.hpp>", "", "-lboost_system" },
    // ── Math / Physics ────────────────────────────────────────────────────
    { "Eigen",           "Linear algebra: matrices, vectors, decompositions",
      "libeigen3-dev",    "#include <Eigen/Dense>",       "", ""          },
    { "glm",             "OpenGL mathematics (vec3, mat4…)",
      "libglm-dev",       "#include <glm/glm.hpp>",       "", ""          },
    { "Box2D",           "2D rigid body physics simulation",
      "libbox2d-dev",     "#include <box2d/box2d.h>",     "", "-lbox2d"   },
    { "FFTW3",           "Fast Fourier Transform (signal/audio)",
      "libfftw3-dev",     "#include <fftw3.h>",           "", "-lfftw3"   },
    // ── Audio / Vision ────────────────────────────────────────────────────
    { "OpenCV",          "Computer vision and image processing",
      "libopencv-dev",    "#include <opencv2/opencv.hpp>","", "-lopencv_core -lopencv_highgui" },
    { "SFML Audio",      "Audio playback, music, and recording",
      "libsfml-dev",      "#include <SFML/Audio.hpp>",    "", "-lsfml-audio" },
    { "PortAudio",       "Cross-platform low-latency audio I/O",
      "portaudio19-dev",  "#include <portaudio.h>",       "", "-lportaudio"  },
    // ── Network / Data ────────────────────────────────────────────────────
    { "libcurl",         "HTTP/HTTPS requests, REST APIs",
      "libcurl4-openssl-dev", "#include <curl/curl.h>",  "", "-lcurl"    },
    { "nlohmann/json",   "Header-only modern C++ JSON",
      "",                 "#include \"json.hpp\"",
      "curl -sL https://github.com/nlohmann/json/releases/latest/download/json.hpp -o src/json.hpp", "" },
    { "SQLite3",         "Embedded SQL database",
      "libsqlite3-dev",   "#include <sqlite3.h>",         "", "-lsqlite3" },
    // ── Utilities ─────────────────────────────────────────────────────────
    { "stb_image",       "Header-only PNG/JPG/BMP/GIF loader",
      "",                 "#include \"stb_image.h\"",
      "curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o src/stb_image.h", "" },
    { "stb_truetype",    "Header-only TTF font rasterizer",
      "",                 "#include \"stb_truetype.h\"",
      "curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h -o src/stb_truetype.h", "" },
    // ── Editor ────────────────────────────────────────────────────────────
    { "Vim keybindings", "Enable vim Normal/Insert mode in editor",
      "",                 "// vim mode — enable via Tools > Toggle Vim Mode", "", "" },
};

static bool showLibMgr    = false;
static int  libScroll     = 0;
static int  installingLib = -1;
static std::string libStatus = "";

// ── Package manager detection ──────────────────────────────────────────────
enum class PkgMgr { Unknown, Apt, Pacman, Dnf, Zypper };

static PkgMgr detectPkgMgr() {
    if (system("command -v pacman >/dev/null 2>&1") == 0) return PkgMgr::Pacman;
    if (system("command -v apt-get >/dev/null 2>&1") == 0) return PkgMgr::Apt;
    if (system("command -v dnf >/dev/null 2>&1")     == 0) return PkgMgr::Dnf;
    if (system("command -v zypper >/dev/null 2>&1")  == 0) return PkgMgr::Zypper;
    return PkgMgr::Unknown;
}

// Map apt package names to pacman/dnf equivalents
struct PkgNames { std::string apt, pacman, dnf; };
static const std::vector<PkgNames> PKG_MAP = {
    // apt name              pacman name         dnf name
    { "libserialport-dev",   "libserialport",    "libserialport-devel"  },
    { "boost",               "boost",            "boost-devel"          },
    { "libeigen3-dev",       "eigen",            "eigen3-devel"         },
    { "libopencv-dev",       "opencv",           "opencv-devel"         },
    { "libsfml-dev",         "sfml",             "SFML-devel"           },
    { "portaudio19-dev",     "portaudio",        "portaudio-devel"      },
    { "libcurl4-openssl-dev","curl",             "libcurl-devel"        },
    { "libglm-dev",          "glm",              "glm-devel"            },
    { "libbox2d-dev",        "box2d",            "box2d-devel"          },
    { "libsqlite3-dev",      "sqlite",           "sqlite-devel"         },
    { "libfftw3-dev",        "fftw",             "fftw-devel"           },
};

static std::string resolvePkg(const std::string& aptName) {
    PkgMgr pm = detectPkgMgr();
    if (pm == PkgMgr::Apt) return aptName;
    for (auto& p : PKG_MAP) {
        if (p.apt == aptName) {
            if (pm == PkgMgr::Pacman) return p.pacman;
            if (pm == PkgMgr::Dnf)    return p.dnf;
        }
    }
    return aptName; // fallback — user may need to adjust
}

// Find an available terminal emulator
static std::string findTerminal() {
    const std::vector<std::string> terms = {
        "kitty","alacritty","foot","wezterm","gnome-terminal","konsole",
        "xfce4-terminal","xterm","st","urxvt"
    };
    for (auto& t : terms)
        if (system(("command -v "+t+" >/dev/null 2>&1").c_str()) == 0)
            return t;
    return "";
}

// Build a terminal-based sudo install command.
// Opens a new terminal window where the user types their password normally.
static std::string buildInstallCmd(const std::string& aptPkg) {
    if (aptPkg.empty()) return "";
    PkgMgr pm  = detectPkgMgr();
    std::string pkg = resolvePkg(aptPkg);

    std::string innerCmd;
    switch (pm) {
        case PkgMgr::Pacman: innerCmd = "sudo pacman -S --noconfirm " + pkg; break;
        case PkgMgr::Dnf:    innerCmd = "sudo dnf install -y "        + pkg; break;
        case PkgMgr::Zypper: innerCmd = "sudo zypper install -y "     + pkg; break;
        case PkgMgr::Apt:    innerCmd = "sudo apt-get install -y "    + pkg; break;
        default:
            return "echo 'Unknown package manager. Install manually: " + pkg + "'; read";
    }
    // Run in a terminal so the user can type their sudo password
    std::string term = findTerminal();
    if (term.empty())
        return innerCmd; // no terminal found — fall back to blocking call

    // Pause after install so the user can see output before window closes
    std::string pause = "; echo ''; echo '=== Done. Press Enter to close ==='; read";
    if (term == "gnome-terminal")
        return term + " -- sh -c '" + innerCmd + pause + "'";
    // kitty, alacritty, foot, wezterm, konsole, xfce4-terminal, xterm, st, urxvt
    return term + " -e sh -c '" + innerCmd + pause + "'";
}

static bool isPkgInstalled(const std::string& aptPkg) {
    if (aptPkg.empty()) return false;
    std::string pkg = resolvePkg(aptPkg);
    PkgMgr pm = detectPkgMgr();
    switch (pm) {
        case PkgMgr::Pacman: return system(("pacman -Q " + pkg + " >/dev/null 2>&1").c_str()) == 0;
        case PkgMgr::Dnf:    return system(("rpm -q "    + pkg + " >/dev/null 2>&1").c_str()) == 0;
        case PkgMgr::Zypper: return system(("rpm -q "    + pkg + " >/dev/null 2>&1").c_str()) == 0;
        case PkgMgr::Apt:    return system(("dpkg -s "   + pkg + " >/dev/null 2>&1").c_str()) == 0;
        default:             return false;
    }
}

static void checkInstalled() {
    for (auto& lib : libraries) {
        if (lib.pkg.empty()) {
            // Header-only: check src/ directory
            std::string h = lib.header;
            size_t a = h.find('"'), b = h.rfind('"');
            if (a != std::string::npos && b != a) {
                struct stat st;
                lib.installed = (stat(("src/" + h.substr(a+1, b-a-1)).c_str(), &st) == 0);
            } else {
                lib.installed = (lib.name == "Vim keybindings") ? vimMode : false;
            }
        } else {
            lib.installed = isPkgInstalled(lib.pkg);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CURSOR / SELECTION HELPERS
// ═══════════════════════════════════════════════════════════════════════════

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
        { l0=selLine; c0=selCol; l1=curLine; c1=curCol; }
    else
        { l0=curLine; c0=curCol; l1=selLine; c1=selCol; }
}
static std::string getSelected() {
    if (!hasSel()) return "";
    int l0,c0,l1,c1; selRange(l0,c0,l1,c1);
    if (l0 == l1) return code[l0].substr(c0, c1-c0);
    std::string s = code[l0].substr(c0) + "\n";
    for (int l = l0+1; l < l1; l++) s += code[l] + "\n";
    s += code[l1].substr(0, c1);
    return s;
}
static void deleteSel() {
    if (!hasSel()) return;
    int l0,c0,l1,c1; selRange(l0,c0,l1,c1);
    if (l0 == l1) { code[l0].erase(c0, c1-c0); }
    else {
        code[l0] = code[l0].substr(0,c0) + code[l1].substr(c1);
        code.erase(code.begin()+l0+1, code.begin()+l1+1);
    }
    curLine=l0; curCol=c0; clearSel();
}

// ═══════════════════════════════════════════════════════════════════════════
// FILE OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

static void newFile() {
    code = {
        "// ── run once ────────────────────────────────────────",
        "void setup() {",
        "  size(640, 360);",
        "}",
        "",
        "// ── loops forever ───────────────────────────────────",
        "void draw() {",
        "  background(102);",
        "}"
    };
    curLine=curCol=scrollTop=0; clearSel();
    undoStack.clear(); redoStack.clear();
    currentFile=""; sketchBin="SketchApp"; modified=false;
    outLines.push_back("New sketch.");
}

static void saveFile(const std::string& path) {
    std::string p = path;
    if (p.size() < 4 || p.substr(p.size()-4) != ".cpp") p += ".cpp";
    std::ofstream f(p);
    if (!f) { outLines.push_back("ERROR: cannot save " + p); return; }
    for (auto& l : code) f << l << "\n";
    currentFile=p; modified=false;
    outLines.push_back("Saved: " + p);
    outScroll = std::max(0, (int)outLines.size()-8);
}

static void openFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { outLines.push_back("ERROR: cannot open " + path); return; }
    code.clear(); std::string l;
    while (std::getline(f, l)) code.push_back(l);
    if (code.empty()) code.push_back("");
    curLine=curCol=scrollTop=0; clearSel();
    undoStack.clear(); redoStack.clear();
    currentFile=path; modified=false;
    outLines.push_back("Opened: " + path);
}

static std::string sysFileDialog(bool save, const std::string& def="") {
    std::string cmd = save
        ? "zenity --file-selection --save --confirm-overwrite --title=\"Save Sketch\""
          " --filename=\"" + def + "\" --file-filter=\"*.cpp\" 2>/dev/null"
        : "zenity --file-selection --title=\"Open Sketch\""
          " --file-filter=\"*.cpp\" 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char buf[1024] = {};
    if (fgets(buf, sizeof(buf), p)) {}
    pclose(p);
    std::string r = buf;
    while (!r.empty() && (r.back()=='\n'||r.back()=='\r')) r.pop_back();
    return r;
}

static std::vector<std::string> listSketches() {
    std::vector<std::string> files;
    DIR* d = opendir("."); if (!d) return files;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.substr(n.size()-4) == ".cpp") files.push_back(n);
    }
    closedir(d); std::sort(files.begin(), files.end());
    return files;
}

// ═══════════════════════════════════════════════════════════════════════════
// BUILD & RUN
// ═══════════════════════════════════════════════════════════════════════════

// Write src/Sketch_run.cpp from the current code buffer
static bool writeSketch() {
    std::ofstream f("src/Sketch_run.cpp");
    if (!f) { outLines.push_back("ERROR: cannot write src/Sketch_run.cpp"); hasError=true; return false; }
    bool hasNS = false;
    for (auto& l : code)
        if (l.find("namespace Processing") != std::string::npos) { hasNS=true; break; }
    f << "#include \"Processing.h\"\n";
    if (!hasNS) f << "namespace Processing {\n";
    for (auto& l : code) f << l << "\n";
    if (!hasNS) f << "} // namespace Processing\n";
    return true;
}

// doCompile: compile only — check for errors, produce binary
static void doCompile() {
    outLines.clear(); hasError=false; outScroll=0;
    if (!writeSketch()) return;

    // Derive binary name from sketch filename
    sketchBin = "SketchApp";
    if (!currentFile.empty()) {
        std::string base = currentFile;
        size_t sl = base.rfind('/');
        if (sl != std::string::npos) base = base.substr(sl+1);
        if (base.size()>4 && base.substr(base.size()-4)==".cpp")
            base = base.substr(0, base.size()-4);
        sketchBin = base;
    }

    std::string cmd = "g++ -std=c++17 src/Processing.cpp src/Sketch_run.cpp src/main.cpp"
                      " -o " + sketchBin + " " + buildFlags + " 2>&1";
    outLines.push_back("Building: " + sketchBin);
    outLines.push_back("$ " + cmd);

    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { outLines.push_back("ERROR: popen failed"); hasError=true; return; }
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) {
        std::string s(buf);
        while (!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back();
        if (s.find("error:") != std::string::npos) hasError=true;
        if (!s.empty()) outLines.push_back(s);
    }
    pclose(p);

    if (!hasError) {
        outLines.push_back("✓ Built: ./" + sketchBin + "  (Ctrl+R to run)");
    } else {
        outLines.push_back("✗ Build failed — see errors above.");
    }
    outScroll = std::max(0, (int)outLines.size()-10);
}

// doRun: rebuild then launch (so Run always reflects latest code)
static void doRun() {
    doCompile();          // always rebuild first
    if (hasError) {
        outLines.push_back("✗ Not running — fix errors first.");
        outScroll = std::max(0, (int)outLines.size()-10);
        return;
    }
    outLines.push_back("$ ./" + sketchBin + " &");
    system(("./" + sketchBin + " &").c_str());
    outScroll = std::max(0, (int)outLines.size()-10);
}

// ═══════════════════════════════════════════════════════════════════════════
// SYNTAX HIGHLIGHTING
// ═══════════════════════════════════════════════════════════════════════════

static const std::vector<std::string> KEYWORDS = {
    // C++ core
    "void","int","float","double","bool","char","auto","long","short","unsigned","signed",
    "true","false","null","nullptr","return","if","else","for","while","do",
    "switch","case","break","continue","default","new","delete","class","struct",
    "template","typename","namespace","static","const","constexpr","override","virtual",
    "public","private","protected","final","explicit","inline","extern","typedef","using",
    // Processing core
    "size","background","fill","stroke","noStroke","noFill",
    "ellipse","rect","circle","line","triangle","quad","point","arc","bezier","curve",
    "box","sphere","lights","noLights","ambientLight","directionalLight","pointLight","spotLight",
    "translate","rotate","rotateX","rotateY","rotateZ","scale","shearX","shearY",
    "pushMatrix","popMatrix","push","pop",
    "beginShape","endShape","vertex","bezierVertex","curveVertex",
    "width","height","mouseX","mouseY","pmouseX","pmouseY",
    "isMousePressed","isKeyPressed","mouseButton","keyCode",
    "frameCount","frameRate","millis","second","minute","hour","day","month","year",
    "PI","TWO_PI","HALF_PI","QUARTER_PI","TAU","P3D","P2D","CLOSE",
    "map","constrain","lerp","norm","sqrt","sq","abs","pow","floor","ceil","round",
    "sin","cos","tan","asin","acos","atan","atan2","degrees","radians","dist","mag",
    "noise","random","randomSeed","noiseSeed",
    "color","PVector","ArrayList","HashMap","setup","draw",
    "text","textSize","textFont","textAlign","textWidth","textAscent","textDescent","textLeading",
    "loadImage","image","createGraphics","loadFont","createFont","tint","noTint",
    "loadPixels","updatePixels","pixels","get","set","filter",
    "blendMode","colorMode","lerpColor","red","green","blue","alpha","hue","saturation","brightness",
    "println","print","save","saveFrame","exit","loop","noLoop","redraw",
    "beginCamera","endCamera","camera","perspective","ortho","frustum",
};

struct Tok { std::string s; int r,g,b; };

static std::vector<Tok> tokenize(const std::string& ln) {
    std::vector<Tok> out;
    int n = (int)ln.size(), i = 0;
    while (i < n) {
        // Line comment
        if (i+1<n && ln[i]=='/' && ln[i+1]=='/') {
            out.push_back({ ln.substr(i), 106,153,85 }); break;
        }
        // Preprocessor directive
        if (ln[i] == '#') {
            out.push_back({ ln.substr(i), 197,134,192 }); break;
        }
        // String literal
        if (ln[i] == '"') {
            int j=i+1; while(j<n && ln[j]!='"') j++;
            out.push_back({ ln.substr(i, j-i+1), 206,145,120 }); i=j+1; continue;
        }
        // Angle-bracket include path  <stdio.h>
        if (ln[i] == '<' && i>0) {
            int j=i+1; while(j<n && ln[j]!='>') j++;
            out.push_back({ ln.substr(i, j-i+1), 206,145,120 }); i=j+1; continue;
        }
        // Char literal
        if (ln[i]=='\'' && i+2<n) {
            out.push_back({ ln.substr(i, 3), 206,145,120 }); i+=3; continue;
        }
        // Number
        if (isdigit((unsigned char)ln[i])) {
            int j=i;
            while (j<n && (isdigit((unsigned char)ln[j])||ln[j]=='.'||ln[j]=='f'||ln[j]=='x')) j++;
            out.push_back({ ln.substr(i, j-i), 181,206,168 }); i=j; continue;
        }
        // Identifier / keyword / function
        if (isalpha((unsigned char)ln[i]) || ln[i]=='_') {
            int j=i;
            while (j<n && (isalnum((unsigned char)ln[j])||ln[j]=='_')) j++;
            std::string w = ln.substr(i, j-i);
            bool kw = false;
            for (auto& k : KEYWORDS) if (k==w) { kw=true; break; }
            bool fn = (j<n && ln[j]=='(');
            if      (kw) out.push_back({ w, 112,184,255 });
            else if (fn) out.push_back({ w, 220,220,170 });
            else         out.push_back({ w, 156,220,254 });
            i=j; continue;
        }
        out.push_back({ std::string(1,ln[i]), 200,200,200 }); i++;
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// RAW GL DRAW HELPERS  (no rect() to avoid TRIANGLE_FAN artifact)
// ═══════════════════════════════════════════════════════════════════════════

static void qFill(float x, float y, float w, float h,
                  int r, int g, int b, int a=255) {
    glColor4ub(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x,   y);   glVertex2f(x+w, y);
    glVertex2f(x+w, y+h); glVertex2f(x,   y+h);
    glEnd();
}
static void qBorder(float x, float y, float w, float h,
                    int r, int g, int b) {
    glColor4ub(r, g, b, 255); glLineWidth(1);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x,   y);   glVertex2f(x+w, y);
    glVertex2f(x+w, y+h); glVertex2f(x,   y+h);
    glEnd();
}
static void qLine(float x1, float y1, float x2, float y2,
                  int r, int g, int b) {
    glColor4ub(r, g, b, 255); glLineWidth(1);
    glBegin(GL_LINES); glVertex2f(x1,y1); glVertex2f(x2,y2); glEnd();
}
static void iText(const std::string& s, float x, float y,
                  int r, int g, int b, float sz) {
    textSize(sz); fill(r,g,b); noStroke(); text(s, x, y);
}

// Pixel x of column col in editor line ln
static float xOf(int ln, int col) {
    textSize(FS);
    if (col == 0) return sbW() + GUTTER_W + 4;
    return sbW() + GUTTER_W + 4 + textWidth(code[ln].substr(0, col));
}

// ═══════════════════════════════════════════════════════════════════════════
// MENU BAR
// ═══════════════════════════════════════════════════════════════════════════

struct MI { std::string label, shortcut; };

static void drawDropdown(float mx, float my, const std::vector<MI>& items) {
    float pw = 210, ph = items.size()*22 + 8;
    qFill(mx, my, pw, ph, 45,45,45);
    qBorder(mx, my, pw, ph, 70,70,70);
    for (int i = 0; i < (int)items.size(); i++) {
        float ry = my + 4 + i*22;
        if (items[i].label == "---") {
            qFill(mx+4, ry+10, pw-8, 1, 65,65,65); continue;
        }
        bool hov = mouseX>=mx && mouseX<=mx+pw && mouseY>=ry && mouseY<=ry+22;
        if (hov) qFill(mx+1, ry, pw-2, 22, 17,108,179);
        iText(items[i].label, mx+10, ry+16, hov?255:215, hov?255:218, hov?255:228, FST);
        if (!items[i].shortcut.empty()) {
            textSize(FST);
            float sw = textWidth(items[i].shortcut);
            iText(items[i].shortcut, mx+pw-sw-8, ry+16, hov?220:120, hov?220:123, hov?220:145, FST);
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════
// FILE TREE (SIDEBAR)
// ═══════════════════════════════════════════════════════════════════════════

static void populateTree() {
    ftEntries.clear();
    // Walk up to 2 levels under ftRoot
    auto addDir = [&](const std::string& path, int depth, auto& self) -> void {
        if (depth > 2) return;
        DIR* d = opendir(path.c_str());
        if (!d) return;
        struct dirent* e;
        std::vector<std::string> dirs, files;
        while ((e = readdir(d))) {
            std::string n = e->d_name;
            if (n=="."||n=="..") continue;
            if (n.size()>0 && n[0]=='.') continue;
            std::string full = path + "/" + n;
            struct stat st; stat(full.c_str(), &st);
            if (S_ISDIR(st.st_mode)) dirs.push_back(n);
            else files.push_back(n);
        }
        closedir(d);
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        for (auto& dn : dirs) {
            std::string full = path+"/"+dn;
            bool exp = false;
            for (auto& ex : ftEntries) if (ex.name==dn&&ex.isDir) { exp=ex.expanded; break; }
            ftEntries.push_back({dn, true, exp, depth});
            if (exp) self(full, depth+1, self);
        }
        for (auto& fn : files) {
            ftEntries.push_back({fn, false, false, depth});
        }
    };
    addDir(ftRoot, 0, addDir);
}

static void drawSidebar() {
    if (!sidebarVisible) return;
    int sw = SIDEBAR_W;

    // Background
    qFill(0, editorY(), sw, height-editorY(), 26,26,26);
    qLine(sw, editorY(), sw, height, 50,50,50);

    // Sidebar resize handle (right edge 4px)
    bool onSbEdge = (mouseX >= sw-4 && mouseX <= sw+2 &&
                     mouseY >= editorY() && mouseY <= height);
    qFill(sw-4, editorY(), 4, height-editorY(),
          (onSbEdge||sidebarResizing)?17:26,
          (onSbEdge||sidebarResizing)?108:27,
          (onSbEdge||sidebarResizing)?179:36);

    // Header
    qFill(0, editorY(), sw, 24, 32,32,32);
    iText("EXPLORER", 10, editorY()+17, 130,135,165, FST);

    // Refresh button
    float rbx=sw-22, rby=editorY()+4, rbw=16, rbh=16;
    bool rbHov = mouseX>=rbx&&mouseX<=rbx+rbw&&mouseY>=rby&&mouseY<=rby+rbh;
    qFill(rbx, rby, rbw, rbh, rbHov?50:32, rbHov?52:33, rbHov?70:44);
    iText("r", rbx+4, rby+rbh*0.82f, rbHov?210:140, rbHov?220:150, rbHov?255:190, FST);

    // Current file label
    std::string fname = currentFile.empty() ? "(unsaved)" : currentFile;
    size_t sl = fname.rfind('/');
    if (sl != std::string::npos) fname = fname.substr(sl+1);
    iText(fname, 10, editorY()+40, 180,210,255, FST);

    // File tree
    int treeTop = editorY() + 50;
    float rowH = FSS * 1.7f;
    int vis = (int)((height - treeTop) / rowH);

    // Refresh if empty
    if (ftEntries.empty()) populateTree();

    ftScroll = std::max(0, std::min(ftScroll, std::max(0,(int)ftEntries.size()-vis)));

    for (int i = 0; i < vis; i++) {
        int fi = ftScroll + i;
        if (fi >= (int)ftEntries.size()) break;
        auto& fe = ftEntries[fi];
        float ry = treeTop + i * rowH;
        float rx = 8 + fe.depth * 12;
        bool hov = mouseX>=0 && mouseX<sw-6 && mouseY>=ry && mouseY<ry+rowH;
        if (hov) qFill(0, ry, sw-6, rowH, 38,38,38);

        // Icon
        std::string icon = fe.isDir ? (fe.expanded ? "v " : "> ") : "  ";
        int ir = fe.isDir ? 200 : 160;
        int ig = fe.isDir ? 180 : 185;
        int ib = fe.isDir ? 100 : 210;
        textSize(FST);
        iText(icon + fe.name, rx, ry+rowH*0.75f, ir,ig,ib, FST);
    }
}

static void drawMenuBar() {
    qFill(0, 0, width, MENUBAR_H, 36,36,36);
    qLine(0, MENUBAR_H-1, width, MENUBAR_H-1, 58,58,58);

    // Compute menu positions dynamically so draw and click detection always agree
    struct MHead { std::string label; Menu id; float x=0, w=0; };
    std::vector<MHead> heads = {
        {"File",Menu::File}, {"Edit",Menu::Edit},
        {"Sketch",Menu::Sketch}, {"Tools",Menu::Tools},
        {"Libraries",Menu::Libraries}
    };
    textSize(FST);
    float mx = 6;
    for (auto& h : heads) {
        h.x = mx;
        h.w = textWidth(h.label) + 14;
        mx += h.w + 2;
    }
    for (auto& h : heads) {
        bool open = (openMenu == h.id);
        bool hov  = mouseX >= h.x-2 && mouseX <= h.x+h.w && mouseY >= 0 && mouseY < MENUBAR_H;
        if (open || hov) qFill(h.x-2, 0, h.w, MENUBAR_H, 17,108,179);
        iText(h.label, h.x+4, MENUBAR_H*0.74f, 228,230,242, FST);
    }

    if      (openMenu==Menu::File)
        drawDropdown(2,MENUBAR_H,{{"New","Ctrl+N"},{"Open...","Ctrl+O"},{"---",""},{"Save","Ctrl+S"},{"Save As...","Ctrl+⇧+S"},{"---",""},{"Exit",""}});
    else if (openMenu==Menu::Edit)
        drawDropdown(42,MENUBAR_H,{{"Undo","Ctrl+Z"},{"Redo","Ctrl+Y"},{"---",""},{"Cut","Ctrl+X"},{"Copy","Ctrl+C"},{"Paste","Ctrl+V"},{"---",""},{"Select All","Ctrl+A"},{"Duplicate Line","Ctrl+D"},{"---",""},{"Toggle Comment","Ctrl+/"},{"Auto Format","Ctrl+⇧+F"}});
    else if (openMenu==Menu::Sketch)
        drawDropdown(84,MENUBAR_H,{{"Build","Ctrl+B"},{"Run (build+run)","Ctrl+R"},{"---",""},{"Show Folder",""},{"Export Binary",""}});
    else if (openMenu==Menu::Tools)
        drawDropdown(138,MENUBAR_H,{{"Serial Monitor","Ctrl+⇧+M"},{"---",""},{"Toggle Vim Mode","Ctrl+⇧+V"},{"Auto Format","Ctrl+⇧+F"},{"---",""},{"Increase Font","Ctrl+="},{"Decrease Font","Ctrl+-"}});
    else if (openMenu==Menu::Libraries)
        drawDropdown(182,MENUBAR_H,{{"Manage Libraries...","Ctrl+⇧+L"},{"---",""},{"Add #include to Sketch",""}});
}

// ═══════════════════════════════════════════════════════════════════════════
// TOOLBAR
// ═══════════════════════════════════════════════════════════════════════════

static void drawToolbar() {
    int ty = MENUBAR_H;
    qFill(0, ty, width, TOOLBAR_H, 42,42,42);
    qLine(0, ty+TOOLBAR_H-1, width, ty+TOOLBAR_H-1, 58,58,58);

    // Sketch title
    std::string title = currentFile.empty() ? "untitled" : currentFile;
    if (modified) title += " *";
    iText(title, 10, ty+TOOLBAR_H*0.68f, 170,180,220, FSS);

    // Vim mode indicator
    if (vimMode) {
        std::string mode = vimInsert ? "INSERT" : "NORMAL";
        int mr = vimInsert ? 217 : 17, mg = vimInsert ? 119 : 108, mb = vimInsert ? 87 : 179;
        qFill(textWidth(title)+18, ty+8, textWidth(mode)+12, TOOLBAR_H-16, mr,mg,mb);
        iText(mode, textWidth(title)+24, ty+TOOLBAR_H*0.68f, 255,255,255, FST);
    }

    float by = ty+6, bh = TOOLBAR_H-12, bw = 92;

    // Build button
    float bx = width - 196;
    bool bHov = mouseX>=bx && mouseX<=bx+bw && mouseY>=by && mouseY<=by+bh;
    qFill(bx, by, bw, bh, bHov?217:160, bHov?119:80, bHov?87:55);
    qBorder(bx, by, bw, bh, bHov?217:180, bHov?119:90, bHov?87:55);
    iText("▶ Build", bx+10, by+bh*0.72f, 255,235,225, FSS);

    // Run button
    float rx = width - 96;
    bool rHov = mouseX>=rx && mouseX<=rx+bw && mouseY>=by && mouseY<=by+bh;
    qFill(rx, by, bw, bh, rHov?25:17, rHov?130:108, rHov?210:179);
    qBorder(rx, by, bw, bh, rHov?70:40, rHov?160:120, rHov?220:180);
    iText("▶ Run", rx+14, by+bh*0.72f, 230,240,255, FSS);

    // Terminal position toggle button
    float tpbx = width-300, tpby = by, tpbw = 84, tpbh = bh;
    bool tpHov = mouseX>=tpbx && mouseX<=tpbx+tpbw && mouseY>=tpby && mouseY<=tpby+tpbh;
    bool termRight = (terminalPos==TermPos::Right);
    qFill(tpbx, tpby, tpbw, tpbh, tpHov?44:30, tpHov?46:32, tpHov?62:46);
    qBorder(tpbx, tpby, tpbw, tpbh, 65,65,65);
    iText(termRight ? "[=] Bottom" : "[|] Right", tpbx+6, tpby+tpbh*0.72f, 160,170,210, FST);

    // Error/OK dot
    if      (hasError)
        qFill(width-210, by+bh/2-5, 10, 10, 220,60,60);
    else if (!outLines.empty() && outLines.back().find("✓") != std::string::npos)
        qFill(width-210, by+bh/2-5, 10, 10, 60,200,60);
}

// ═══════════════════════════════════════════════════════════════════════════
// STATUS BAR
// ═══════════════════════════════════════════════════════════════════════════

static void drawStatus() {
    int sy = statusY();
    qFill(0, sy, width, STATUS_H, 30,30,30);
    qLine(0, sy, width, sy, 55,55,55);

    std::string s = "Ln " + std::to_string(curLine+1) +
                    "  Col " + std::to_string(curCol+1) +
                    "  |  " + std::to_string((int)code.size()) + " lines";
    if (!currentFile.empty()) s += "  |  " + currentFile;
    if (!sketchBin.empty())   s += "  →  ./" + sketchBin;
    iText(s, 8, sy+STATUS_H*0.76f, 125,130,158, FST);

    // Right side: serial status + encoding
    std::string right = (serialFd>=0 ? "⚡ "+serialPort : "No port") + "  UTF-8  C++17";
    textSize(FST); float rw = textWidth(right);
    iText(right, width-rw-8, sy+STATUS_H*0.76f, serialFd>=0 ? 80 : 90, serialFd>=0 ? 210 : 95, serialFd>=0 ? 80 : 110, FST);
}

// ═══════════════════════════════════════════════════════════════════════════
// EDITOR
// ═══════════════════════════════════════════════════════════════════════════

static void drawEditor() {
    int ey = editorY(), eh = editorH();
    float lh = lineH(), asc = FS * 0.80f;

    int ex = sbW();
    int ew = editorFullW();
    qFill(ex, ey, ew, eh, 30,30,30);             // editor bg
    qFill(ex, ey, GUTTER_W, eh, 38,38,38);       // gutter bg
    qLine(ex+GUTTER_W, ey, ex+GUTTER_W, ey+eh, 58,58,58);

    int vis = visLines();
    for (int i = 0; i < vis; i++) {
        int  li      = scrollTop + i;
        if (li >= (int)code.size()) break;
        float rowTop  = ey + i*lh;
        float baseline= rowTop + asc + 2;

        // Current line highlight
        if (li == curLine)
            qFill(ex+GUTTER_W, rowTop, ew-GUTTER_W, lh, 44,44,50);

        // Selection highlight
        if (hasSel()) {
            int l0,c0,l1,c1; selRange(l0,c0,l1,c1);
            if (li >= l0 && li <= l1) {
                float sx = (li==l0) ? xOf(li,c0) : (float)(ex+GUTTER_W+4);
                float rx2 = (li==l1) ? xOf(li,c1) : (float)(ex+ew-8);
                float ex2 = rx2;
                if (ex2 > sx) qFill(sx, rowTop+1, ex2-sx, lh-2, 17,60,110, 210);
            }
        }

        // Line number (highlighted on current line)
        textSize(FST);
        std::string num = std::to_string(li+1);
        float nw = textWidth(num);
        if (li == curLine) iText(num, ex+GUTTER_W-6-nw, baseline-1, 200,204,215, FST);
        else               iText(num, ex+GUTTER_W-6-nw, baseline-1,  85, 90,112, FST);

        // Syntax-highlighted tokens
        textSize(FS);
        float tx = ex + GUTTER_W + 4;
        for (auto& tok : tokenize(code[li])) {
            fill(tok.r, tok.g, tok.b); noStroke();
            text(tok.s, tx, baseline);
            tx += textWidth(tok.s);
        }

        // Blinking cursor (hide in vim NORMAL if on non-insert line)
        bool showCursor = (li == curLine) && ((frameCount/22) % 2 == 0);
        if (vimMode && !vimInsert) showCursor = (li == curLine);  // solid in NORMAL
        if (showCursor) {
            float cx = xOf(li, curCol);
            if (vimMode && !vimInsert)
                qFill(cx, rowTop+2, FS*0.6f, lh-4, 200,190,100, 160);  // block cursor
            else
                qFill(cx, rowTop+2, 2, lh-4, 220,210,160);              // bar cursor
        }
    }

    // Vertical scrollbar
    qFill(ex+ew-8, ey, 8, eh, 36,36,36);
    if ((int)code.size() > vis) {
        float sbH = std::max(14.0f, (float)vis/code.size()*eh);
        float sbY = ey + (float)scrollTop/code.size()*eh;
        qFill(ex+ew-8, sbY, 8, sbH, 80,80,80);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CONSOLE OUTPUT
// ═══════════════════════════════════════════════════════════════════════════

// Console: selected line index for copy
static int consoleSelLine = -1;

static void drawConsole() {
    int cy = consoleY(), cx = consoleX(), cw = consoleW();
    int consH = (terminalPos==TermPos::Right ? height-editorY() : CONSOLE_H);
    float lh = FSS * 1.5f;

    // ── Resize handle ────────────────────────────────────────────────────
    bool onHandle = (terminalPos==TermPos::Bottom) ?
        (mouseY >= cy && mouseY <= cy+4) :
        (mouseX >= cx && mouseX <= cx+4 && mouseY >= cy && mouseY <= cy+consH);
    if (terminalPos==TermPos::Bottom)
        qFill(cx, cy, cw, 4, (onHandle||consoleResizing)?17:45,
              (onHandle||consoleResizing)?108:50, (onHandle||consoleResizing)?179:75);
    else
        qFill(cx, cy, 4, consH, (onHandle||termSideResizing)?17:45,
              (onHandle||termSideResizing)?108:50, (onHandle||termSideResizing)?179:75);

    // ── Background ───────────────────────────────────────────────────────
    qFill(cx+4, cy+4, cw-4, consH-4, 20,20,20);

    // ── Tab bar ──────────────────────────────────────────────────────────
    qFill(cx+4, cy+4, cw-4, TAB_H, 24,24,24);
    qLine(cx+4, cy+4+TAB_H, cx+cw, cy+4+TAB_H, 52,52,52);

    float tx = cx + 8;
    for (int i = 0; i < (int)terminals.size(); i++) {
        textSize(FST);
        float tw = textWidth(terminals[i].name) + 20;
        bool isActive = (i == activeTab);
        bool tabHov   = (mouseX>=tx && mouseX<=tx+tw &&
                         mouseY>=cy+4 && mouseY<=cy+4+TAB_H);
        qFill(tx, cy+4, tw, TAB_H,
              isActive?32:(tabHov?30:22),
              isActive?33:(tabHov?31:23),
              isActive?46:(tabHov?44:32));
        if (isActive)
            qLine(tx, cy+4+TAB_H-1, tx+tw, cy+4+TAB_H-1, 17,108,179);
        if (terminals[i].hasError)
            qFill(tx+tw-10, cy+4+7, 6, 6, 220,60,60);
        else if (!terminals[i].lines.empty())
            qFill(tx+tw-10, cy+4+7, 6, 6, 55,190,55);
        fill(isActive?220:140, isActive?224:145, isActive?240:165);
        text(terminals[i].name, tx+6, cy+4+TAB_H-6);
        tx += tw + 2;
    }

    // ── New-tab (+) button ───────────────────────────────────────────────
    bool newTabHov = (mouseX>=tx && mouseX<=tx+20 &&
                      mouseY>=cy+4 && mouseY<=cy+4+TAB_H);
    qFill(tx, cy+4, 20, TAB_H,
          newTabHov?38:26, newTabHov?40:28, newTabHov?56:40);
    fill(newTabHov?200:120, newTabHov?210:130, newTabHov?255:180);
    text("+", tx+5, cy+4+TAB_H-6);

    // ── Copy All button ──────────────────────────────────────────────────
    float cbx=cx+cw-84, cby=cy+7, cbw=76, cbh=16;
    bool cbHov = (mouseX>=cbx && mouseX<=cbx+cbw &&
                  mouseY>=cby && mouseY<=cby+cbh);
    qFill(cbx, cby, cbw, cbh, cbHov?50:34, cbHov?52:36, cbHov?66:50);
    qBorder(cbx, cby, cbw, cbh, 72,72,72);
    iText("Copy All", cbx+6, cby+cbh*0.82f, 172,177,212, 10.0f);

    // ── Output lines (active tab) ────────────────────────────────────────
    auto& tlines  = terminals[activeTab].lines;
    auto& tscroll = terminals[activeTab].scroll;
    int contentH  = consH - 4 - TAB_H;
    int visOut    = std::max(1, (int)(contentH / lh));
    tscroll = std::max(0, std::min(tscroll, std::max(0,(int)tlines.size()-visOut)));

    for (int i = 0; i < visOut; i++) {
        int li = tscroll + i;
        if (li >= (int)tlines.size()) break;
        auto& s  = tlines[li];
        float rowTop  = cy + 4 + TAB_H + i*lh;
        float baseline= rowTop + lh - 3;

        bool hov = (mouseX>cx+4 && mouseX<cx+cw-12 &&
                    mouseY>=rowTop && mouseY<rowTop+lh);
        bool sel = (li == consoleSelLine);
        if (sel)      qFill(cx+4, rowTop, cw-10, lh, 40,60,110);
        else if (hov) qFill(cx+4, rowTop, cw-10, lh, 30,30,30);

        bool isErr  = s.find("error:")   != std::string::npos
                   || s.find("\xe2\x9c\x97") != std::string::npos;
        bool isWarn = s.find("warning:") != std::string::npos;
        bool isOk   = s.find("\xe2\x9c\x93") != std::string::npos
                   || s.find("successful") != std::string::npos;
        bool isCmd  = !s.empty() && s[0]=='$';
        bool isMeta = !s.empty() && s[0]=='[';

        int r=188,g=190,b=198;
        if      (isErr)  { r=255; g=100; b=100; }
        else if (isWarn) { r=255; g=200; b=60;  }
        else if (isOk)   { r=80;  g=210; b=100; }
        else if (isCmd)  { r=100; g=180; b=255; }
        else if (isMeta) { r=128; g=132; b=162; }

        textSize(FSS);
        std::string disp = s;
        while (disp.size()>1 && textWidth(disp)>cw-22) disp.pop_back();
        if (disp.size() < s.size() && !disp.empty()) disp.back() = '>';
        fill(r,g,b); noStroke(); text(disp, cx+14, baseline);
    }

    // ── Scrollbar ────────────────────────────────────────────────────────
    if ((int)tlines.size() > visOut) {
        float th  = contentH;
        float sbH = std::max(6.0f, (float)visOut/tlines.size()*th);
        float sbY = cy + 4 + TAB_H + (float)tscroll/tlines.size()*th;
        qFill(cx+cw-6, cy+4+TAB_H, 6, th, 38,38,38);
        qFill(cx+cw-6, sbY, 6, sbH, 80,80,80);
    }
}
// SERIAL MONITOR OVERLAY
// ═══════════════════════════════════════════════════════════════════════════

static void drawSerialMonitor() {
    pollSerial();

    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    qFill(0, 0, width, height, 0,0,0, 175);

    // Panel dimensions — nice and roomy
    float pw=640, ph=500, px=(width-pw)*0.5f, py=(height-ph)*0.5f;
    qFill(px, py, pw, ph, 26,26,26);
    qBorder(px, py, pw, ph, 65,65,65);

    // ── Title bar ──────────────────────────────────────────────────────────
    qFill(px, py, pw, 36, 34,34,34);
    qLine(px, py+36, px+pw, py+36, 58,58,58);
    iText("Serial Monitor", px+14, py+25, 222,228,255, FS);

    // Close button
    bool xhov = mouseX>=px+pw-32 && mouseX<=px+pw-6 && mouseY>=py+6 && mouseY<=py+30;
    qFill(px+pw-32, py+6, 26, 24, xhov?190:78, 45, 52);
    qBorder(px+pw-32, py+6, 26, 24, xhov?240:110, 55, 60);
    iText("✕", px+pw-24, py+23, 240,242,248, FSS);

    // ── Port selector ──────────────────────────────────────────────────────
    float portY = py+46;
    qFill(px, portY, pw, 36, 30,30,30);
    qLine(px, portY+36, px+pw, portY+36, 52,52,52);
    iText("Port:", px+10, portY+24, 148,155,185, FST);

    auto ports = listPorts();
    float bx = px+54;
    if (ports.empty()) {
        qFill(bx, portY+6, 180, 24, 38,40,52);
        qBorder(bx, portY+6, 180, 24, 60,60,60);
        iText("No USB/serial devices found", bx+8, portY+22, 130,135,160, FST);
    }
    for (int i = 0; i < (int)ports.size() && i < 4; i++) {
        float bw = 148; float bx2 = bx + i*(bw+4);
        bool sel = (ports[i]==serialPort);
        bool hov = mouseX>=bx2 && mouseX<=bx2+bw && mouseY>=portY+6 && mouseY<=portY+30;
        qFill(bx2, portY+6, bw, 24, sel?28:hov?38:26, sel?95:hov?42:28, sel?210:hov?58:40);
        qBorder(bx2, portY+6, bw, 24, sel?60:50, sel?150:58, sel?240:78);
        iText(ports[i], bx2+8, portY+21, sel?220:190, sel?230:195, sel?255:220, FST);
    }

    // ── Baud rate selector ─────────────────────────────────────────────────
    float baudY = portY+42;
    qFill(px, baudY, pw, 36, 28,28,28);
    qLine(px, baudY+36, px+pw, baudY+36, 52,52,52);
    iText("Baud:", px+10, baudY+24, 148,155,185, FST);

    float bbx = px+54;
    for (int i = 0; i < (int)BAUD_RATES.size(); i++) {
        std::string bs = std::to_string(BAUD_RATES[i]);
        textSize(FST);
        float bw = textWidth(bs) + 14;
        bool sel = (i==baudIndex);
        bool hov = mouseX>=bbx && mouseX<=bbx+bw && mouseY>=baudY+6 && mouseY<=baudY+30;
        qFill(bbx, baudY+6, bw, 24, sel?28:hov?36:22, sel?95:hov?42:26, sel?210:hov?55:38);
        qBorder(bbx, baudY+6, bw, 24, sel?60:45, sel?150:55, sel?240:72);
        iText(bs, bbx+7, baudY+21, sel?220:172, sel?230:176, sel?255:200, FST);
        bbx += bw + 4;
    }

    // ── Connect / Disconnect button ────────────────────────────────────────
    bool conn = (serialFd >= 0);
    float cbx2 = px+pw-130, cby2 = py+46, cbw2 = 122, cbh2 = 72;
    bool chov = mouseX>=cbx2 && mouseX<=cbx2+cbw2 && mouseY>=cby2 && mouseY<=cby2+cbh2;
    qFill(cbx2, cby2, cbw2, cbh2,
          conn?(chov?130:95):(chov?32:20),
          conn?(chov?45:35):(chov?128:96),
          conn?(chov?45:35):(chov?218:175));
    qBorder(cbx2, cby2, cbw2, cbh2,
            conn?150:45, conn?55:148, conn?55:240);
    std::string connLabel = conn ? "● Disconnect" : "● Connect";
    iText(connLabel, cbx2+10, cby2+cbh2*0.58f, conn?255:200, conn?160:230, conn?160:255, FSS);
    if (conn) {
        iText(serialPort, cbx2+10, cby2+cbh2*0.82f, 140,220,140, 10.0f);
    }

    // ── Log area ───────────────────────────────────────────────────────────
    float logy = baudY+42, logh = ph-210;
    qFill(px, logy, pw, logh, 16,16,16);
    qBorder(px, logy, pw, logh, 50,50,50);

    float llh = FST * 1.6f;
    int visLog = (int)((logh-8) / llh);
    serialScroll = std::max(0, std::min(serialScroll, std::max(0,(int)serialLog.size()-visLog)));

    for (int i = 0; i < visLog; i++) {
        int li = serialScroll + i;
        if (li >= (int)serialLog.size()) break;
        auto& s = serialLog[li];
        float rowY = logy+4+i*llh;
        bool rhov = mouseX>=px+4 && mouseX<=px+pw-12 && mouseY>=rowY && mouseY<rowY+llh;
        if (rhov) qFill(px+2, rowY, pw-14, llh, 28,30,44);
        bool isErr  = s.find("ERROR") != std::string::npos;
        bool isConn = s.find("Connected") != std::string::npos || s.find("Disconnected") != std::string::npos;
        bool isSend = s.size()>=2 && s[0]=='>' && s[1]==' ';
        int r=182,g=186,b=194;
        if (isErr)  { r=255; g=100; b=100; }
        if (isConn) { r=80;  g=200; b=100; }
        if (isSend) { r=100; g=180; b=255; }
        iText(s, px+8, rowY+llh-3, r,g,b, FST);
    }

    // Log scrollbar
    if ((int)serialLog.size() > visLog) {
        float sbH = std::max(8.0f, (float)visLog/serialLog.size()*logh);
        float sbY = logy + (float)serialScroll/serialLog.size()*logh;
        qFill(px+pw-7, logy, 7, logh, 34,34,34);
        qFill(px+pw-7, sbY, 7, sbH, 85,90,125);
    }

    // ── Send bar ───────────────────────────────────────────────────────────
    float sendY = logy + logh + 4;
    qFill(px, sendY, pw, 44, 28,30,42);
    qBorder(px, sendY, pw, 44, 50,52,68);

    // Input field
    float inpW = pw - 176;
    qFill(px+8, sendY+8, inpW, 28, 16,17,24);
    qBorder(px+8, sendY+8, inpW, 28, conn?55:40, conn?85:44, conn?180:60);
    if (conn) {
        fill(215,220,238); textSize(FSS);
        text(serialInput+"▌", px+14, sendY+26);
    } else {
        iText("Connect to a port first", px+14, sendY+26, 90,95,120, FST);
    }

    // Send button
    float sx2 = px+pw-162; float sw2 = 72;
    bool sbHov = mouseX>=sx2 && mouseX<=sx2+sw2 && mouseY>=sendY+8 && mouseY<=sendY+36;
    qFill(sx2, sendY+8, sw2, 28, sbHov?36:22, sbHov?110:78, sbHov?205:165);
    qBorder(sx2, sendY+8, sw2, 28, 45,125,215);
    iText("Send", sx2+16, sendY+25, 200,225,255, FSS);

    // Clear button
    float cx2 = px+pw-82; float cw2 = 72;
    bool clHov = mouseX>=cx2 && mouseX<=cx2+cw2 && mouseY>=sendY+8 && mouseY<=sendY+36;
    qFill(cx2, sendY+8, cw2, 28, clHov?72:48, clHov?38:28, clHov?38:28);
    qBorder(cx2, sendY+8, cw2, 28, 105,48,48);
    iText("Clear", cx2+12, sendY+25, 225,175,175, FSS);
}

// ═══════════════════════════════════════════════════════════════════════════
// LIBRARY MANAGER OVERLAY
// ═══════════════════════════════════════════════════════════════════════════

static void drawLibMgr() {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    qFill(0, 0, width, height, 0,0,0, 175);

    float pw=660, ph=460, px=(width-pw)*0.5f, py=(height-ph)*0.5f;
    qFill(px, py, pw, ph, 32,33,42);
    qBorder(px, py, pw, ph, 72,75,92);
    qFill(px, py, pw, 32, 40,42,54);
    qLine(px, py+32, px+pw, py+32, 60,62,78);
    iText("Library Manager", px+12, py+23, 228,232,255, FS);

    bool xhov = mouseX>=px+pw-28 && mouseX<=px+pw-8 && mouseY>=py+6 && mouseY<=py+26;
    qFill(px+pw-28, py+6, 20, 20, xhov?200:85, 50, 58);
    iText("✕", px+pw-21, py+21, 240,242,248, FSS);

    if (!libStatus.empty()) {
        qFill(px+4, py+36, pw-8, 18, 26,46,26);
        qBorder(px+4, py+36, pw-8, 18, 48,118,48);
        iText(libStatus, px+10, py+49, 128,220,128, FST);
    }

    float ly = py+58, rowH = 34;
    int visLib = (int)((ph-66) / rowH);
    libScroll = std::max(0, std::min(libScroll, std::max(0,(int)libraries.size()-visLib)));

    // Column headers
    qFill(px, ly-2, pw, 20, 40,42,54);
    iText("Library", px+10, ly+13, 148,152,178, FST);
    iText("Description", px+180, ly+13, 148,152,178, FST);
    iText("Action", px+pw-108, ly+13, 148,152,178, FST);
    ly += 20;

    for (int i = 0; i < visLib; i++) {
        int li = libScroll + i;
        if (li >= (int)libraries.size()) break;
        auto& lib = libraries[li];
        float ry = ly + i*rowH;

        bool hov = mouseX>=px && mouseX<=px+pw && mouseY>=ry && mouseY<=ry+rowH;
        qFill(px, ry, pw, rowH, hov?38:28, hov?40:29, hov?52:38);
        qLine(px, ry+rowH-1, px+pw, ry+rowH-1, 48,50,62);

        // Truncate name
        std::string nm = lib.name;
        textSize(FST);
        while (nm.size()>1 && textWidth(nm)>165) nm.pop_back();
        iText(nm, px+10, ry+rowH*0.72f, 208,212,238, FST);

        // Truncate description
        std::string desc = lib.desc;
        while (desc.size()>1 && textWidth(desc)>pw-300) desc.pop_back();
        if (desc.size()<lib.desc.size() && !desc.empty()) desc.back()='.';
        iText(desc, px+180, ry+rowH*0.72f, 152,155,178, FST);

        // Action button
        float bx2=px+pw-106, by2=ry+5, bw2=98, bh2=rowH-10;
        if (lib.name == "Vim keybindings") {
            bool ton = vimMode;
            qFill(bx2, by2, bw2, bh2, ton?30:25, ton?90:68, ton?40:155);
            qBorder(bx2, by2, bw2, bh2, ton?50:40, ton?160:110, ton?60:220);
            iText(ton?"✓ Enabled":"Enable", bx2+8, by2+bh2*0.72f, ton?140:200, ton?240:224, ton?140:255, FST);
        } else if (lib.installed) {
            bool ahov = mouseX>=bx2 && mouseX<=bx2+bw2 && mouseY>=by2 && mouseY<=by2+bh2;
            qFill(bx2, by2, bw2, bh2, ahov?38:28, ahov?110:88, ahov?40:32);
            qBorder(bx2, by2, bw2, bh2, 48,158,58);
            iText("+ Add #include", bx2+4, by2+bh2*0.72f, 136,238,148, 10.0f);
        } else if (li == installingLib) {
            qFill(bx2, by2, bw2, bh2, 96,78,18);
            iText("Installing…", bx2+4, by2+bh2*0.72f, 238,208,98, FST);
        } else {
            bool ibhov = mouseX>=bx2 && mouseX<=bx2+bw2 && mouseY>=by2 && mouseY<=by2+bh2;
            qFill(bx2, by2, bw2, bh2, ibhov?38:22, ibhov?98:68, ibhov?198:158);
            qBorder(bx2, by2, bw2, bh2, ibhov?78:48, ibhov?158:108, ibhov?255:210);
            iText("↓ Install", bx2+12, by2+bh2*0.72f, 198,222,255, FST);
        }
    }

    // Lib scrollbar
    if ((int)libraries.size() > visLib) {
        float th  = visLib * rowH;
        float sbH = std::max(12.0f, (float)visLib/libraries.size()*th);
        float sbY = ly + (float)libScroll/libraries.size()*th;
        qFill(px+pw-6, ly, 6, th, 40,42,55);
        qFill(px+pw-6, sbY, 6, sbH, 88,92,128);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FILE PICKER (inline in console zone)
// ═══════════════════════════════════════════════════════════════════════════

static bool fpShow=false, fpSave=false;
static std::string fpInput="";

static void drawFilePicker() {
    int cy = consoleY();
    qFill(0, cy, width, CONSOLE_H, 22,22,22);
    qFill(0, cy, width, 3, 17,108,179);  // blue accent bar
    qLine(0, cy+3, width, cy+3, 17,108,179);

    iText(fpSave?"Save Sketch As:":"Open Sketch:", 10, cy+20, 188,198,238, FSS);

    // Input box
    qFill(10, cy+25, width-94, 26, 16,17,24);
    qBorder(10, cy+25, width-94, 26, 17,108,179);
    fill(220,224,240); textSize(FSS); text(fpInput+"▌", 16, cy+42);

    // Browse button
    float bbx=width-78, bby=cy+25, bbw=68, bbh=26;
    bool bbhov = mouseX>=bbx && mouseX<=bbx+bbw && mouseY>=bby && mouseY<=bby+bbh;
    qFill(bbx, bby, bbw, bbh, bbhov?38:22, bbhov?98:68, bbhov?198:158);
    qBorder(bbx, bby, bbw, bbh, 58,118,218);
    iText("Browse…", bbx+4, bby+bbh*0.76f, 198,218,255, FST);

    iText("Enter=confirm   Esc=cancel   or click file:", 10, cy+62, 108,112,142, FST);

    auto files = listSketches();
    for (int i = 0; i < (int)files.size() && i < 6; i++) {
        float ry = cy + 74 + i*19;
        bool hov = mouseX>=10 && mouseX<=380 && mouseY>=ry && mouseY<ry+19;
        if (hov) qFill(8, ry, 374, 18, 30,78,158);
        iText(files[i], 14, ry+14, hov?240:158, hov?244:165, hov?255:198, FST);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP / DRAW
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    size(1040, 720);
    windowResizable(true);
    frameRate(60);
    checkInstalled();
    outLines.push_back("Processing CPP IDE ready.");
    outLines.push_back("Ctrl+B build  |  Ctrl+R build+run  |  Ctrl+Shift+M serial monitor  |  Ctrl+Shift+L libraries");
}

void draw() {
    background(30,30,30);

    // Base layers (back to front)
    drawSidebar();
    if (!fpShow) { drawEditor(); drawStatus(); }
    drawConsole();
    if (fpShow) drawFilePicker();

    // Fixed chrome on top
    drawToolbar();
    drawMenuBar();

    // Modals (topmost)
    if (showSerial) drawSerialMonitor();
    if (showLibMgr) drawLibMgr();
}

// ═══════════════════════════════════════════════════════════════════════════
// MOUSE
// ═══════════════════════════════════════════════════════════════════════════

static bool dragging = false;

static void mouseToLC(int& li, int& col) {
    float lh = lineH();
    li  = scrollTop + (int)((mouseY - editorY()) / lh);
    li  = std::max(0, std::min(li, (int)code.size()-1));
    textSize(FS);
    float tx = sbW() + GUTTER_W + 4; col = 0;
    for (int c = 0; c < (int)code[li].size(); c++) {
        float cw = textWidth(std::string(1, code[li][c]));
        if (tx + cw*0.5f > mouseX) break;
        tx += cw; col = c+1;
    }
}

void mousePressed() {

    // ── Serial monitor ──────────────────────────────────────────────────
    if (showSerial) {
        float pw=640, ph=500, px=(width-pw)*0.5f, py=(height-ph)*0.5f;

        // Close
        if (mouseX>=px+pw-32 && mouseX<=px+pw-6 && mouseY>=py+6 && mouseY<=py+30)
            { showSerial=false; return; }

        // Port buttons
        float portY=py+46;
        auto ports=listPorts();
        float bx=px+54;
        for (int i=0; i<(int)ports.size()&&i<4; i++) {
            float bw=148, bx2=bx+i*(bw+4);
            if (mouseX>=bx2 && mouseX<=bx2+bw && mouseY>=portY+6 && mouseY<=portY+30)
                { serialPort=ports[i]; return; }
        }

        // Baud buttons — same dynamic widths as draw
        float baudY=portY+42;
        float bbx=px+54;
        for (int i=0; i<(int)BAUD_RATES.size(); i++) {
            std::string bs=std::to_string(BAUD_RATES[i]);
            textSize(FST);
            float bw=textWidth(bs)+14;
            if (mouseX>=bbx && mouseX<=bbx+bw && mouseY>=baudY+6 && mouseY<=baudY+30)
                { baudIndex=i; return; }
            bbx+=bw+4;
        }

        // Connect/Disconnect
        float cbx2=px+pw-130, cby2=py+46, cbw2=122, cbh2=72;
        if (mouseX>=cbx2 && mouseX<=cbx2+cbw2 && mouseY>=cby2 && mouseY<=cby2+cbh2) {
            if (serialFd>=0) closeSerial();
            else {
                std::string port = serialPort.empty()
                    ? (ports.empty() ? "" : ports[0])
                    : serialPort;
                if (!port.empty()) openSerial(port, BAUD_RATES[baudIndex]);
                else serialLog.push_back("ERROR: no port selected");
            }
            return;
        }

        // Send / Clear
        float logy=baudY+42, logh=ph-210;
        float sendY=logy+logh+4;
        float inpW=pw-176;
        float sx2=px+pw-162;
        float cx2=px+pw-82, cw2=72;

        if (mouseX>=sx2 && mouseX<=sx2+72 && mouseY>=sendY+8 && mouseY<=sendY+36) {
            if (serialFd>=0 && !serialInput.empty()) {
                std::string s=serialInput+"\n";
                write(serialFd, s.c_str(), s.size());
                serialLog.push_back("> "+serialInput);
                serialInput="";
                serialScroll=std::max(0,(int)serialLog.size()-16);
            }
            return;
        }
        if (mouseX>=cx2 && mouseX<=cx2+cw2 && mouseY>=sendY+8 && mouseY<=sendY+36)
            { serialLog.clear(); serialScroll=0; return; }

        return;
    }

    // ── Library manager ─────────────────────────────────────────────────
    if (showLibMgr) {
        float pw=660, ph=460, px=(width-pw)*0.5f, py=(height-ph)*0.5f;
        if (mouseX>=px+pw-28 && mouseX<=px+pw-8 && mouseY>=py+6 && mouseY<=py+26)
            { showLibMgr=false; return; }

        float ly=py+58+20, rowH=34;
        int visLib=(int)((ph-66)/rowH);
        for (int i=0; i<visLib; i++) {
            int li=libScroll+i; if(li>=(int)libraries.size())break;
            auto&lib=libraries[li]; float ry=ly+i*rowH;
            float bx2=px+pw-106, by2=ry+5, bw2=98, bh2=rowH-10;
            if (mouseX>=bx2 && mouseX<=bx2+bw2 && mouseY>=by2 && mouseY<=by2+bh2) {
                if (lib.name=="Vim keybindings") {
                    vimMode=!vimMode; vimInsert=false;
                    lib.installed=vimMode;
                    libStatus=vimMode?"Vim mode enabled":"Vim mode disabled";
                    return;
                }
                if (lib.installed) {
                    // Insert after last existing #include line, or at top
                    int insertAt = 0;
                    for (int ci=0; ci<(int)code.size(); ci++)
                        if (code[ci].find("#include") != std::string::npos) insertAt=ci+1;
                    code.insert(code.begin()+insertAt, lib.header);
                    modified=true;
                    libStatus="Added: "+lib.header;
                    outLines.push_back("Added to sketch: "+lib.header);
                } else {
                    installingLib=li; libStatus="Installing "+lib.name+"…";
                    // Build the correct install command for this distro
                    std::string installCmd = lib.pkg.empty()
                        ? lib.installCmd   // header-only: use curl/wget as-is
                        : buildInstallCmd(lib.pkg);
                    outLines.push_back("$ "+installCmd);
                    int ret=system(installCmd.c_str());
                    lib.installed=(ret==0);
                    if (ret==0) {
                        checkInstalled();  // re-detect so lib.installed is now true
                        libStatus="✓ Installed: "+lib.name+" — click again to add #include";
                        outLines.push_back("✓ Installed: "+lib.name);
                        if (!lib.linkFlag.empty() && buildFlags.find(lib.linkFlag)==std::string::npos)
                            buildFlags += " " + lib.linkFlag;
                    } else {
                        libStatus="✗ Failed — run manually: "+installCmd;
                        outLines.push_back("✗ Failed. Try: "+installCmd);
                    }
                    installingLib=-1;
                }
                return;
            }
        }
        return;
    }

    // ── Sidebar resize (drag right edge) ────────────────────────────────
    if (sidebarVisible && mouseX >= SIDEBAR_W-4 && mouseX <= SIDEBAR_W+2
        && mouseY >= editorY()) {
        sidebarResizing      = true;
        sidebarResizeAnchorX = mouseX;
        sidebarResizeAnchorW = SIDEBAR_W;
        return;
    }

    // ── Sidebar file tree click ──────────────────────────────────────────
    if (sidebarVisible && mouseX < SIDEBAR_W && mouseY >= editorY()+50) {
        float rowH = FSS*1.7f;
        int treeTop = editorY()+50;
        int vi = (int)((mouseY - treeTop) / rowH);
        int fi = ftScroll + vi;
        if (fi >= 0 && fi < (int)ftEntries.size()) {
            auto& fe = ftEntries[fi];
            if (fe.isDir) {
                fe.expanded = !fe.expanded;
                populateTree();
            } else {
                // Open file in editor
                std::string path = ftRoot + "/" + fe.name;
                // walk depth for full path
                openFile(path);
            }
        }
        return;
    }

    // ── File picker ─────────────────────────────────────────────────────
    if (fpShow) {
        int cy=consoleY();
        float bbx=width-78, bby=cy+25, bbw=68, bbh=26;
        if (mouseX>=bbx && mouseX<=bbx+bbw && mouseY>=bby && mouseY<=bby+bbh) {
            std::string path=sysFileDialog(fpSave, fpSave?fpInput:"");
            if (!path.empty()) { if(fpSave)saveFile(path);else openFile(path); fpShow=false; }
            return;
        }
        auto files=listSketches();
        for (int i=0; i<(int)files.size()&&i<6; i++) {
            float ry=cy+74+i*19;
            if (mouseX>=10 && mouseX<=380 && mouseY>=ry && mouseY<ry+19) {
                if (fpSave) saveFile(files[i]); else openFile(files[i]);
                fpShow=false; return;
            }
        }
        return;
    }

    // ── Menu bar ─────────────────────────────────────────────────────────
    if (mouseY < MENUBAR_H) {
        // Recompute positions exactly as drawMenuBar does
        struct MH { std::string label; Menu id; float x=0, w=0; };
        std::vector<MH> hs = {
            {"File",Menu::File}, {"Edit",Menu::Edit},
            {"Sketch",Menu::Sketch}, {"Tools",Menu::Tools},
            {"Libraries",Menu::Libraries}
        };
        textSize(FST);
        float mx2 = 6;
        for (auto& h : hs) { h.x=mx2; h.w=textWidth(h.label)+14; mx2+=h.w+2; }
        for (auto& h : hs) {
            if (mouseX >= h.x-2 && mouseX <= h.x+h.w) {
                openMenu = (openMenu==h.id) ? Menu::None : h.id;
                return;
            }
        }
        openMenu=Menu::None; return;
    }

    // ── Dropdown items ──────────────────────────────────────────────────
    if (openMenu != Menu::None) {
        float mx=0; std::vector<MI> items;
        if      (openMenu==Menu::File)      { mx=2;   items={{"New",""},{"Open...",""},{"---",""},{"Save",""},{"Save As...",""},{"---",""},{"Exit",""}}; }
        else if (openMenu==Menu::Edit)      { mx=42;  items={{"Undo",""},{"Redo",""},{"---",""},{"Cut",""},{"Copy",""},{"Paste",""},{"---",""},{"Select All",""},{"Duplicate Line",""},{"---",""},{"Toggle Comment",""},{"Auto Format",""}}; }
        else if (openMenu==Menu::Sketch)    { mx=84;  items={{"Build",""},{"Run (build+run)",""},{"---",""},{"Show Folder",""},{"Export Binary",""}}; }
        else if (openMenu==Menu::Tools)     { mx=138; items={{"Serial Monitor",""},{"---",""},{"Toggle Vim Mode",""},{"---",""},{"Auto Format",""},{"---",""},{"Increase Font",""},{"Decrease Font",""}}; }
        else if (openMenu==Menu::Libraries) { mx=182; items={{"Manage Libraries...",""},{"---",""},{"Add #include to Sketch",""}}; }

        float my=MENUBAR_H, pw=210;
        for (int i=0; i<(int)items.size(); i++) {
            float ry=my+4+i*22;
            if (mouseX>=mx && mouseX<=mx+pw && mouseY>=ry && mouseY<=ry+22 && items[i].label!="---") {
                std::string lbl=items[i].label; openMenu=Menu::None;
                if      (lbl=="New")               newFile();
                else if (lbl=="Open...")          { fpShow=true; fpSave=false; fpInput=""; }
                else if (lbl=="Save")             { if(currentFile.empty()){fpShow=true;fpSave=true;fpInput="";}else saveFile(currentFile); }
                else if (lbl=="Save As...")       { fpShow=true; fpSave=true; fpInput=currentFile; }
                else if (lbl=="Exit")              exit_sketch();
                else if (lbl=="Build")             doCompile();
                else if (lbl=="Run (build+run)")   doRun();
                else if (lbl=="Show Folder")       system("xdg-open . >/dev/null 2>&1 &");
                else if (lbl=="Export Binary")    { std::string dst=sysFileDialog(true,sketchBin); if(!dst.empty()) system(("cp "+sketchBin+" "+dst).c_str()); }
                else if (lbl=="Serial Monitor")   { showSerial=true; checkInstalled(); }
                else if (lbl=="Toggle Vim Mode")  { vimMode=!vimMode; vimInsert=false; }
                else if (lbl=="Auto Format")      { /* handled below */ }
                else if (lbl=="Increase Font")    { FS=std::min(32.0f,FS+1);FSS=FS-1;FST=FS-2; }
                else if (lbl=="Decrease Font")    { FS=std::max(8.0f,FS-1);FSS=FS-1;FST=FS-2; }
                else if (lbl=="Manage Libraries...")  { showLibMgr=true; checkInstalled(); }
                return;
            }
        }
        openMenu=Menu::None; return;
    }

    // ── Toolbar buttons ─────────────────────────────────────────────────
    int ty=MENUBAR_H; float by=ty+6, bh=TOOLBAR_H-12, bw=92;
    if (mouseY>=by && mouseY<=by+bh) {
        if (mouseX>=width-196 && mouseX<=width-196+bw) { doCompile(); return; }
        if (mouseX>=width-96  && mouseX<=width-96+bw)  { doRun();     return; }
        // Terminal position toggle
        float tpbx=width-300, tpbw=84;
        if (mouseX>=tpbx && mouseX<=tpbx+tpbw) {
            terminalPos = (terminalPos==TermPos::Bottom) ? TermPos::Right : TermPos::Bottom;
            return;
        }
    }
    // Sidebar toggle (click on right edge of sidebar when visible,
    //                 or a narrow strip at x=0 when hidden)
    if (mouseY >= editorY()) {
        if (!sidebarVisible && mouseX>=0 && mouseX<=8) {
            sidebarVisible = true; return;
        }
    }

    // ── Console area clicks ────────────────────────────────────────────
    {
        int cy = consoleY();
    
        // Resize handle
        int cx2 = consoleX();
        if (terminalPos==TermPos::Bottom && mouseY >= cy && mouseY <= cy+4) {
            consoleResizing      = true;
            consoleResizeAnchorY = mouseY;
            consoleResizeAnchorH = CONSOLE_H;
            return;
        }
        if (terminalPos==TermPos::Right && mouseX >= cx2 && mouseX <= cx2+4
            && mouseY >= cy && mouseY <= cy + (height-editorY())) {
            termSideResizing = true;
            termSideAnchorX  = mouseX;
            termSideAnchorW  = TERM_SIDE_W;
            return;
        }

        // Tab bar clicks
        if (mouseY >= cy+4 && mouseY <= cy+4+TAB_H) {
            float tx = 4;
            for (int i = 0; i < (int)terminals.size(); i++) {
                textSize(FST);
                float tw = textWidth(terminals[i].name) + 20;
                if (mouseX >= tx && mouseX <= tx+tw) {
                    activeTab = i;
                    consoleSelLine = -1;
                    return;
                }
                tx += tw + 2;
            }
            // New tab (+) button
            if (mouseX >= tx && mouseX <= tx+20) {
                terminals.push_back({"Tab " + std::to_string(terminals.size()+1), {}, 0, false});
                activeTab = (int)terminals.size()-1;
                consoleSelLine = -1;
                return;
            }
            return;
        }

        // Copy All button
        float cbx=width-84, cby=cy+7, cbw=76, cbh=16;
        if (mouseX>=cbx && mouseX<=cbx+cbw && mouseY>=cby && mouseY<=cby+cbh) {
            auto& tlines = terminals[activeTab].lines;
            std::string all; for(auto&l:tlines) all+=l+"\n";
            if (!all.empty()) glfwSetClipboardString(glfwGetCurrentContext(), all.c_str());
            return;
        }

        // Line click — select + copy to clipboard
        if (mouseY >= cy+4+TAB_H && mouseY < height) {
            float lh2 = FSS*1.5f;
            int vi  = (int)((mouseY - (cy+4+TAB_H)) / lh2);
            auto& tlines  = terminals[activeTab].lines;
            auto& tscroll = terminals[activeTab].scroll;
            int li = tscroll + vi;
            if (li >= 0 && li < (int)tlines.size()) {
                consoleSelLine = li;
                glfwSetClipboardString(glfwGetCurrentContext(), tlines[li].c_str());
            }
            return;
        }
    }
    // ── Editor click ────────────────────────────────────────────────────
    if (mouseY>=editorY() && mouseY<editorY()+editorH()) {
        int li,col; mouseToLC(li,col);
        curLine=li; curCol=col; selLine=li; selCol=col;
        dragging=true; clamp();
    }
}

void mouseDragged() {
    // Console bottom resize
    if (consoleResizing) {
        int delta = consoleResizeAnchorY - mouseY;
        CONSOLE_H = std::max(CONSOLE_H_MIN,
                    std::min(CONSOLE_H_MAX, consoleResizeAnchorH + delta));
        return;
    }
    // Console right-panel resize (drag left edge)
    if (termSideResizing) {
        int delta = termSideAnchorX - mouseX;
        TERM_SIDE_W = std::max(TERM_SIDE_W_MIN,
                      std::min(TERM_SIDE_W_MAX, termSideAnchorW + delta));
        return;
    }
    // Sidebar resize (drag right edge)
    if (sidebarResizing) {
        int delta = mouseX - sidebarResizeAnchorX;
        SIDEBAR_W = std::max(SIDEBAR_W_MIN,
                    std::min(SIDEBAR_W_MAX, sidebarResizeAnchorW + delta));
        return;
    }
    if (!dragging) return;
    if (mouseY>=editorY() && mouseY<editorY()+editorH()) {
        int li,col; mouseToLC(li,col);
        curLine=li; curCol=col; clamp(); ensureVis();
    }
}
void mouseReleased() {
    consoleResizing  = false;
    termSideResizing = false;
    sidebarResizing  = false;
    dragging = false;
    if (hasSel() && selLine==curLine && selCol==curCol) clearSel();
}
void mouseWheel(int delta) {
    if (showLibMgr)  { libScroll    += delta; return; }
    if (showSerial)  { serialScroll += delta; return; }
    if (sidebarVisible && mouseX < SIDEBAR_W) {
        ftScroll = std::max(0, ftScroll + delta);
        return;
    }
    if (mouseY>=editorY() && mouseY<editorY()+editorH()) {
        scrollTop = std::max(0, std::min(scrollTop+delta*3,
                    std::max(0,(int)code.size()-visLines())));
    } else if (mouseY>=consoleY()) {
        float lh2 = FSS*1.5f;
        int contentH = CONSOLE_H - 4 - TAB_H;
        int vis = std::max(1,(int)(contentH/lh2));
        auto& tlines  = terminals[activeTab].lines;
        auto& tscroll = terminals[activeTab].scroll;
        tscroll = std::max(0, std::min(tscroll+delta*2,
                  std::max(0,(int)tlines.size()-vis)));
    }
}
// ═══════════════════════════════════════════════════════════════════════════
// KEYBOARD
// ═══════════════════════════════════════════════════════════════════════════

// Auto-format: normalize indentation
static void autoFormat() {
    pushUndo(); int depth=0;
    for (auto& ln : code) {
        std::string t=ln;
        size_t sp=t.find_first_not_of(" \t");
        if (sp!=std::string::npos) t=t.substr(sp);
        if (!t.empty() && t[0]=='}') depth=std::max(0,depth-1);
        ln=std::string(depth*2,' ')+t;
        for (char c:t) { if(c=='{')depth++; else if(c=='}')depth--; }
        depth=std::max(0,depth);
    }
}

void keyPressed() {

    // ── Serial monitor input ─────────────────────────────────────────────
    if (showSerial) {
        if (keyCode==ESC) { showSerial=false; return; }
        if (keyCode==ENTER && serialFd>=0 && !serialInput.empty()) {
            std::string toSend=serialInput+"\n";
            write(serialFd, toSend.c_str(), toSend.size());
            serialLog.push_back("> "+serialInput);
            serialInput="";
        }
        if (keyCode==BACKSPACE && !serialInput.empty()) serialInput.pop_back();
        return;
    }

    // ── File picker input ────────────────────────────────────────────────
    if (fpShow) {
        if (keyCode==ESC) { fpShow=false; return; }
        if (keyCode==ENTER && !fpInput.empty()) {
            if (fpSave) saveFile(fpInput); else openFile(fpInput);
            fpShow=false;
        }
        if (keyCode==BACKSPACE && !fpInput.empty()) fpInput.pop_back();
        return;
    }

    // ── Global ESC ───────────────────────────────────────────────────────
    if (keyCode==ESC) { openMenu=Menu::None; showLibMgr=false; showSerial=false; return; }

    bool ctrl  = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_LEFT_CONTROL )==GLFW_PRESS
              || glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS;
    bool shift = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_LEFT_SHIFT   )==GLFW_PRESS
              || glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_RIGHT_SHIFT  )==GLFW_PRESS;

    // ── Vim INSERT mode — Esc back to NORMAL ────────────────────────────
    if (vimMode && vimInsert) {
        if (keyCode==ESC) { vimInsert=false; if(curCol>0)curCol--; clamp(); return; }
        // other keys fall through to normal editing
    }

    // ── Vim NORMAL mode ───────────────────────────────────────────────────
    if (vimMode && !vimInsert) {
        // Numeric count prefix
        static int vimN=0;
        if (key>='1'&&key<='9'&&vimN==0){vimN=key-'0';return;}
        if (key=='0'&&vimN>0){vimN*=10;return;}
        int n=(vimN>0)?vimN:1; vimN=0;

        // Motions
        if (keyCode==GLFW_KEY_H||keyCode==LEFT_KEY)  {for(int i=0;i<n;i++){if(curCol>0)curCol--;else if(curLine>0){curLine--;curCol=(int)code[curLine].size();}}clamp();return;}
        if (keyCode==GLFW_KEY_L||keyCode==RIGHT_KEY) {for(int i=0;i<n;i++){if(curCol<(int)code[curLine].size())curCol++;else if(curLine<(int)code.size()-1){curLine++;curCol=0;}}clamp();return;}
        if (keyCode==GLFW_KEY_K||keyCode==UP)         {curLine-=n;clamp();ensureVis();return;}
        if (keyCode==GLFW_KEY_J||keyCode==DOWN)       {curLine+=n;clamp();ensureVis();return;}
        if (key=='0'){curCol=0;return;}
        if (key=='$'){curCol=(int)code[curLine].size();return;}
        if (key=='^'){curCol=0;while(curCol<(int)code[curLine].size()&&(code[curLine][curCol]==' '||code[curLine][curCol]=='	'))curCol++;return;}
        if (key=='G'&&shift){curLine=(int)code.size()-1;curCol=0;ensureVis();return;}
        if (key=='w'){for(int i=0;i<n;i++){while(curCol<(int)code[curLine].size()&&code[curLine][curCol]!=' ')curCol++;while(curCol<(int)code[curLine].size()&&code[curLine][curCol]==' ')curCol++;if(curCol>=(int)code[curLine].size()&&curLine<(int)code.size()-1){curLine++;curCol=0;}}clamp();return;}
        if (key=='b'){for(int i=0;i<n;i++){if(curCol==0&&curLine>0){curLine--;curCol=(int)code[curLine].size();}if(curCol>0)curCol--;while(curCol>0&&code[curLine][curCol-1]==' ')curCol--;while(curCol>0&&code[curLine][curCol-1]!=' ')curCol--;}clamp();return;}
        if (key=='e'){for(int i=0;i<n;i++){if(curCol<(int)code[curLine].size())curCol++;while(curCol<(int)code[curLine].size()-1&&code[curLine][curCol]==' ')curCol++;while(curCol<(int)code[curLine].size()-1&&code[curLine][curCol+1]!=' ')curCol++;}clamp();return;}
        if ((keyCode==GLFW_KEY_F&&ctrl)||(keyCode==GLFW_KEY_D&&ctrl)){curLine+=visLines()/2;clamp();ensureVis();return;}
        if ((keyCode==GLFW_KEY_B&&ctrl)||(keyCode==GLFW_KEY_U&&ctrl)){curLine-=visLines()/2;clamp();ensureVis();return;}

        // Enter INSERT mode
        if (key=='i'){vimInsert=true;return;}
        if (key=='I'){curCol=0;while(curCol<(int)code[curLine].size()&&(code[curLine][curCol]==' '||code[curLine][curCol]=='	'))curCol++;vimInsert=true;return;}
        if (key=='a'){if(curCol<(int)code[curLine].size())curCol++;vimInsert=true;clamp();return;}
        if (key=='A'){curCol=(int)code[curLine].size();vimInsert=true;return;}
        if (key=='o'){  // open line below
            pushUndo();
            std::string ind="";for(char c:code[curLine]){if(c==' '||c=='	')ind+=c;else break;}
            if(!code[curLine].empty()&&code[curLine].back()=='{')ind+="  ";
            code.insert(code.begin()+curLine+1,ind);
            curLine++;curCol=(int)ind.size();vimInsert=true;clamp();ensureVis();return;
        }
        if (key=='O'){  // open line above
            pushUndo();
            std::string ind="";if(curLine>0)for(char c:code[curLine]){if(c==' '||c=='	')ind+=c;else break;}
            code.insert(code.begin()+curLine,ind);
            curCol=(int)ind.size();vimInsert=true;clamp();ensureVis();return;
        }
        if (key=='s'){pushUndo();if(curCol<(int)code[curLine].size())code[curLine].erase(curCol,1);vimInsert=true;return;}
        if (key=='S'){pushUndo();std::string ind="";for(char c:code[curLine]){if(c==' '||c=='	')ind+=c;else break;}code[curLine]=ind;curCol=(int)ind.size();vimInsert=true;return;}
        if (key=='C'){pushUndo();code[curLine]=code[curLine].substr(0,curCol);vimInsert=true;return;}

        // Edit
        if (key=='x'){pushUndo();for(int i=0;i<n;i++)if(curCol<(int)code[curLine].size())code[curLine].erase(curCol,1);clamp();return;}
        if (key=='X'){pushUndo();if(curCol>0){code[curLine].erase(curCol-1,1);curCol--;}return;}
        if (key=='r'){vimCmd="r";return;}
        if (key=='J'){pushUndo();if(curLine<(int)code.size()-1){std::string t=code[curLine+1];size_t s=t.find_first_not_of(" \t");code[curLine]+=" "+(s!=std::string::npos?t.substr(s):t);code.erase(code.begin()+curLine+1);}return;}
        if (key=='~'){pushUndo();if(curCol<(int)code[curLine].size()){char&c=code[curLine][curCol];c=isupper(c)?tolower(c):toupper(c);if(curCol<(int)code[curLine].size()-1)curCol++;}return;}
        if (key=='D'){pushUndo();code[curLine]=code[curLine].substr(0,curCol);return;}
        if (key=='>'&&shift){pushUndo();code[curLine]="  "+code[curLine];return;}
        if (key=='<'){pushUndo();if(code[curLine].size()>=2&&code[curLine][0]==' '&&code[curLine][1]==' ')code[curLine].erase(0,2);return;}

        // dd / cc / yy (double-key commands via static flag)
        if (key=='d'){static bool ld=false;if(ld){pushUndo();glfwSetClipboardString(glfwGetCurrentContext(),code[curLine].c_str());for(int i=0;i<n;i++){code.erase(code.begin()+std::min(curLine,(int)code.size()-1));if(code.empty())code.push_back("");}clamp();ld=false;}else ld=true;return;}
        if (key=='c'){static bool lc=false;if(lc){pushUndo();std::string ind="";for(char c:code[curLine]){if(c==' '||c=='\t')ind+=c;else break;}code[curLine]=ind;curCol=(int)ind.size();vimInsert=true;lc=false;}else lc=true;return;}
        if (key=='y'){static bool ly=false;if(ly){glfwSetClipboardString(glfwGetCurrentContext(),code[curLine].c_str());ly=false;}else ly=true;return;}
        if (key=='Y'){glfwSetClipboardString(glfwGetCurrentContext(),code[curLine].c_str());return;}
        if (key=='p'){const char*cb=glfwGetClipboardString(glfwGetCurrentContext());if(cb){pushUndo();code.insert(code.begin()+curLine+1,std::string(cb));curLine++;clamp();ensureVis();}return;}
        if (key=='P'){const char*cb=glfwGetClipboardString(glfwGetCurrentContext());if(cb){pushUndo();code.insert(code.begin()+curLine,std::string(cb));ensureVis();}return;}

        // Undo/Redo
        if (key=='u'){if(!undoStack.empty()){auto&u=undoStack.back();redoStack.push_back({code,{curLine,curCol}});code=u.first;curLine=u.second.first;curCol=u.second.second;undoStack.pop_back();clearSel();clamp();ensureVis();}return;}
        if (keyCode==GLFW_KEY_R&&ctrl){if(!redoStack.empty()){auto&u=redoStack.back();undoStack.push_back({code,{curLine,curCol}});code=u.first;curLine=u.second.first;curCol=u.second.second;redoStack.pop_back();clearSel();clamp();ensureVis();}return;}

        // Visual select
        if (key=='v'){selLine=curLine;selCol=curCol;return;}
        if (key=='V'){selLine=curLine;selCol=0;curCol=(int)code[curLine].size();return;}

        return; // swallow in NORMAL
    }


    // ── Ctrl shortcuts ───────────────────────────────────────────────────
    if (ctrl) {
        if (keyCode==GLFW_KEY_N) { newFile(); return; }
        if (keyCode==GLFW_KEY_O) { fpShow=true; fpSave=false; fpInput=""; return; }
        if (keyCode==GLFW_KEY_S) {
            if (shift) { fpShow=true; fpSave=true; fpInput=currentFile; }
            else if (currentFile.empty()) { fpShow=true; fpSave=true; fpInput=""; }
            else saveFile(currentFile);
            return;
        }
        if (keyCode==GLFW_KEY_B) { doCompile(); return; }
        if (keyCode==GLFW_KEY_R) { doRun();     return; }
        if (keyCode==GLFW_KEY_M && shift) { showSerial=true; return; }
        if (keyCode==GLFW_KEY_L && shift) { showLibMgr=true; checkInstalled(); return; }
        if (keyCode==GLFW_KEY_V && shift) { vimMode=!vimMode; vimInsert=false; return; }
        if (keyCode==GLFW_KEY_Z && !shift) {
            if (!undoStack.empty()) { auto&u=undoStack.back();redoStack.push_back({code,{curLine,curCol}});code=u.first;curLine=u.second.first;curCol=u.second.second;undoStack.pop_back();clearSel();clamp();ensureVis(); }
            return;
        }
        if (keyCode==GLFW_KEY_Y || (keyCode==GLFW_KEY_Z&&shift)) {
            if (!redoStack.empty()) { auto&u=redoStack.back();undoStack.push_back({code,{curLine,curCol}});code=u.first;curLine=u.second.first;curCol=u.second.second;redoStack.pop_back();clearSel();clamp();ensureVis(); }
            return;
        }
        if (keyCode==GLFW_KEY_A)     { selLine=0;selCol=0;curLine=(int)code.size()-1;curCol=(int)code.back().size(); return; }
        if (keyCode==GLFW_KEY_C)     { std::string s=getSelected();if(!s.empty())glfwSetClipboardString(glfwGetCurrentContext(),s.c_str()); return; }
        if (keyCode==GLFW_KEY_X)     { pushUndo();std::string s=getSelected();if(!s.empty())glfwSetClipboardString(glfwGetCurrentContext(),s.c_str());deleteSel(); return; }
        if (keyCode==GLFW_KEY_V) {
            const char* cb=glfwGetClipboardString(glfwGetCurrentContext()); if(!cb)return;
            pushUndo(); if(hasSel())deleteSel();
            std::istringstream ss{std::string(cb)}; std::string ln; bool first=true;
            while(std::getline(ss,ln)) {
                if (!first) { code.insert(code.begin()+curLine+1,code[curLine].substr(curCol));code[curLine]=code[curLine].substr(0,curCol);curLine++;curCol=0; }
                code[curLine].insert(curCol,ln); curCol+=(int)ln.size(); first=false;
            }
            clamp();ensureVis(); return;
        }
        if (keyCode==GLFW_KEY_D)     { pushUndo();code.insert(code.begin()+curLine+1,code[curLine]);curLine++;clamp();ensureVis(); return; }
        if (keyCode==GLFW_KEY_SLASH) { pushUndo();auto&ln=code[curLine];if(ln.size()>=2&&ln[0]=='/'&&ln[1]=='/')ln.erase(0,2);else ln="//"+ln; return; }
        if (keyCode==GLFW_KEY_F&&shift) { autoFormat(); return; }
        if (keyCode==GLFW_KEY_EQUAL) {
            FS =std::min(32.0f, FS +1); FSS=FS-1; FST=FS-2; return;
        }
        if (keyCode==GLFW_KEY_MINUS) {
            FS =std::max(8.0f,  FS -1); FSS=FS-1; FST=FS-2; return;
        }
        if (keyCode==GLFW_KEY_HOME)  { curLine=0;curCol=0;scrollTop=0;clearSel(); return; }
        if (keyCode==GLFW_KEY_END)   { curLine=(int)code.size()-1;curCol=(int)code.back().size();clearSel();ensureVis(); return; }
    }

    // ── Arrow / navigation (with optional shift-select) ──────────────────
    auto anchor = [&]() {
        if (shift && !hasSel()) { selLine=curLine; selCol=curCol; }
        else if (!shift) clearSel();
    };

    if      (keyCode==UP)               { anchor(); curLine--; }
    else if (keyCode==DOWN)             { anchor(); curLine++; }
    else if (keyCode==LEFT_KEY)         { anchor(); if(curCol>0)curCol--;else if(curLine>0){curLine--;curCol=(int)code[curLine].size();} }
    else if (keyCode==RIGHT_KEY)        { anchor(); if(curCol<(int)code[curLine].size())curCol++;else if(curLine<(int)code.size()-1){curLine++;curCol=0;} }
    else if (keyCode==GLFW_KEY_HOME)    { anchor(); curCol=0; }
    else if (keyCode==GLFW_KEY_END)     { anchor(); curCol=(int)code[curLine].size(); }
    else if (keyCode==GLFW_KEY_PAGE_UP) { anchor(); curLine-=visLines(); }
    else if (keyCode==GLFW_KEY_PAGE_DOWN){anchor(); curLine+=visLines(); }

    // ── Editing ──────────────────────────────────────────────────────────
    else if (keyCode==ENTER) {
        pushUndo(); if(hasSel())deleteSel();
        std::string& cur=code[curLine];
        std::string before=cur.substr(0,curCol), after=cur.substr(curCol);
        std::string ind=""; for(char c:cur){if(c==' '||c=='\t')ind+=c;else break;}
        if (!before.empty()&&before.back()=='{') ind+="  ";
        cur=before; code.insert(code.begin()+curLine+1, ind+after);
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

// keyTyped: correct shifted/capsed character from GLFW char callback
void keyTyped() {
    if (showSerial) { if(key>=32&&key<127) serialInput+=key; return; }
    if (fpShow)     { if(key>=32&&key<127) fpInput+=key;      return; }
    if (vimMode && !vimInsert) {
        // Handle pending 'r' replace command
        if (vimCmd=="r" && key>=32 && key<127) {
            pushUndo();
            if(curCol<(int)code[curLine].size()) code[curLine][curCol]=key;
            vimCmd="";
        }
        return;
    }
    if (key>=32 && key<127) {
        pushUndo(); if(hasSel())deleteSel();
        code[curLine].insert(curCol, 1, key);
        curCol++; clamp(); ensureVis();
    }
}

void mouseMoved() {}
void keyReleased() {}
void windowMoved() {}
void mouseClicked() {}
void windowResized() {}

} // namespace Processing

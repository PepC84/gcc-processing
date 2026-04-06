#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// Platform.h — cross-platform shim for Processing CPP IDE
// Supports: Linux, Windows (MinGW-w64 / MSVC)
// ═══════════════════════════════════════════════════════════════════════════

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <algorithm>
#include <cstring>

// ── Detect platform ────────────────────────────────────────────────────────
#ifdef _WIN32
  #define PLAT_WINDOWS 1
#else
  #define PLAT_LINUX   1
#endif

// ═══════════════════════════════════════════════════════════════════════════
// WINDOWS
// ═══════════════════════════════════════════════════════════════════════════
#ifdef PLAT_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
// ── Stubs for Linux headers that may still be included by user code ─────────
// These are empty on Windows since Platform.h provides the real implementations.
// This allows code written for Linux to compile without changes.
// ── Fake termios.h so old code that does #include <termios.h> still compiles ─
// We set the same header guard that a real termios.h would set, so the
// #include becomes a no-op after Platform.h has already defined everything.
#ifndef _TERMIOS_H
#define _TERMIOS_H
   typedef unsigned int speed_t;
   typedef unsigned int tcflag_t;
   typedef unsigned char cc_t;
   static const speed_t B300=300, B600=600, B1200=1200, B2400=2400, B4800=4800;
   static const speed_t B9600=9600, B14400=14400, B19200=19200, B38400=38400;
   static const speed_t B57600=57600, B115200=115200, B230400=230400;
   static const int TCSANOW=0, TCSADRAIN=1, TCSAFLUSH=2;
   static const tcflag_t IGNBRK=1, IXON=2, IXOFF=4, IXANY=8;
   static const tcflag_t CLOCAL=0x800, CREAD=0x200, CS8=0x30, CSIZE=0x30;
   static const tcflag_t PARENB=0x100, PARODD=0x200, CSTOPB=0x40, CRTSCTS=0x80000000;
   static const int VMIN=6, VTIME=5;
   struct termios {
       tcflag_t c_iflag=0, c_oflag=0, c_cflag=0, c_lflag=0;
       cc_t c_cc[20]={};
   };
   inline int tcgetattr(int,termios*){return 0;}
   inline int tcsetattr(int,int,const termios*){return 0;}
   inline int cfsetispeed(termios*,speed_t){return 0;}
   inline int cfsetospeed(termios*,speed_t){return 0;}
#endif  // _TERMIOS_H

// ── Fake glob.h ────────────────────────────────────────────────────────────
#ifndef _GLOB_H
#define _GLOB_H
   struct glob_t { size_t gl_pathc=0; char** gl_pathv=nullptr; int gl_offs=0; };
   inline int  glob(const char*,int,int(*)(const char*,int),glob_t* g){g->gl_pathc=0;return 1;}
   inline void globfree(glob_t*){}
#endif  // _GLOB_H

// dirent.h — provided natively by MinGW/MSYS2
#include <dirent.h>
#include <sys/stat.h>

#include <windows.h>
#include <commdlg.h>   // GetOpenFileName / GetSaveFileName
#include <shellapi.h>  // ShellExecute
#include <shlobj.h>    // SHGetFolderPath
#include <sys/stat.h>

// ── Undefine Windows macros that clash with Processing constants ──────────
// windows.h pollutes the global namespace with short macro names.
// We undef them all so Processing.h can redeclare them as constexpr ints.
#undef DIFFERENCE
#undef BLEND
#undef ADD
#undef SUBTRACT
#undef MULTIPLY
#undef SCREEN
#undef DARKEST
#undef LIGHTEST
#undef EXCLUSION
#undef LEFT
#undef RIGHT
#undef CENTER
#undef ROUND
#undef POINTS
#undef LINES
#undef TRIANGLES
#undef QUADS
#undef CLOSE
#undef BASELINE
#undef IMAGE
#undef NORMAL
#undef CROSS
#undef HAND
#undef MOVE
#undef WAIT
#undef SQUARE
#undef PROJECT
#undef MITER
#undef BEVEL
#undef RGB
#undef HSB
#undef REPLACE
#undef NONE
#undef TRANSPARENT
#undef ERROR
#undef DELETE
#undef NEAR
#undef FAR
#undef OPEN
#undef CHORD
#undef PIE
#undef TEXT
#undef BACKSPACE
#undef TAB
#undef ENTER
#undef RETURN
#undef ESC
#undef CODED
// ─────────────────────────────────────────────────────────────────────────

// ── Sleep ──────────────────────────────────────────────────────────────────
inline void plat_sleep_ms(int ms) { Sleep(ms); }

// ── Open folder in explorer ────────────────────────────────────────────────
inline void plat_open_folder(const std::string& path) {
    ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOW);
}

// ── File dialog ───────────────────────────────────────────────────────────
inline std::string plat_folder_dialog(const std::string& /*current*/ = ".") {
    BROWSEINFOA bi = {};
    bi.lpszTitle = "Select Project Folder";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return "";
    char buf[MAX_PATH] = {};
    SHGetPathFromIDListA(pidl, buf);
    CoTaskMemFree(pidl);
    return buf;
}

inline std::string plat_file_dialog(bool save, const std::string& def = "") {
    char buf[MAX_PATH] = {};
    if (!def.empty()) strncpy(buf, def.c_str(), MAX_PATH - 1);
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "C++ Sketches\0*.cpp\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = save ? "Save Sketch" : "Open Sketch";
    ofn.Flags       = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : 0);
    if (save) { if (!GetSaveFileNameA(&ofn)) return ""; }
    else      { if (!GetOpenFileNameA(&ofn)) return ""; }
    return buf;
}

// ── List .cpp files in current directory ──────────────────────────────────
inline std::vector<std::string> plat_list_sketches() {
    std::vector<std::string> files;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(".\\*.cpp", &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do { files.push_back(fd.cFileName); } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end());
    return files;
}

// ── Serial ports ────────────────────────────────────────────────────────────
inline std::vector<std::string> plat_list_ports() {
    std::vector<std::string> ports;
    for (int i = 1; i <= 256; i++) {
        std::string name = "\\\\.\\COM" + std::to_string(i);
        HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            ports.push_back("COM" + std::to_string(i));
            CloseHandle(h);
        }
    }
    return ports;
}

// Serial handle type
using plat_serial_t = HANDLE;
inline plat_serial_t plat_serial_invalid() { return INVALID_HANDLE_VALUE; }
inline bool plat_serial_ok(plat_serial_t h) { return h != INVALID_HANDLE_VALUE; }

inline plat_serial_t plat_serial_open(const std::string& port, int baud) {
    std::string name = "\\\\.\\" + port;
    HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    SetCommState(h, &dcb);
    COMMTIMEOUTS ct = { 1, 0, 0, 0, 0 };  // non-blocking read
    SetCommTimeouts(h, &ct);
    return h;
}
inline void plat_serial_close(plat_serial_t h) { CloseHandle(h); }
inline int  plat_serial_read(plat_serial_t h, char* buf, int n) {
    DWORD got = 0;
    ReadFile(h, buf, n, &got, NULL);
    return (int)got;
}
inline int  plat_serial_write(plat_serial_t h, const char* buf, int n) {
    DWORD written = 0;
    WriteFile(h, buf, n, &written, NULL);
    return (int)written;
}

// ── Process (run sketch, capture output) ──────────────────────────────────
struct plat_proc_t {
    HANDLE hProcess = NULL;
    HANDLE hPipeRead = NULL;
};

inline plat_proc_t plat_proc_invalid() { return {}; }
inline bool        plat_proc_ok(const plat_proc_t& p) { return p.hProcess != NULL; }

inline plat_proc_t plat_proc_start(const std::string& exePath) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return {};
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb        = sizeof(si);
    si.dwFlags   = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    PROCESS_INFORMATION pi = {};

    std::string cmd = exePath;
    if (!CreateProcessA(NULL, cmd.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite);
        return {};
    }
    CloseHandle(hWrite);   // parent doesn't write
    CloseHandle(pi.hThread);
    plat_proc_t proc;
    proc.hProcess  = pi.hProcess;
    proc.hPipeRead = hRead;
    return proc;
}
inline int plat_proc_read(plat_proc_t& p, char* buf, int n) {
    DWORD got = 0;
    if (!ReadFile(p.hPipeRead, buf, n, &got, NULL)) return 0;
    return (int)got;
}
inline bool plat_proc_running(plat_proc_t& p) {
    return WaitForSingleObject(p.hProcess, 0) == WAIT_TIMEOUT;
}
inline void plat_proc_stop(plat_proc_t& p) {
    if (p.hProcess) { TerminateProcess(p.hProcess, 0); CloseHandle(p.hProcess); p.hProcess=NULL; }
    if (p.hPipeRead){ CloseHandle(p.hPipeRead); p.hPipeRead=NULL; }
}

// ── File stat ────────────────────────────────────────────────────────────
inline bool plat_file_exists(const std::string& path) {
    struct _stat st;
    return _stat(path.c_str(), &st) == 0;
}

// ── Install command (Windows has no package manager — show instructions) ──
inline std::string plat_build_install_cmd(const std::string& /*pkg*/, const std::string& msysName) {
    // In MSYS2: pacman -S mingw-w64-x86_64-<name>
    return "pacman -S --noconfirm mingw-w64-x86_64-" + msysName;
}

// ── Binary extension ──────────────────────────────────────────────────────
inline std::string plat_exe_ext() { return ".exe"; }

#endif // PLAT_WINDOWS

// ═══════════════════════════════════════════════════════════════════════════
// LINUX
// ═══════════════════════════════════════════════════════════════════════════
#ifdef PLAT_LINUX
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <dirent.h>
#include <glob.h>
#include <termios.h>
#include <errno.h>

inline void plat_sleep_ms(int ms) { usleep(ms * 1000); }

inline void plat_open_folder(const std::string& path) {
    system(("xdg-open " + path + " >/dev/null 2>&1 &").c_str());
}

inline std::string plat_folder_dialog(const std::string& current = ".") {
    std::string cmd = "zenity --file-selection --directory"
                      " --title=\"Open Folder\""
                      " --filename=\"" + current + "\" 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char buf[1024] = {};
    if (fgets(buf, sizeof(buf), p)) {}
    pclose(p);
    std::string r = buf;
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r;
}

inline std::string plat_file_dialog(bool save, const std::string& def = "") {
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
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r;
}

inline std::vector<std::string> plat_list_sketches() {
    std::vector<std::string> files;
    DIR* d = opendir("."); if (!d) return files;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.substr(n.size() - 4) == ".cpp") files.push_back(n);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

inline std::vector<std::string> plat_list_ports() {
    std::vector<std::string> ports;
    glob_t g;
    auto add = [&](const char* pat) {
        if (glob(pat, 0, nullptr, &g) == 0)
            for (size_t i = 0; i < g.gl_pathc; i++) ports.push_back(g.gl_pathv[i]);
        globfree(&g);
    };
    add("/dev/ttyUSB*"); add("/dev/ttyACM*"); add("/dev/ttyS*");
    return ports;
}

using plat_serial_t = int;
inline plat_serial_t plat_serial_invalid() { return -1; }
inline bool          plat_serial_ok(plat_serial_t fd) { return fd >= 0; }

inline plat_serial_t plat_serial_open(const std::string& port, int baud) {
    int fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return fd;
    struct termios tty = {};
    tcgetattr(fd, &tty);
    speed_t sp = B9600;
    switch (baud) {
        case 300: sp=B300; break; case 1200: sp=B1200; break;
        case 2400: sp=B2400; break; case 4800: sp=B4800; break;
        case 9600: sp=B9600; break; case 19200: sp=B19200; break;
        case 38400: sp=B38400; break; case 57600: sp=B57600; break;
        case 115200: sp=B115200; break;
    }
    cfsetispeed(&tty, sp); cfsetospeed(&tty, sp);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~(IGNBRK | IXON | IXOFF | IXANY);
    tty.c_lflag = 0; tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0; tty.c_cc[VTIME] = 0;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}
inline void plat_serial_close(plat_serial_t fd) { close(fd); }
inline int  plat_serial_read(plat_serial_t fd, char* buf, int n) { return read(fd, buf, n); }
inline int  plat_serial_write(plat_serial_t fd, const char* buf, int n) { return write(fd, buf, n); }

struct plat_proc_t {
    pid_t pid  = -1;
    int   pipe = -1;
};
inline plat_proc_t plat_proc_invalid() { return {}; }
inline bool        plat_proc_ok(const plat_proc_t& p) { return p.pid > 0; }

inline plat_proc_t plat_proc_start(const std::string& exePath) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return {};
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return {}; }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        setvbuf(stdout, nullptr, _IONBF, 0);
        execl(exePath.c_str(), exePath.c_str(), nullptr);
        write(STDOUT_FILENO, "exec failed\n", 12);
        _exit(1);
    }
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    return { pid, pipefd[0] };
}
inline int plat_proc_read(plat_proc_t& p, char* buf, int n) {
    return read(p.pipe, buf, n);
}
inline bool plat_proc_running(plat_proc_t& p) {
    if (p.pid <= 0) return false;
    int s; return waitpid(p.pid, &s, WNOHANG) == 0;
}
inline void plat_proc_stop(plat_proc_t& p) {
    if (p.pid > 0) { kill(p.pid, SIGTERM); waitpid(p.pid, nullptr, WNOHANG); p.pid=-1; }
    if (p.pipe >= 0) { close(p.pipe); p.pipe=-1; }
}

inline bool plat_file_exists(const std::string& path) {
    struct stat st; return stat(path.c_str(), &st) == 0;
}

inline std::string plat_build_install_cmd(const std::string& pkg, const std::string& /*msys*/) {
    // Detect pkg manager at runtime (same as before)
    auto hasBin = [](const std::string& b) {
        return system(("command -v " + b + " >/dev/null 2>&1").c_str()) == 0;
    };
    std::string priv = (system("command -v pkexec >/dev/null 2>&1") == 0) ? "pkexec" : "sudo";
    // Find terminal for password prompt
    for (auto& t : {"kitty","alacritty","foot","wezterm","xterm"}) {
        if (system(("command -v " + std::string(t) + " >/dev/null 2>&1").c_str()) == 0) {
            std::string inner;
            if      (hasBin("pacman"))  inner = priv + " pacman -S --noconfirm " + pkg;
            else if (hasBin("apt-get")) inner = priv + " apt-get install -y " + pkg;
            else if (hasBin("dnf"))     inner = priv + " dnf install -y " + pkg;
            else                        inner = "echo 'Install manually: " + pkg + "'";
            std::string pause = "; echo ''; echo 'Done — press Enter'; read";
            if (std::string(t) == "gnome-terminal")
                return std::string(t) + " -- sh -c '" + inner + pause + "'";
            return std::string(t) + " -e sh -c '" + inner + pause + "'";
        }
    }
    // Fallback: blocking
    if      (hasBin("pacman"))  return priv + " pacman -S --noconfirm " + pkg;
    else if (hasBin("apt-get")) return priv + " apt-get install -y " + pkg;
    return "echo 'Install: " + pkg + "'";
}

inline std::string plat_exe_ext() { return ""; }

#endif // PLAT_LINUX

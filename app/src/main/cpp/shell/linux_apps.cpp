#include "shell/linux_apps.h"

#include <dirent.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace pxr {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Removes the %f %u ... field codes of the Desktop Entry spec.
std::string stripFieldCodes(const std::string& exec) {
    std::string out;
    for (size_t i = 0; i < exec.size(); ++i) {
        if (exec[i] == '%' && i + 1 < exec.size()) {
            if (exec[i + 1] == '%') out.push_back('%');
            ++i;
            continue;
        }
        out.push_back(exec[i]);
    }
    std::string squeezed;
    for (char c : out) {
        if (c == ' ' && !squeezed.empty() && squeezed.back() == ' ') continue;
        squeezed.push_back(c);
    }
    return trim(squeezed);
}

bool hidden(const std::string& id, const std::string& exec) {
    // Compositors, servers and X11-only programs that cannot show up as a window here.
    static const char* ids[] = {"weston", "weston-terminal", "foot-server", "footclient", "xterm", "uxterm",
                                "xdg-desktop-portal-gtk"};
    for (const char* h : ids) {
        if (id == h) return true;
    }
    return exec.rfind("weston ", 0) == 0 || exec == "weston" || exec.rfind("Xwayland", 0) == 0;
}

bool parse(const std::string& path, const std::string& id, LinuxApp* out) {
    std::ifstream f(path);
    if (!f) return false;
    bool inEntry = false, noDisplay = false, isHidden = false, terminal = false, application = true;
    std::string name, exec, icon;
    for (std::string line; std::getline(f, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            if (inEntry) break;  // the next group (actions) is not interesting
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        if (!inEntry) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "Name") name = value;
        else if (key == "Exec") exec = value;
        else if (key == "Icon") icon = value;
        else if (key == "NoDisplay") noDisplay = value == "true";
        else if (key == "Hidden") isHidden = value == "true";
        else if (key == "Terminal") terminal = value == "true";
        else if (key == "Type") application = value == "Application";
    }
    if (!application || noDisplay || isHidden || name.empty() || exec.empty()) return false;
    std::string command = stripFieldCodes(exec);
    if (command.empty() || hidden(id, command)) return false;
    if (terminal) command = "foot -e " + command;
    out->id = id;
    out->name = name;
    out->command = command;
    out->icon = icon;
    return true;
}

}  // namespace

std::vector<LinuxApp> scanLinuxApps(const std::string& linuxRoot) {
    std::vector<LinuxApp> apps;
    const char* dirs[] = {"/rootfs/usr/share/applications", "/rootfs/usr/local/share/applications"};
    for (const char* sub : dirs) {
        const std::string dir = linuxRoot + sub;
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        while (dirent* e = readdir(d)) {
            const std::string file = e->d_name;
            const std::string ext = ".desktop";
            if (file.size() <= ext.size() || file.compare(file.size() - ext.size(), ext.size(), ext) != 0) continue;
            const std::string id = file.substr(0, file.size() - ext.size());
            if (std::any_of(apps.begin(), apps.end(), [&](const LinuxApp& a) { return a.id == id; })) continue;
            LinuxApp app;
            if (parse(dir + "/" + file, id, &app)) apps.push_back(std::move(app));
        }
        closedir(d);
    }
    std::sort(apps.begin(), apps.end(), [](const LinuxApp& a, const LinuxApp& b) { return a.name < b.name; });
    return apps;
}

}  // namespace pxr

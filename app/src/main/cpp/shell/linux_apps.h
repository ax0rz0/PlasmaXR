// Finds the Linux programs installed in the rootfs by reading their .desktop files.
#pragma once

#include <string>
#include <vector>

namespace pxr {

struct LinuxApp {
    std::string id;       // desktop file name without the extension
    std::string name;
    std::string command;  // shell command line, field codes removed
    std::string icon;     // icon name from the desktop file (may be empty)
};

// `linuxRoot` is the directory that contains rootfs/. Entries are sorted by name.
std::vector<LinuxApp> scanLinuxApps(const std::string& linuxRoot);

}  // namespace pxr

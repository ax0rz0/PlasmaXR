// Installed-app catalogue for the shell: launchable apps, pinned favourites, apps launched this
// session, and GPU icon textures decoded on a background thread.
#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gfx/gl_util.h"
#include "platform/jni_bridge.h"

namespace pxr {

class AppModel {
public:
    ~AppModel() { shutdown(); }

    void init();
    void shutdown();
    void refresh();

    const std::vector<AppInfo>& apps() const { return apps_; }
    const AppInfo* find(const std::string& pkg) const;

    bool isPinned(const std::string& pkg) const;
    void togglePin(const std::string& pkg);
    std::vector<const AppInfo*> pinnedApps() const;

    void launch(const AppInfo& app);
    // Records an app the shell started itself (Linux apps) so its tile shows as running.
    void noteLaunched(const std::string& pkg);
    bool isRecent(const std::string& pkg) const;

    // Returns the icon if decoded; otherwise queues a load and returns nullptr.
    const Texture* icon(const AppInfo& app);
    // Uploads decoded icons (GL thread). Returns true if anything changed.
    bool pumpUploads(int maxPerFrame);

    // Increments whenever pins, recents or icons change, so panels know to redraw.
    uint32_t revision() const { return revision_; }

private:
    struct Decoded {
        std::string pkg;
        PixelBuffer pixels;
    };

    void workerMain();
    void loadPinned();
    void savePinned();

    std::vector<AppInfo> apps_;
    std::vector<std::string> pinned_;
    std::vector<std::string> recents_;
    std::unordered_map<std::string, Texture> icons_;
    std::unordered_set<std::string> requested_;
    uint32_t revision_ = 1;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<AppInfo> requests_;
    std::deque<Decoded> results_;
    bool stop_ = false;
    bool started_ = false;
};

}  // namespace pxr

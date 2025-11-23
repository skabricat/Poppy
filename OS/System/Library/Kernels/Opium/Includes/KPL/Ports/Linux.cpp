#include <iostream>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <random>
#include <algorithm>
#include <deque>

#include <libudev.h>

using namespace std;

namespace KPL {

    // -----------------------
    //   DEVICE STRUCTURE
    // -----------------------
    struct Device {
        string ID;
        string name;
        string subsystem;

        string vendorID;
        string productID;
        string driver;
        string deviceNode;

        map<string, string> properties;
    };

    // -----------------------
    //   GLOBAL STORAGE
    // -----------------------
    static vector<Device> g_devices;
    static mutex g_devMutex;

    // observers receive "device added" / "device removed" / "changed"
    using DeviceEventCallback =
        function<void(const string& action, const Device&)>;

    static vector<DeviceEventCallback> g_callbacks;
    static mutex g_cbMutex;

    // worker thread control
    static atomic<bool> g_monitorRunning = false;
    static thread g_monitorThread;

    // -----------------------
    //  DEVICE ENUMERATION
    // -----------------------

    Device makeDeviceFromUdev(udev_device* dev) {
        Device d;

        const char* path = udev_device_get_syspath(dev);
        if(path)
            d.ID = path;

        d.name = filesystem::path(d.ID).filename().string();

        if(auto v = udev_device_get_subsystem(dev)) d.subsystem = v;
        if(auto v = udev_device_get_devnode(dev))   d.deviceNode = v;
        if(auto v = udev_device_get_driver(dev))    d.driver = v;

        // sysattrs
        udev_list_entry* attrs = udev_device_get_sysattr_list_entry(dev);
        udev_list_entry* a;

        udev_list_entry_foreach(a, attrs) {
            const char* name = udev_list_entry_get_name(a);
            const char* val  = udev_device_get_sysattr_value(dev, name);
            if(name && val)
                d.properties[name] = val;
        }

        // vendor/product IDs from udev properties
        if(auto v = udev_device_get_property_value(dev, "ID_VENDOR_ID"))
            d.vendorID = v;
        else if(d.properties.contains("vendor"))
            d.vendorID = d.properties["vendor"];
        else if(d.properties.contains("idVendor"))
            d.vendorID = d.properties["idVendor"];

        if(auto v = udev_device_get_property_value(dev, "ID_MODEL_ID"))
            d.productID = v;
        else if(d.properties.contains("device"))
            d.productID = d.properties["device"];
        else if(d.properties.contains("idProduct"))
            d.productID = d.properties["idProduct"];

        return d;
    }


    vector<Device> getDevices() {
        vector<Device> result;
        udev* udev = udev_new();
        if(!udev)
            return result;

        udev_enumerate* enumerate = udev_enumerate_new(udev);
        udev_enumerate_add_match_subsystem(enumerate, NULL);
        udev_enumerate_scan_devices(enumerate);

        udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry* entry;

        udev_list_entry_foreach(entry, devices) {
            const char* path = udev_list_entry_get_name(entry);
            udev_device* dev = udev_device_new_from_syspath(udev, path);

            if(!dev) continue;

            result.push_back(makeDeviceFromUdev(dev));
            udev_device_unref(dev);
        }

        udev_enumerate_unref(enumerate);
        udev_unref(udev);
        return result;
    }

    // -----------------------
    //  CALLBACK REGISTRATION
    // -----------------------
    void addDeviceObserver(DeviceEventCallback cb) {
        lock_guard lock(g_cbMutex);
        g_callbacks.push_back(cb);
    }

    static void notifyCallbacks(const string& action, const Device& d) {
        lock_guard lock(g_cbMutex);
        for(auto& cb : g_callbacks)
            cb(action, d);
    }


    // -----------------------
    //  DEVICE MONITOR THREAD
    // -----------------------
    void monitorThreadFunc() {
        udev* udev = udev_new();
        if(!udev) return;

        udev_monitor* mon = udev_monitor_new_from_netlink(udev, "kernel");
        if(!mon) {
            udev_unref(udev);
            return;
        }

        udev_monitor_filter_add_match_subsystem_devtype(mon, NULL, NULL);
        udev_monitor_enable_receiving(mon);

        int fd = udev_monitor_get_fd(mon);
        g_monitorRunning = true;

        while(g_monitorRunning) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            timeval tv = {1, 0}; // 1-second timeout
            int ret = select(fd+1, &fds, NULL, NULL, &tv);

            if(ret <= 0) {
                continue;
            }

            if(FD_ISSET(fd, &fds)) {
                udev_device* dev = udev_monitor_receive_device(mon);
                if(dev) {
                    const char* action = udev_device_get_action(dev);
                    string act = action ? action : "change";
                    Device d = makeDeviceFromUdev(dev);

                    {
                        lock_guard lock(g_devMutex);

                        if(act == "add") {
                            g_devices.push_back(d);
                        }
                        else if(act == "remove") {
                            g_devices.erase(remove_if(g_devices.begin(),
                                                      g_devices.end(),
                                [&](auto& x){ return x.ID == d.ID; }),
                                g_devices.end());
                        }
                        else { // change
                            for(auto& x : g_devices)
                                if(x.ID == d.ID)
                                    x = d;
                        }
                    }

                    notifyCallbacks(act, d);
                    udev_device_unref(dev);
                }
            }
        }

        udev_monitor_unref(mon);
        udev_unref(udev);
    }


    // -----------------------
    //  PUBLIC CONTROL API
    // -----------------------
    void startDeviceObservation() {
        if(g_monitorRunning) return;

        {
            lock_guard lock(g_devMutex);
            g_devices = getDevices();
        }

        // notify initial load
        for (auto& d : g_devices) {
            notifyCallbacks("initial", d);
        }

        g_monitorThread = thread(monitorThreadFunc);
    }

    void stopDeviceObservation() {
        g_monitorRunning = false;
        if(g_monitorThread.joinable())
            g_monitorThread.join();
    }

    vector<Device> getCurrentDeviceSnapshot() {
        lock_guard lock(g_devMutex);
        return g_devices;
    }

} // namespace KPL

// ==============================================
// UI helpers
// ==============================================

void clearScreen() {
    cout << "\033[H\033[2J\033[3J";
}

// Форматируем короткое имя устройства
string formatDevice(const KPL::Device& d) {
    string s = d.name;
    if(!d.subsystem.empty()) s += " S: " + d.subsystem;
    if(!d.vendorID.empty())  s += " V: " + d.vendorID;
    if(!d.productID.empty()) s += " P: " + d.productID;
    return s;
}

// Печать экрана
void printScreen(const deque<string>& logLines) {
    clearScreen();

    cout << "==== Device Event Log (" << logLines.size() << " entries) ====\n\n";
    for(const auto& line : logLines) {
        cout << line << "\n";
    }
    cout << flush;
}


// ==============================================
// MAIN
// ==============================================

int main() {
    constexpr int MAX_LINES = 48;
    deque<string> logLines;

    // Добавляем наблюдателя на устройства
    KPL::addDeviceObserver([&](const string& action, const KPL::Device& d){
        // Формируем строчку
        string line = action + "  " + formatDevice(d);

        // Добавляем в лог
        logLines.push_back(line);

        // Ограничиваем размер
        if(logLines.size() > MAX_LINES)
            logLines.pop_front();

        // Перерисовываем
        printScreen(logLines);
    });

    // Запускаем монитор
    KPL::startDeviceObservation();

    // Главный цикл
    while(true) {
        this_thread::sleep_for(chrono::seconds(1));
    }

    KPL::stopDeviceObservation();
    return 0;
}
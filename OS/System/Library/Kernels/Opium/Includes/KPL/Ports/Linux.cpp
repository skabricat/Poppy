#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <random>
#include <deque>

#include <libudev.h>

using namespace std;

namespace KPL {
    struct Device {
        string ID, name, subsystem, node, driver;
        map<string, string> properties;

        Device(udev_device* dev) {
            ID = udev_device_get_syspath(dev) ?: "";
            name = filesystem::path(ID).filename().string();
            subsystem = udev_device_get_subsystem(dev) ?: "";
            node = udev_device_get_devnode(dev) ?: "";
            driver = udev_device_get_driver(dev) ?: "";

            udev_list_entry* attributeEntries = udev_device_get_sysattr_list_entry(dev);
            udev_list_entry* attributeEntry;

            udev_list_entry_foreach(attributeEntry, attributeEntries) {
                const char* k = udev_list_entry_get_name(attributeEntry);
                const char* v = udev_device_get_sysattr_value(dev, k);

                if(k && v) properties[k] = v;
            }
        }
    };

    using DeviceEventCallback = function<void(const string& action, const Device&)>;

    vector<Device> devices;
    vector<DeviceEventCallback> deviceObservers;

    mutex devicesMutex,
          deviceObserversMutex;

    atomic<bool> deviceMonitorRunning = false;
    thread deviceMonitorThread;

    void addDeviceObserver(DeviceEventCallback observer) {
        lock_guard lock(deviceObserversMutex);

        deviceObservers.push_back(observer);
    }

    void notifyDeviceObservers(const string& action, const Device& device) {
        lock_guard lock(deviceObserversMutex);

        for(DeviceEventCallback& observer : deviceObservers) {
            observer(action, device);
        }
    }

    void loadDevices() {
        lock_guard lock(devicesMutex);
        udev* udev = udev_new();

        if(!udev) {
            return;
        }

        udev_enumerate* enumerate = udev_enumerate_new(udev);

        udev_enumerate_add_match_subsystem(enumerate, NULL);
        udev_enumerate_scan_devices(enumerate);

        udev_list_entry* deviceEntries = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry* deviceEntry;

        udev_list_entry_foreach(deviceEntry, deviceEntries) {
            const char* path = udev_list_entry_get_name(deviceEntry);
            udev_device* dev = udev_device_new_from_syspath(udev, path);

            if(!dev) {
                continue;
            }

            Device device = dev;

            devices.push_back(device);
            udev_device_unref(dev);
            notifyDeviceObservers("add", device);
        }

        udev_enumerate_unref(enumerate);
        udev_unref(udev);
    }

    vector<Device> getDevices() {
        lock_guard lock(devicesMutex);

        return devices;
    }

    void deviceMonitorLoop() {
        udev* udev = udev_new();

        if(!udev) {
            return;
        }

        udev_monitor* monitor = udev_monitor_new_from_netlink(udev, "kernel");

        if(!monitor) {
            udev_unref(udev);

            return;
        }

        udev_monitor_filter_add_match_subsystem_devtype(monitor, NULL, NULL);
        udev_monitor_enable_receiving(monitor);

        int FD = udev_monitor_get_fd(monitor);

        while(deviceMonitorRunning) {
            fd_set FDs;
            FD_ZERO(&FDs);
            FD_SET(FD, &FDs);
            timeval TV = {1, 0};  // 1-second timeout

            if(select(FD+1, &FDs, NULL, NULL, &TV) <= 0) {
                continue;
            }

            if(FD_ISSET(FD, &FDs)) {
                if(udev_device* dev = udev_monitor_receive_device(monitor)) {
                    string action = udev_device_get_action(dev) ?: "change";
                    Device device = dev;

                    {
                        lock_guard lock(devicesMutex);

                        if(action == "add") {
                            devices.push_back(device);
                        } else
                        if(action == "remove") {
                            devices.erase(remove_if(devices.begin(), devices.end(), [&](Device& x) { return x.ID == device.ID; }), devices.end());
                        } else
                        for(Device& x : devices) {
                            if(x.ID == device.ID) {
                                x = device;
                            }
                        }
                    }

                    notifyDeviceObservers(action, device);
                    udev_device_unref(dev);
                }
            }
        }

        udev_monitor_unref(monitor);
        udev_unref(udev);
    }

    void startDeviceObservation() {
        if(!deviceMonitorRunning) {
            deviceMonitorRunning = true;
            deviceMonitorThread = thread(deviceMonitorLoop);
        }
    }

    void stopDeviceObservation() {
        if(deviceMonitorRunning) {
            deviceMonitorRunning = false;

            if(deviceMonitorThread.joinable()) {
                deviceMonitorThread.join();
            }
        }
    }
}

int main() {
    deque<string> logLines;
    int maxLines = 48;

    KPL::addDeviceObserver([&](const string& action, const KPL::Device& device){
        string line = action+"  "+device.name;

        if(!device.subsystem.empty()) line += " ["+device.subsystem+"]";
        if(!device.node.empty())      line += " Node: "+device.node;
        if(!device.driver.empty())    line += " Driver: "+device.driver;

        logLines.push_back(line);

        if(logLines.size() > maxLines) {
            logLines.pop_front();
        }

        cout << "\033[H\033[2J\033[3J";
        cout << "==== Device Event Log (" << logLines.size() << " entries) ====\n\n";

        for(const string& line : logLines) {
            cout << line << "\n";
        }

        cout << flush;
    });

    KPL::loadDevices();
    KPL::startDeviceObservation();
    this_thread::sleep_for(chrono::seconds(30));
    KPL::stopDeviceObservation();

    return 0;
}
//#pragma once

#include <iostream>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <memory>

using namespace std;

namespace KPL {  // Kernel Portability Layer: Linux Backend
    void readTextFileIfExists(const filesystem::path& p, string& out) {
        if(!filesystem::exists(p)) return;
        ifstream f(p);
        if(f) {
            getline(f, out);
        }
    }

    struct Device {
        string ID;
        string name;
        string subsystem;

        string vendorID;
        string productID;
        string driver;
        string deviceNode;

        map<string, string> properties;

        void normalize() {
            name = filesystem::path(ID).filename().string();

            if(properties.contains("uevent")) {
                const string& u = properties.at("uevent");
                auto pos = u.find("DRIVER=");
                if(pos != string::npos) {
                    string val = u.substr(pos+7);
                    auto end = val.find('\n');
                    if(end != string::npos) val = val.substr(0, end);
                    driver = val;
                }
            }

            if(properties.contains("dev"))
                deviceNode = "/dev/"+name;

            if(properties.contains("vendor"))
                vendorID = properties.at("vendor");

            if(properties.contains("device"))
                productID = properties.at("device");
        }
    };

    struct SysFSNode {
        string path;
        map<string, string> attributes;

        void loadAttributes() {
            attributes.clear();
            const filesystem::path root = path;

            {
                filesystem::path link = root / "subsystem";
                if(filesystem::is_symlink(link)) {
                    attributes["subsystem"] = filesystem::read_symlink(link).filename().string();
                }
            }

            for(auto& entry : filesystem::recursive_directory_iterator(root, filesystem::directory_options::skip_permission_denied)) {
                if(!entry.is_regular_file())
                    continue;

                const auto& p = entry.path();
                auto rel = filesystem::relative(p, root).string();
                string text;
                readTextFileIfExists(p, text);

                if(!text.empty()) {
                    attributes[rel] = text;
                }
            }
        }

        string getSubsystem() const {
            string subsystem;

            auto it = attributes.find("subsystem");
            if(it != attributes.end() && !it->second.empty())
                subsystem = it->second;
            else {
                filesystem::path p(path);
                vector<string> parts;
                for(auto& x : p) parts.push_back(x.string());
                if(parts.size() > 4 && parts[3] == "virtual")
                    subsystem = parts[4];
            }

            return subsystem;
        }
    };

    void collectDeviceDirs(const filesystem::path& root, int depth, vector<filesystem::path>& out) {
        if(depth == 0) {
            if(filesystem::is_directory(root)) {
                out.push_back(root);
            }
            return;
        }

        if(!filesystem::is_directory(root))
            return;

        for(auto& entry : filesystem::directory_iterator(root)) {
            if(entry.is_directory()) {
                collectDeviceDirs(entry.path(), depth-1, out);
            }
        }
    }

    vector<SysFSNode> getSysFSNodes() {
        vector<SysFSNode> result;
        vector<filesystem::path> deviceDirs;

        const filesystem::path busRoot = "/sys/bus";
        if(filesystem::exists(busRoot)) {
            for(auto& bus : filesystem::directory_iterator(busRoot)) {
                auto devices = bus.path() / "devices";
                if(filesystem::is_directory(devices)) {
                    collectDeviceDirs(devices, 1, deviceDirs);
                }
            }
        }

        const filesystem::path classRoot = "/sys/class";
        if(filesystem::exists(classRoot)) {
            for(auto& cls : filesystem::directory_iterator(classRoot)) {
                collectDeviceDirs(cls.path(), 1, deviceDirs);
            }
        }

        const filesystem::path virtualRoot = "/sys/devices/virtual";
        if(filesystem::exists(virtualRoot)) {
            collectDeviceDirs(virtualRoot, 2, deviceDirs);
        }

        for(auto& p : deviceDirs) {
            SysFSNode node;
            node.path = p.string();
            node.loadAttributes();
            result.push_back(move(node));
        }

        return result;
    }

    vector<Device> getDevices() {
        vector<Device> list;

        for(auto& node : getSysFSNodes()) {
            Device device;
            device.ID = node.path;
            device.subsystem = node.getSubsystem();
            device.properties = node.attributes;
            device.normalize();
            list.push_back(move(device));
        }

        return list;
    }
}

int main() {  // Test
    for(const auto& device : KPL::getDevices()) {
        cout << "Device: " << device.ID << endl;
        cout << "  Name:  " << device.name << endl;
        cout << "  Subsystem: " << device.subsystem << endl;

        if(!device.vendorID.empty())
            cout << "  Vendor:  " << device.vendorID << endl;

        if(!device.productID.empty())
            cout << "  Product: " << device.productID << endl;

        if(!device.driver.empty())
            cout << "  Driver:  " << device.driver << endl;

        if(!device.deviceNode.empty())
            cout << "  Device Node: " << device.deviceNode << endl;

        cout << "  Raw properties:\n";
        for(auto& [k, v] : device.properties) {
            cout << "    " << k << " = " << v << endl;
        }

        cout << endl;
    }

    return 0;
}
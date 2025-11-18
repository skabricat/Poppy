#pragma once

#include "Std.cpp"

namespace IOKit {
    using PropertyValue = variant<int, bool, string>;
    using PropertyDictionary = unordered_map<string, PropertyValue>;

    const string kIOBSDNameKey        = "IOBSDName";
    const string kIOBSDTypeKey        = "IOBSDType";       // string: "character", "block"
    const string kIOBSDMajorKey       = "IOBSDMajor";      // optional
    const string kIOBSDMinorKey       = "IOBSDMinor";      // optional
    const string kIOBSDUnitKey        = "IOBSDUnit";       // optional
    const string kIOBSDCreateTTYKey   = "IOBSDCreateTTY";  // bool
    const string kIOBSDPermissionsKey = "IOBSDPerm";       // optional string: "600", "666"...

    struct IODevice {
        size_t ID = 0;
        string name;
        bool isOpen = false;
        size_t ownerPID = 0;
        PropertyDictionary properties;

        vector<function<void(const string&)>> onDataReceived;
        vector<function<void(const string&)>> onDataSent;

        IODevice() = default;
        virtual ~IODevice() = default;

        void setProperty(const string& key, const PropertyValue& value) {
            properties[key] = value;
        }

        bool getProperty(const string& key, PropertyValue& out) const {
            auto it = properties.find(key);
            if(it == properties.end()) return false;
            out = it->second;
            return true;
        }

        virtual bool open(size_t PID) {
            if(isOpen) return false;
            isOpen = true;
            ownerPID = PID;
            return true;
        }

        virtual void close() {
            isOpen = false;
            ownerPID = 0;
        }

        virtual void write(const string &data) {}  // данные, которые ядро/процесс/console пишет в устройство
        virtual string read() { return string(); }  // данные, которые устройство предоставляет на чтение

        void notifyDataReceived(const string& data) {
            for(auto& fn : onDataReceived) fn(data);
        }

        void notifyDataSent(const string& data) {
            for(auto& fn : onDataSent) fn(data);
        }
    };

    unordered_map<size_t, unique_ptr<IODevice>> deviceRegistry;
    vector<function<void(IODevice*)>> publishNotifiers;

    void registerPublishNotifier(function<void(IODevice*)> fn) {
        publishNotifiers.push_back(fn);
    }

    void publishDevice(IODevice* device) {
        for(auto& fn : publishNotifiers) fn(device);
    }

    struct IOSerialDevice : IODevice {
        vector<char> inputBuffer;    // данные, которые устройство само получило от физики
        vector<char> outputBuffer;   // данные, которые были записаны в устройство
        bool connected = false;

        virtual bool connect() { connected = true; return true; }
        virtual void disconnect() { connected = false; }

        void write(const string &data) override {
            // записываем в локальный буфер (в реале - передаём на линию)
            outputBuffer.insert(outputBuffer.end(), data.begin(), data.end());
            notifyDataSent(data);
        }

        void pushFromHardware(const string &data) {
            inputBuffer.insert(inputBuffer.end(), data.begin(), data.end());
            notifyDataReceived(data);
        }

        string read() override {
            string s(inputBuffer.begin(), inputBuffer.end());
            inputBuffer.clear();
            return s;
        }
    };

    struct IOFrameBuffer : IODevice {
        string framebufferLog;

        void drawText(const string &text) {
            framebufferLog += text;

            cout << "[framebuffer:" << name << "] " << text;
        }

        void write(const string &data) override {
            drawText(data);
        }

        string read() override { return framebufferLog; }
    };

    struct IOConsole : IODevice {
        vector<size_t> branchTargets;

        void addTarget(size_t deviceID) {
            for(auto ID : branchTargets) if(ID == deviceID) return;
            branchTargets.push_back(deviceID);
        }

        void removeTarget(size_t deviceID) {
            branchTargets.erase(
                remove(branchTargets.begin(), branchTargets.end(), deviceID),
                branchTargets.end());
        }

        void write(const string &data) override {
            for(size_t targetID : branchTargets) {
                auto it = deviceRegistry.find(targetID);
                if(it == deviceRegistry.end()) continue;
                IODevice *device = it->second.get();
                device->write(data);
            }
        }
    };

    template<typename T, typename... Args>
    T& registerDevice(size_t ID, const string& name, Args&&... args) {
        auto device = make_unique<T>(forward<Args>(args)...);
        device->ID = ID;
        device->name = name;
        auto& ref = *device;
        deviceRegistry[ID] = move(device);
        return ref;
    }

    IODevice* getDevice(size_t ID) {
        auto it = deviceRegistry.find(ID);
        return (it != deviceRegistry.end()) ? it->second.get() : nullptr;
    }
}
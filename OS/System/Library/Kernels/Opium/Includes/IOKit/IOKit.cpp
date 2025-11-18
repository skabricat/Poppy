#pragma once

#include "Std.cpp"

namespace IOKit {
    struct IODevice {
        size_t ID = 0;
        string name;
        bool isOpen = false;
        size_t ownerPID = 0;

        vector<function<void(const string&)>> onDataReceived;
        vector<function<void(const string&)>> onDataSent;

        IODevice() = default;
        virtual ~IODevice() = default;

        virtual bool open(size_t PID) {
            if (isOpen) return false;
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
            for (auto& fn : onDataReceived) fn(data);
        }

        void notifyDataSent(const string& data) {
            for (auto& fn : onDataSent) fn(data);
        }
    };

    unordered_map<size_t, unique_ptr<IODevice>> deviceRegistry;

    struct IOSerialDevice : IODevice {
        vector<char> inputBuffer;    // данные, которые устройство само получило от физики
        vector<char> outputBuffer;   // данные, которые были записаны в устройство
        bool connected = false;

        virtual bool connect() { connected = true; return true; }
        virtual void disconnect() { connected = false; }

        void write(const string &data) override {
            // записываем в локальный буфер (в реале — передаём на линию)
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

        void addTarget(size_t devId) {
            for (auto ID : branchTargets) if (ID == devId) return;
            branchTargets.push_back(devId);
        }

        void removeTarget(size_t devId) {
            branchTargets.erase(
                remove(branchTargets.begin(), branchTargets.end(), devId),
                branchTargets.end());
        }

        void write(const string &data) override {
            for (size_t targetID : branchTargets) {
                auto it = deviceRegistry.find(targetID);
                if (it == deviceRegistry.end()) continue;
                IODevice *dev = it->second.get();
                dev->write(data);
            }
        }
    };

    template<typename T, typename... Args>
    T& registerDevice(size_t ID, const string& name, Args&&... args) {
        auto dev = make_unique<T>(forward<Args>(args)...);
        dev->ID = ID;
        dev->name = name;
        auto& ref = *dev;
        deviceRegistry[ID] = move(dev);
        return ref;
    }

    IODevice* getDevice(size_t ID) {
        auto it = deviceRegistry.find(ID);
        return (it != deviceRegistry.end()) ? it->second.get() : nullptr;
    }
}
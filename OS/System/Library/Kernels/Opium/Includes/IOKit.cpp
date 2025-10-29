#pragma once

#include "Std.cpp"

namespace IOKit {
    struct IODevice {
        usize id = 0;                // уникальный ID устройства
        string name;                 // имя, например "tty0" или "console"
        bool isOpen = false;         // открыт ли дескриптор
        usize ownerPID = 0;          // PID процесса, который открыл устройство

        // Подписчики (наблюдатели)
        vector<function<void(const string&)>> onDataReceived;
        vector<function<void(const string&)>> onDataSent;

        IODevice() = default;
        virtual ~IODevice() = default;

        virtual bool open(usize pid) {
            if (isOpen) return false;
            isOpen = true;
            ownerPID = pid;
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

    unordered_map<usize, unique_ptr<IODevice>> deviceRegistry;

    struct IOSerialDevice : IODevice {
        vector<char> inputBuffer;    // данные, которые устройство сам получил от физики
        vector<char> outputBuffer;   // данные, которые были записаны в устройство (для inspect)
        bool connected = false;

        virtual bool connect() { connected = true; return true; }
        virtual void disconnect() { connected = false; }

        void write(const string &data) override {
            // записываем в локальный буфер (в реале — передаём на линию)
            outputBuffer.insert(outputBuffer.end(), data.begin(), data.end());
            notifyDataSent(data); // например, логируем или пересылаем дальше
        }

        void pushFromHardware(const string &data) {
            inputBuffer.insert(inputBuffer.end(), data.begin(), data.end());
            notifyDataReceived(data); // уведомляем всех подписчиков
        }

        string read() override {
            string s(inputBuffer.begin(), inputBuffer.end());
            inputBuffer.clear();
            return s;
        }
    };

    struct IOFrameBuffer : IODevice {
        string framebufferLog; // текстовая репрезентация вывода для модели

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
        vector<usize> branchTargets;

        void addTarget(usize devId) {
            for (auto id : branchTargets) if (id == devId) return;
            branchTargets.push_back(devId);
        }

        void removeTarget(usize devId) {
            branchTargets.erase(
                remove(branchTargets.begin(), branchTargets.end(), devId),
                branchTargets.end());
        }

        void write(const string &data) override {
            for (usize targetID : branchTargets) {
                auto it = deviceRegistry.find(targetID);
                if (it == deviceRegistry.end()) continue;
                IODevice *dev = it->second.get();
                dev->write(data);
            }
        }
    };

    template<typename T, typename... Args>
    T& registerDevice(usize id, const string& name, Args&&... args) {
        auto dev = make_unique<T>(forward<Args>(args)...);
        dev->id = id;
        dev->name = name;
        auto& ref = *dev;
        deviceRegistry[id] = move(dev);
        return ref;
    }

    IODevice* getDevice(usize id) {
        auto it = deviceRegistry.find(id);
        return (it != deviceRegistry.end()) ? it->second.get() : nullptr;
    }
}
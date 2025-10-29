#pragma once

#include "Std.cpp"

namespace VFS {
    // --- Типы vnode (vtype)
    enum class VType {
        VNON,   // неинициализированный
        VREG,   // обычный файл
        VDIR,   // директория
        VCHR,   // символьное устройство
        VBLK,   // блочное устройство
        VFIFO,  // FIFO / pipe
        VSOCK,  // сокет
        VBAD    // повреждённый
    };

    // --- Основные операции для файловой системы
    struct VNodeOps {
        function<int()> open;
        function<int()> close;
        function<string()> read;
        function<int(const string&)> write;
    };

    // --- Таблица операций устройства
    struct DevSW {
        function<int(int)> open;
        function<int(int)> close;
        function<string(int)> read;
        function<int(int, const string&)> write;
    };

    // --- Узел устройства
    struct DeviceNode {
        int major = 0;
        int minor = 0;
        string name;
        bool isBlock = false;     // true → bdev, false → cdev
        shared_ptr<DevSW> ops;
    };

    struct Mount;

    // --- vnode — абстракция файла
    struct VNode {
        string name;
        VType type = VType::VNON;
        weak_ptr<Mount> mount;       // точка монтирования, где находится vnode
        shared_ptr<DeviceNode> dev;  // если это устройство
        shared_ptr<VNodeOps> ops;    // операции файловой системы
    };

    // --- Файловая система (интерфейс)
    struct FileSystem {
        string name;
        unordered_map<string, shared_ptr<VNode>> entries;

        virtual shared_ptr<VNode> lookup(const string& path) {
            auto it = entries.find(path);
            return (it != entries.end()) ? it->second : nullptr;
        }

        virtual void mount(const string& path) {}
        virtual void unmount() {}
    };

    // --- Mount-пойнт
    struct Mount : enable_shared_from_this<Mount> {
        string path;                   // путь, куда смонтировано
        shared_ptr<FileSystem> fs;     // объект ФС
    };

    // --- Файловая система устройств
    struct DevFS : FileSystem {
        unordered_map<int, shared_ptr<DevSW>> cdevsw; // символьные драйверы
        unordered_map<int, shared_ptr<DevSW>> bdevsw; // блочные драйверы
        unordered_map<string, shared_ptr<DeviceNode>> devices;

        DevFS() { name = "devfs"; }

        void registerCDev(int major, int minor, const string& name, shared_ptr<DevSW> ops) {
            cdevsw[major] = ops;
            devices[name] = make_shared<DeviceNode>(DeviceNode{major, minor, name, false, ops});
            auto vnode = make_shared<VNode>();
            vnode->name = name;
            vnode->type = VType::VCHR;
            vnode->dev = devices[name];
            entries["/dev/" + name] = vnode;
        }

        void registerBDev(int major, int minor, const string& name, shared_ptr<DevSW> ops) {
            bdevsw[major] = ops;
            devices[name] = make_shared<DeviceNode>(DeviceNode{major, minor, name, true, ops});
            auto vnode = make_shared<VNode>();
            vnode->name = name;
            vnode->type = VType::VBLK;
            vnode->dev = devices[name];
            entries["/dev/" + name] = vnode;
        }

        shared_ptr<VNode> lookup(const string& path) override {
            auto it = entries.find(path);
            return (it != entries.end()) ? it->second : nullptr;
        }
    };

    struct VFSManager {
        vector<shared_ptr<Mount>> mounts;

        void mountFS(const string& path, shared_ptr<FileSystem> fs) {
            auto m = make_shared<Mount>();
            m->path = path;
            m->fs = fs;
            fs->mount(path);
            mounts.push_back(m);
        }

        shared_ptr<VNode> lookup(const string& path) {
            for (auto& m : mounts) {
                if (path.starts_with(m->path)) {
                    auto vnode = m->fs->lookup(path);
                    if (vnode) return vnode;
                }
            }
            return nullptr;
        }
    };
}
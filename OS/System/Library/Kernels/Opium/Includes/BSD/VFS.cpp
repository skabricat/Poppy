#pragma once

#include "Std.cpp"
#include "BSD/IO.cpp"

namespace BSD::VFS {
    struct VirtualFileSystem;
    struct VirtualNode;
    struct MountPoint;

    using VFSSP = shared_ptr<VirtualFileSystem>;
    using VNSP = shared_ptr<VirtualNode>;

    struct VirtualFileSystem {
        string name;

        struct Operations {
            function<void()> init;
            function<void()> deinit;
            function<void(MountPoint, const string&)> mount;
            function<void(MountPoint)> unmount;
            function<VNSP(MountPoint, const string&)> lookup;
        } operations;
    };

    struct VirtualNode {
        enum class Type { None, Regular, Directory, Block, Character, Link, FIFO, Socket, Bad } type = Type::None;
        BSD::IO::DeviceID deviceID = 0;

        struct Operations {
            function<void(VNSP)> open;
            function<void(VNSP)> close;
            function<string(VNSP)> read;
            function<void(VNSP, const string&)> write;
        } operations;
    };

    struct MountPoint {
        string path;
        VFSSP VFS;
    };

    unordered_map<string, VFSSP> virtualFileSystems;
    vector<MountPoint> mountPoints;

    void addVirtualFileSystem(const VirtualFileSystem& VFS) {
        if(virtualFileSystems.contains(VFS.name)) return;
        virtualFileSystems[VFS.name] = make_shared<VirtualFileSystem>(VFS);
        if(VFS.operations.init) VFS.operations.init();
    }

    void mount(const string& path, const string& name) {
        if(!virtualFileSystems.contains(name) || !virtualFileSystems[name]) return;

        if(path.empty() || path.back() == '/') {
            cerr << "Invalid mount point: " << path << "\n";
            return;
        }

        for(auto& MP : mountPoints) {
            if(MP.path == path) {
                cerr << "Mount point already in use: " << path << "\n";
                return;
            }
        }

        VFSSP VFS = virtualFileSystems[name];
        MountPoint MP = { path, VFS };

        bool success = true;
        try {
            if(VFS->operations.mount) {
                VFS->operations.mount(MP, path);
            }
        } catch(...) {
            cerr << "Mount operation failed for FS at: " << path << "\n";
            success = false;
        }

        if(success) {
            mountPoints.push_back(MP);
            cout << "Mounted FS at " << path << "\n";
        } else {
            cerr << "Mount aborted for: " << path << "\n";
        }
    }

    VNSP lookup(const string& path) {
        MountPoint* bestMatch = nullptr;
        for(auto& MP : mountPoints) {
            if(path.starts_with(MP.path))
                if(!bestMatch || MP.path.size() > bestMatch->path.size())
                    bestMatch = &MP;
        }
        if(!bestMatch->VFS || !bestMatch->VFS->operations.lookup) return nullptr;
        return bestMatch->VFS->operations.lookup(*bestMatch, path);
    }

    string read(const string& path) {
        auto VN = lookup(path);
        if(VN && VN->operations.read) return VN->operations.read(VN);
        return "";
    }

    void write(const string& path, const string& data) {
        auto VN = lookup(path);
        if(VN && VN->operations.write) VN->operations.write(VN, data);
    }
}

namespace BSD::VFS::SpecialFS {
    auto VNO = VirtualNode::Operations {
        .read = [](VNSP VN) -> string {
            if(!VN) return "";
            if(VN->type == VirtualNode::Type::Block) {}  // TODO
            if(VN->type == VirtualNode::Type::Character) {
                int majorID = BSD::IO::getMajorID(VN->deviceID);
                if(BSD::IO::characterDeviceSwitches.contains(majorID))
                    return BSD::IO::characterDeviceSwitches[majorID]->read(VN->deviceID);
            }
            return "";
        },
        .write = [](VNSP VN, const string& data) {
            if(!VN) return;
            if(VN->type == VirtualNode::Type::Block) {}  // TODO
            if(VN->type == VirtualNode::Type::Character) {
                int majorID = BSD::IO::getMajorID(VN->deviceID);
                if(BSD::IO::characterDeviceSwitches.contains(majorID))
                    BSD::IO::characterDeviceSwitches[majorID]->write(VN->deviceID, data);
            }
        }
    };
}

namespace BSD::VFS::DeviceFS {
    struct IndexNode {
        string name;
        bool isCharacter;
        BSD::IO::DeviceID deviceID;
        weak_ptr<VirtualNode> VN;  // Cache
    };

    unordered_map<string, shared_ptr<IndexNode>> indexNodes;

    string localName(const string& path) {
        auto pos = path.find_last_of('/');
        if(pos == string::npos) return path;
        return path.substr(pos+1);
    }

    void createDeviceNode(bool isCharacter, BSD::IO::DeviceID deviceID, const string& name) {
        cout << "[devfs] Created node: " << name << endl;
        indexNodes[name] = make_shared<IndexNode>(IndexNode {
            .name = name,
            .isCharacter = isCharacter,
            .deviceID = deviceID
        });
    }

    void init() {
        for(auto& [deviceID, device] : BSD::IO::blockDevices) {
            if(device) createDeviceNode(false, deviceID, device->name);
        }
        for(auto& [deviceID, device] : BSD::IO::characterDevices) {
            if(device) createDeviceNode(true, deviceID, device->name);
        }

        int hookID = BSD::IO::deviceEventHandler.add([](BSD::IO::DeviceID deviceID, bool isCharacter) {
            if(!isCharacter) {
                auto device = BSD::IO::getBlockDevice(deviceID);
                if(device) createDeviceNode(isCharacter, deviceID, device->name);
            } else {
                auto device = BSD::IO::getCharacterDevice(deviceID);
                if(device) createDeviceNode(isCharacter, deviceID, device->name);
            }
        });
    }

    void mount(MountPoint MP, const string& path) {
    //    auto root = make_shared<VirtualNode>();
    //    root->type = VirtualNode::Type::Directory;
    };

    VNSP lookup(MountPoint MP, const string& path) {
        if(!MP.VFS) return nullptr;

        string name = localName(path);

        auto it = indexNodes.find(name);
        if(it == indexNodes.end()) return nullptr;

        auto IN = it->second;
        if(!IN) return nullptr;

        auto VN = IN->VN.lock();
        if(!VN) {
            IN->VN = VN = make_shared<VirtualNode>(VirtualNode {
                .type = IN->isCharacter ? VirtualNode::Type::Character : VirtualNode::Type::Block,
                .deviceID = IN->deviceID,
                .operations = BSD::VFS::SpecialFS::VNO
            });
        }

        return VN;
    };

    auto deviceFileSystem = VirtualFileSystem {
        .name = "devfs",
        .operations = {
            .init = init,
            .mount = mount,
            .lookup = lookup
        }
    };
};
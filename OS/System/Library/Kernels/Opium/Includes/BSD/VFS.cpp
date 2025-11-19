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
        int mountCount = 0;

        struct Operations {
            function<void()> init;
            function<void()> deinit;
            function<void(MountPoint, const string&)> mount;
            function<void(MountPoint)> unmount;
            function<VNSP(MountPoint, const string&)> lookup;
        } operations;
    };

    struct VirtualNode {
        enum class Type { None, Regular, Directory, Block, Character, Link, Pipe, Socket, Bad } type = Type::None;
        IO::DeviceID deviceID = 0;

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
            VFS->mountCount++;
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
        auto virtualNode = lookup(path);
        if(virtualNode && virtualNode->operations.read) return virtualNode->operations.read(virtualNode);
        return "";
    }

    void write(const string& path, const string& data) {
        auto virtualNode = lookup(path);
        if(virtualNode && virtualNode->operations.write) virtualNode->operations.write(virtualNode, data);
    }
}

namespace BSD::VFS::SpecialFS {
    auto virtualNodeOperations = VirtualNode::Operations {
        .read = [](VNSP virtualNode) -> string {
            if(!virtualNode) return "";
            if(virtualNode->type == VirtualNode::Type::Block) {}  // TODO
            if(virtualNode->type == VirtualNode::Type::Character) {
                int majorID = IO::getMajorID(virtualNode->deviceID);
                if(IO::characterDeviceSwitches.contains(majorID))
                    return IO::characterDeviceSwitches[majorID]->read(virtualNode->deviceID);
            }
            return "";
        },
        .write = [](VNSP virtualNode, const string& data) {
            if(!virtualNode) return;
            if(virtualNode->type == VirtualNode::Type::Block) {}  // TODO
            if(virtualNode->type == VirtualNode::Type::Character) {
                int majorID = IO::getMajorID(virtualNode->deviceID);
                if(IO::characterDeviceSwitches.contains(majorID))
                    IO::characterDeviceSwitches[majorID]->write(virtualNode->deviceID, data);
            }
        }
    };
}

namespace BSD::VFS::DeviceFS {
    struct IndexNode {
        string name;
        bool isCharacter;
        IO::DeviceID deviceID;
        weak_ptr<VirtualNode> virtualNode;  // Cache
    };

    unordered_map<string, shared_ptr<IndexNode>> indexNodes;

    string localName(const string& path) {
        auto pos = path.find_last_of('/');
        if(pos == string::npos) return path;
        return path.substr(pos+1);
    }

    void createDeviceNode(bool isCharacter, IO::DeviceID deviceID, const string& name) {
        cout << "[devfs] Created node: " << name << endl;
        indexNodes[name] = make_shared<IndexNode>(IndexNode {
            .name = name,
            .isCharacter = isCharacter,
            .deviceID = deviceID
        });
    }

    void updateDeviceNode(IO::DeviceID deviceID, bool isCharacter) {
        string name;

        if(!isCharacter) {
            if(auto device = IO::getBlockDevice(deviceID)) {
                name = device->name;
            }
        } else {
            if(auto device = IO::getCharacterDevice(deviceID)) {
                name = device->name;
            }
        }

        if(!name.empty()) {
            createDeviceNode(isCharacter, deviceID, name);
        } else {
            // destroyDeviceNode(name);
        }
    }

    void init() {
        for(auto& [deviceID, device] : IO::blockDevices) {
            if(device && !device->name.empty()) {
                createDeviceNode(false, deviceID, device->name);
            }
        }
        for(auto& [deviceID, device] : IO::characterDevices) {
            if(device && !device->name.empty()) {
                createDeviceNode(true, deviceID, device->name);
            }
        }

        int hookID = IO::deviceEventHandler.add(updateDeviceNode);
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

        auto indexNode = it->second;
        if(!indexNode) return nullptr;

        auto virtualNode = indexNode->virtualNode.lock();
        if(!virtualNode) {
            indexNode->virtualNode = virtualNode = make_shared<VirtualNode>(VirtualNode {
                .type = indexNode->isCharacter ? VirtualNode::Type::Character : VirtualNode::Type::Block,
                .deviceID = indexNode->deviceID,
                .operations = SpecialFS::virtualNodeOperations
            });
        }

        return virtualNode;
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
#pragma once

#include "Std.cpp"
#include "IO.cpp"

namespace BSD::VFS {
    struct VirtualFileSystem;
    struct VirtualNode;
    struct MountPoint;
    struct DirectoryEntry;

    using VFSSP = shared_ptr<VirtualFileSystem>;
    using VNSP = shared_ptr<VirtualNode>;

    struct VirtualFileSystem {
        string name;
        int mountCount = 0;

        struct Operations {
            function<void()> init;
            function<void()> deinit;
            function<void(MountPoint&, const string&)> mount;
            function<void(MountPoint&)> unmount;
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
            function<vector<DirectoryEntry>(VNSP)> readdir;
            function<VNSP(VNSP, const string&)> lookup;
        } operations;
    };

    struct MountPoint {
        string path;
        VFSSP VFS;
        VNSP rootVN;
    };

    struct DirectoryEntry {
        string name;
        VirtualNode::Type type;
        IO::DeviceID deviceID = 0;
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

    string normalizePath(const string& path) {
        return filesystem::path(path).lexically_normal().string();
    }

    MountPoint* findMount(const string& normPath, vector<MountPoint>& mountPoints) {
        MountPoint* best = nullptr;

        for(auto& MP : mountPoints) {
            string m = MP.path;
            if(m == "/") m = "";  // чтобы "/" совпадал со всем

            if(normPath == m || normPath.starts_with(m+"/")) {
                if(!best || m.size() > best->path.size())
                    best = &MP;
            }
        }
        return best;
    }

    string makeRelative(const string& full, const string& mountPath) {
        if(mountPath == "/") {
            if(full == "/") return "";
            return full.substr(1);
        }

        if(full == mountPath)
            return "";

        return full.substr(mountPath.size()+1);
    }

    VNSP lookup(const string& rawPath) {
        string path = normalizePath(rawPath);
        MountPoint* MP = findMount(path, mountPoints);
        if(!MP || !MP->rootVN)
            return nullptr;

        path = makeRelative(path, MP->path);
        if(path.empty())
            return MP->rootVN;

        VNSP current = MP->rootVN;
        while(true) {
            auto pos = path.find('/');
            string name = (pos == string::npos) ? path : path.substr(0, pos);
            string rest = (pos == string::npos) ? "" : path.substr(pos+1);

            if(!current->operations.lookup)
                return nullptr;

            current = current->operations.lookup(current, name);
            if(!current)
                return nullptr;

            if(rest.empty())
                return current;

            path = rest;
        }
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

    vector<DirectoryEntry> readdir(const string& path) {
        auto node = lookup(path);
        if(!node || node->type != VirtualNode::Type::Directory)
            return {};
        if(node->operations.readdir)
            return node->operations.readdir(node);
        return {};
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

    unordered_map<string, IndexNode> indexNodes;
    VNSP rootVirtualNode;

    void addIndexNode(bool isCharacter, IO::DeviceID deviceID, const string& name) {
        cout << "[devfs] Created node: " << name << endl;
        indexNodes[name] = IndexNode {
            .name = name,
            .isCharacter = isCharacter,
            .deviceID = deviceID
        };
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
            addIndexNode(isCharacter, deviceID, name);
        } else {
            // removeDeviceNode(name);
        }
    }

    VNSP getVirtualNode(IndexNode& indexNode) {
        auto virtualNode = indexNode.virtualNode.lock();
        if(!virtualNode) {
            indexNode.virtualNode = virtualNode = make_shared<VirtualNode>(VirtualNode {
                .type = indexNode.isCharacter ? VirtualNode::Type::Character : VirtualNode::Type::Block,
                .deviceID = indexNode.deviceID,
                .operations = SpecialFS::virtualNodeOperations
            });
        }

        return virtualNode;
    }

    void init() {
        rootVirtualNode = make_shared<VirtualNode>();
        rootVirtualNode->type = VirtualNode::Type::Directory;
        rootVirtualNode->operations.readdir = [](VNSP) {
            vector<DirectoryEntry> result;
            for(auto& [name, indexNode] : indexNodes) {
                result.push_back({
                    .name = name,
                    .type = indexNode.isCharacter ? VirtualNode::Type::Character : VirtualNode::Type::Block,
                    .deviceID = indexNode.deviceID
                });
            }
            return result;
        };
        rootVirtualNode->operations.lookup = [](VNSP, const string& name) -> VNSP {
            auto it = indexNodes.find(name);
            if(it == indexNodes.end()) return nullptr;
            return getVirtualNode(it->second);
        };

        for(auto& [deviceID, device] : IO::blockDevices) {
            if(device && !device->name.empty()) {
                addIndexNode(false, deviceID, device->name);
            }
        }
        for(auto& [deviceID, device] : IO::characterDevices) {
            if(device && !device->name.empty()) {
                addIndexNode(true, deviceID, device->name);
            }
        }

        int hookID = IO::deviceEventHandler.add(updateDeviceNode);
    }

    void mount(MountPoint& mp, const string& path) {
        mp.rootVN = rootVirtualNode;
    }

    auto deviceFileSystem = VirtualFileSystem {
        .name = "devfs",
        .operations = {
            .init = init,
            .mount = mount
        }
    };
};
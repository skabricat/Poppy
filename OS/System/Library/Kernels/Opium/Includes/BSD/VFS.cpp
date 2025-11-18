#pragma once

#include "Std.cpp"
#include "BSD/IO.cpp"

namespace BSD::VFS {
    struct MountPoint;
    struct VirtualNode;

    using VNSP = shared_ptr<VirtualNode>;

    struct VirtualNode {
        enum class Type { None, Regular, Directory, Block, Character, Link, FIFO, Socket, Bad } type = Type::None;
        weak_ptr<MountPoint> mountPoint;
        BSD::IO::DeviceID deviceID = 0;

        struct Operations {
            function<void(VNSP)> open;
            function<void(VNSP)> close;
            function<string(VNSP)> read;
            function<void(VNSP, const string&)> write;
        } operations;
    };

    struct VirtualFileSystem;

    using VFSSP = shared_ptr<VirtualFileSystem>;

    struct MountPoint {
        string path;
        VFSSP VFS;
    };

    vector<MountPoint> mountPoints;

    struct VirtualFileSystem {
        struct Operations {
            function<VNSP(VFSSP, const string&)> lookup;
            function<void(VFSSP, const string&)> mount;
            function<void(VFSSP)> unmount;
        } operations;

        VirtualFileSystem() {
            operations.mount = [](VFSSP VFS, const string& path) {
                if(!VFS) return;
                mountPoints.push_back({ path, VFS });
            };
        }

        virtual ~VirtualFileSystem() = default;
    };

    void mount(const string& path, VFSSP VFS) {
        if(VFS && VFS->operations.mount) VFS->operations.mount(VFS, path);
    }

    VNSP lookup(const string& path) {
        for (auto& MP : mountPoints) {
            if (path.starts_with(MP.path)) {
                if(!MP.VFS || !MP.VFS->operations.lookup) return nullptr;
                auto VN = MP.VFS->operations.lookup(MP.VFS, path);
                if (VN) return VN;
            }
        }
        return nullptr;
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

    // ----------------------------------------------------------------

    auto SFSVNO = VirtualNode::Operations {  // Special File System Virtual Node Operations
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

    struct DeviceFileSystem : VirtualFileSystem {
        struct IndexNode {
            string name;
            bool isCharacter;
            BSD::IO::DeviceID deviceID;
            weak_ptr<VirtualNode> VN;  // Cache
        };

        unordered_map<string, shared_ptr<IndexNode>> indexNodes;

        DeviceFileSystem() : VirtualFileSystem() {
            operations.lookup = [](VFSSP VFS, const string& path) -> VNSP {
                if(!VFS) return nullptr;

                auto deviceFS = dynamic_pointer_cast<DeviceFileSystem>(VFS);
                if(!deviceFS) return nullptr;

                string name = deviceFS->localName(path);

                auto it = deviceFS->indexNodes.find(name);
                if(it == deviceFS->indexNodes.end()) return nullptr;

                auto IN = it->second;
                if(!IN) return nullptr;

                auto VN = IN->VN.lock();
                if(!VN) {
                    IN->VN = VN = make_shared<VirtualNode>(VirtualNode{
                        .type = IN->isCharacter ? VirtualNode::Type::Character : VirtualNode::Type::Block,
                        .deviceID = IN->deviceID,
                        .operations = SFSVNO
                    });
                }

                return VN;
            };
        }

        string localName(const string& path) {
            auto pos = path.find_last_of('/');
            if(pos == string::npos) return path;
            return path.substr(pos + 1);
        }

        void createDeviceNode(bool isCharacter, BSD::IO::DeviceID deviceID, const string& name) {
            auto IN = make_shared<IndexNode>();
            IN->name = name;
            IN->isCharacter = isCharacter;
            IN->deviceID = deviceID;

            indexNodes[name] = IN;
        }
    };
}
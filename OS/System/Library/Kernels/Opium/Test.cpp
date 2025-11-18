#include <functional>
#include <iostream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <vector>
#include <memory>

using namespace std;

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

namespace BSD {
    template<typename... Args>
    class EventHandler {
        struct Entry {
            int ID;
            function<void(Args...)> callback;
        };

        vector<Entry> entries;
        int nextID = 1;

    public:
        int add(function<void(Args...)> callback) {
            int ID = nextID++;
            entries.push_back({ID, move(callback)});
            return ID;
        }

        void remove(int ID) {
            entries.erase(
                remove_if(entries.begin(), entries.end(), [ID](const Entry& e){ return e.ID == ID; }),
                entries.end()
            );
        }

        void notify(Args... arguments) {
            for(auto& entry : entries) {
                entry.callback(arguments...);
            }
        }

        bool empty() const {
            return entries.empty();
        }
    };
}

namespace BSD::IO {
    using DeviceID = uint32_t;

    int nextMajorID = 1;
    unordered_map<int, int> nextMinorForMajor;
    EventHandler<DeviceID, bool> deviceEventHandler;  // isCharacter

    DeviceID createDeviceID(int majorID, int minorID) {
        return ((majorID & 0xFF) << 8) | (minorID & 0xFF);
    }

    int getMajorID(DeviceID d) {
        return (d >> 8) & 0xFF;
    }

    int getMinorID(DeviceID d) {
        return d & 0xFF;
    }

    DeviceID allocateDeviceID() {
        int majorID = nextMajorID++;
        int minorID = 0;
        nextMinorForMajor[majorID] = 1;

        return createDeviceID(majorID, minorID);
    }

    DeviceID allocateDeviceID(int majorID) {
        int minorID = nextMinorForMajor[majorID]++;

        return createDeviceID(majorID, minorID);
    }

    // ----------------------------------------------------------------

    struct BlockDeviceSwitch {
        function<int(DeviceID)> open;
        function<int(DeviceID)> close;
        function<int(DeviceID, char*, size_t)> strategy;
    };

    struct BlockDevice {
        DeviceID ID;
        shared_ptr<BlockDeviceSwitch> sw;
        string name;
    };

    unordered_map<int, shared_ptr<BlockDeviceSwitch>> blockDeviceSwitches;
    unordered_map<DeviceID, shared_ptr<BlockDevice>> blockDevices;

    void addBlockDeviceSwitch(int majorID, shared_ptr<BlockDeviceSwitch> sw) {
        blockDeviceSwitches[majorID] = sw;
    }

    shared_ptr<BlockDevice> addBlockDevice(DeviceID ID, const string& name) {
        int majorID = getMajorID(ID);

        auto it = blockDeviceSwitches.find(majorID);
        if(it == blockDeviceSwitches.end())
            throw runtime_error("Device switch is not registered");

        auto device = make_shared<BlockDevice>(BlockDevice { ID, it->second, name });
        blockDevices[ID] = device;
        deviceEventHandler.notify(ID, false);
        return device;
    }

    shared_ptr<BlockDevice> getBlockDevice(DeviceID ID) {
        auto it = blockDevices.find(ID);
        return (it != blockDevices.end()) ? it->second : nullptr;
    }

    int open(const BlockDevice& device) {
        return device.sw->open(device.ID);
    }

    int close(const BlockDevice& device) {
        return device.sw->close(device.ID);
    }

    int strategy(const BlockDevice& device, char* buf, size_t len) {
        return device.sw->strategy(device.ID, buf, len);
    }

    // ----------------------------------------------------------------

    struct CharacterDeviceSwitch {
        function<int(DeviceID)> open;
        function<int(DeviceID)> close;
        function<string(DeviceID)> read;
        function<int(DeviceID, const string&)> write;
    };

    struct CharacterDevice {
        DeviceID ID;
        shared_ptr<CharacterDeviceSwitch> sw;
        string name;
    };

    unordered_map<int, shared_ptr<CharacterDeviceSwitch>> characterDeviceSwitches;
    unordered_map<DeviceID, shared_ptr<CharacterDevice>> characterDevices;

    void addCharacterDeviceSwitch(int majorID, shared_ptr<CharacterDeviceSwitch> sw) {
        characterDeviceSwitches[majorID] = sw;
    }

    shared_ptr<CharacterDevice> addCharacterDevice(DeviceID ID, const string& name) {
        int majorID = getMajorID(ID);

        auto it = characterDeviceSwitches.find(majorID);
        if(it == characterDeviceSwitches.end())
            throw runtime_error("Device switch is not registered");

        auto device = make_shared<CharacterDevice>(CharacterDevice { ID, it->second, name });
        characterDevices[ID] = device;
        deviceEventHandler.notify(ID, true);
        return device;
    }

    shared_ptr<CharacterDevice> getCharacterDevice(DeviceID ID) {
        auto it = characterDevices.find(ID);
        return (it != characterDevices.end()) ? it->second : nullptr;
    }

    int open(const CharacterDevice& device) {
        return device.sw->open(device.ID);
    }

    int close(const CharacterDevice& device) {
        return device.sw->close(device.ID);
    }

    string read(const CharacterDevice& device) {
        return device.sw->read(device.ID);
    }

    int write(const CharacterDevice& device, const string& s) {
        return device.sw->write(device.ID, s);
    }
}

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

namespace BSD::TTY {
    struct Terminal {
        size_t ID = 0;
        size_t sessionID = 0;
        size_t foregroundProcessGroupID = 0;
        vector<char> inputBuffer;
        vector<char> outputBuffer;
        BSD::IO::DeviceID deviceID = 0;

        void attachDevice(BSD::IO::DeviceID device) { deviceID = device; }

        void pushInput(const string &s) {
            inputBuffer.insert(inputBuffer.end(), s.begin(), s.end());
        }

        string read() {
            string s(inputBuffer.begin(), inputBuffer.end());
            inputBuffer.clear();
            return s;
        }

        void write(const string& s) {
            outputBuffer.insert(outputBuffer.end(), s.begin(), s.end());
            int majorID = BSD::IO::getMajorID(deviceID);
            if(BSD::IO::characterDeviceSwitches.contains(majorID))
                BSD::IO::characterDeviceSwitches[majorID]->write(deviceID, s);
        }
    };

    unordered_map<size_t, unique_ptr<Terminal>> terminals;

    Terminal& createTerminal(size_t ID) {
        auto p = make_unique<Terminal>();
        p->ID = ID;
        auto &ref = *p;
        terminals[ID] = move(p);
        return ref;
    }

    Terminal* getTerminal(size_t ID) {
        auto it = terminals.find(ID);
        return it != terminals.end() ? it->second.get() : nullptr;
    }
}

int main() {
    auto& console = IOKit::registerDevice<IOKit::IOConsole>(1, "console0");
    auto& fb0 = IOKit::registerDevice<IOKit::IOFrameBuffer>(2, "fb0");
    int serialMajorID = 3;
    int serialMinorID = 0;
    auto deviceID = BSD::IO::createDeviceID(serialMajorID, serialMinorID);
    auto& serial = IOKit::registerDevice<IOKit::IOSerialDevice>(serialMajorID, "tty.serial0");
    serial.connect();
    console.addTarget(fb0.ID);

    auto serialDeviceSwitch = make_shared<BSD::IO::CharacterDeviceSwitch>();
    serialDeviceSwitch->write = [&](BSD::IO::DeviceID device, const string& s){ serial.write(s); return 0; };
    serialDeviceSwitch->read = [&](BSD::IO::DeviceID device){ return serial.read(); };
    BSD::IO::addCharacterDeviceSwitch(serialMajorID, serialDeviceSwitch);
    BSD::IO::addCharacterDevice(deviceID, "tty0");

    auto& terminal = BSD::TTY::createTerminal(100);
    terminal.attachDevice(deviceID);
    serial.onDataReceived.push_back([&](const string& data) {
        terminal.pushInput(data);
    });
    serial.onDataSent.push_back([&](const string& data) {
        console.write("[serial TX] " + data);
    });

    console.write("Kernel boot OK\n");
    terminal.write("shell> ls -la\n");
    serial.pushFromHardware("result: file1 file2\n");
    cout << "TTY read(): " << terminal.read() << endl;
    cout << "\nFramebuffer content:\n" << fb0.read() << endl;

    BSD::VFS::addVirtualFileSystem(BSD::VFS::DeviceFS::deviceFileSystem);
    BSD::VFS::mount("/dev", "devfs");

    cout << "Writing to /dev/tty0 via VFS...\n";
    BSD::VFS::write("/dev/tty0", "echo test\n");
    serial.pushFromHardware("ok\n");
    cout << "Reading from TTY buffer: " << terminal.read() << endl;
    cout << "Reading directly from /dev/tty0 via VFS: "
         << BSD::VFS::read("/dev/tty0") << endl;

    return 0;
}
#include <functional>
#include <iostream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <vector>
#include <memory>

using namespace std;

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
    using DeviceID = uint64_t;
    using MajorID = uint32_t;
    using MinorID = uint32_t;

    MajorID nextMajorID = 1;
    unordered_map<MajorID, MinorID> nextMinorForMajor;
    EventHandler<DeviceID, bool> deviceEventHandler;  // isCharacter

    DeviceID createDeviceID(MajorID majorID, MinorID minorID) {
        return (static_cast<uint64_t>(majorID) << 32) | minorID;
    }

    MajorID getMajorID(DeviceID deviceID) {
        return deviceID >> 32;
    }

    MinorID getMinorID(DeviceID deviceID) {
        return deviceID & 0xFFFFFFFF;
    }

    DeviceID allocateDeviceID() {
        MajorID majorID = nextMajorID++;
        MinorID minorID = 0;
        nextMinorForMajor[majorID] = 1;

        return createDeviceID(majorID, minorID);
    }

    DeviceID allocateDeviceID(MajorID majorID) {
        MinorID minorID = nextMinorForMajor[majorID]++;

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

    unordered_map<MajorID, shared_ptr<BlockDeviceSwitch>> blockDeviceSwitches;
    unordered_map<DeviceID, shared_ptr<BlockDevice>> blockDevices;

    void addBlockDeviceSwitch(int majorID, shared_ptr<BlockDeviceSwitch> sw) {
        blockDeviceSwitches[majorID] = sw;
    }

    shared_ptr<BlockDevice> addBlockDevice(DeviceID ID, const string& name) {
        MajorID majorID = getMajorID(ID);

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

    unordered_map<MajorID, shared_ptr<CharacterDeviceSwitch>> characterDeviceSwitches;
    unordered_map<DeviceID, shared_ptr<CharacterDevice>> characterDevices;

    void addCharacterDeviceSwitch(MajorID majorID, shared_ptr<CharacterDeviceSwitch> sw) {
        characterDeviceSwitches[majorID] = sw;
    }

    shared_ptr<CharacterDevice> addCharacterDevice(DeviceID ID, const string& name) {
        MajorID majorID = getMajorID(ID);

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

    string normalizePath(string_view path) {
        if(path.empty()) return "/";

        bool absolute = path.front() == '/';
        vector<string_view> stack;
        size_t start = 0;

        auto push_token = [&](string_view token) {
            if(token == "."sv || token.empty()) return;

            if(token == ".."sv) {
                if(!stack.empty() && stack.back() != ".."sv) {
                    stack.pop_back();
                } else
                if(!absolute) {
                    stack.push_back(".."sv);
                }
            } else {
                stack.push_back(token);
            }
        };

        for(size_t i = 0; i <= path.size(); i++) {
            if(i == path.size() || path[i] == '/') {
                if(i > start)
                    push_token(path.substr(start, i-start));
                start = i+1;
            }
        }

        string out;
        if(absolute) out.push_back('/');

        for(size_t i = 0; i < stack.size(); i++) {
            if(i > 0) out.push_back('/');
            out.append(stack[i]);
        }

        if(out.empty()) return absolute ? "/" : ".";

        return out;
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

namespace BSD::TTY {
    struct Terminal {
        size_t ID = 0;
        size_t sessionID = 0;
        size_t foregroundProcessGroupID = 0;
        vector<char> inputBuffer;
        vector<char> outputBuffer;
        IO::DeviceID deviceID = 0;

        void attachDevice(IO::DeviceID device) { deviceID = device; }

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
            int majorID = IO::getMajorID(deviceID);
            if(IO::characterDeviceSwitches.contains(majorID))
                IO::characterDeviceSwitches[majorID]->write(deviceID, s);
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

namespace IOKit {
    using DeviceID = BSD::IO::DeviceID;
    using PropertyValue = variant<uint64_t, bool, string>;
    using PropertyDictionary = unordered_map<string, PropertyValue>;

    struct IOService {
        size_t ID = 0;
        IOService* provider = nullptr;  // Parent
        vector<IOService*> clients;     // Children
        unordered_set<size_t> clientPIDs;
        PropertyDictionary properties;
        PropertyDictionary personality;

        vector<function<void(const string&)>> onDataReceived;
        vector<function<void(const string&)>> onDataSent;

        void setProperty(const string& key, const PropertyValue& value) {
            properties[key] = value;
        }

        void setPersonalityProperty(const string& key, const PropertyValue& value) {
            personality[key] = value;
        }

        bool getProperty(const string& key, PropertyValue& out) const {
            auto it = properties.find(key);
            if(it == properties.end()) return false;
            out = it->second;
            return true;
        }

        bool isAncestor(IOService* ofService) {
            for(IOService* p = ofService; p != nullptr; p = p->provider) {
                if(p == this)
                    return true;
            }

            return false;
        }

        bool matches(IOService* provider) {
            if(isAncestor(provider)) {  // Break cycle
                return false;
            }

            auto it = personality.find("IOProviderClass");
            if(it != personality.end()) {
                PropertyValue pv;
                if(!provider->getProperty("IOClass", pv))
                    return false;
                if(!holds_alternative<string>(pv) || !holds_alternative<string>(it->second) || get<string>(pv) != get<string>(it->second))
                    return false;
            }

            // Generic property match (flat IOPropertyMatch simplification)
            for(auto& [key, value] : personality) {
                if(key == "IOProviderClass") continue;

                PropertyValue pv;
                if(!provider->getProperty(key, pv))
                    return false;
                if(pv != value)
                    return false;
            }

            return true;
        }

        virtual bool open(size_t PID) {
            if(!clientPIDs.contains(PID)) {
                clientPIDs.insert(PID);
                return true;
            }
            return false;
        }

        virtual bool close(size_t PID) {
            if(clientPIDs.contains(PID)) {
                clientPIDs.erase(PID);
                return true;
            }
            return false;
        }

        virtual void write(const string &data) {}  // данные, которые ядро/процесс/console пишет в устройство
        virtual string read() { return string(); }  // данные, которые устройство предоставляет на чтение

        void notifyDataReceived(const string& data) {
            for(auto& fn : onDataReceived) fn(data);
        }

        void notifyDataSent(const string& data) {
            for(auto& fn : onDataSent) fn(data);
        }

        virtual bool probe() { return true; }
        virtual bool start() { return true; }

        bool attach(IOService* toProvider) {
            if(provider != nullptr)
                return false;

            if(isAncestor(toProvider)) {  // Break cycle
                return false;
            }

            provider = toProvider;
            toProvider->clients.push_back(this);
            return true;
        }

        virtual void detach(IOService* fromProvider) {
            if(provider != fromProvider) return;
            auto& v = fromProvider->clients;
            v.erase(remove(v.begin(), v.end(), this), v.end());
            provider = nullptr;
        }
    };

    struct Root : IOService {
        Root() : IOService() {
            setProperty("IOClass", "Root");
        }
    };

    size_t nextID = 1;
    unordered_map<size_t, unique_ptr<IOService>> IORegistry;
    vector<function<void(IOService*)>> publishNotifiers;

    void addPublishNotifier(function<void(IOService*)> fn) {
        publishNotifiers.push_back(fn);
    }

    template<typename T, typename... Args>
    T& addService(Args&&... args) {
        auto service = make_unique<T>(forward<Args>(args)...);
        service->ID = nextID++;
        auto& ref = *service;
        IORegistry[service->ID] = move(service);
        return ref;
    }

    IOService* getService(size_t ID) {
        auto it = IORegistry.find(ID);
        return (it != IORegistry.end()) ? it->second.get() : nullptr;
    }

    IOService* rootService = &addService<Root>();

    DeviceID getBSDDeviceID(IOService* service) {
        if(!service) return 0;

        PropertyValue BSDProperty;
        BSD::IO::MajorID majorID = service->ID;
        BSD::IO::MinorID deviceMinor = 0;

        if(service->getProperty("IOBSDMajor", BSDProperty) && holds_alternative<uint64_t>(BSDProperty)) {
            majorID = get<uint64_t>(BSDProperty);
        }
        if(service->getProperty("IOBSDMinor", BSDProperty) && holds_alternative<uint64_t>(BSDProperty)) {
            deviceMinor = get<uint64_t>(BSDProperty);
        }

        return BSD::IO::createDeviceID(majorID, deviceMinor);
    }

    void attachToBSD(IOService* service) {
        if(!service) return;

        PropertyValue BSDProperty;
        string deviceName;
        char deviceType = 'b';

        if(service->getProperty("IOBSDName", BSDProperty) && holds_alternative<string>(BSDProperty)) {
            string n = get<string>(BSDProperty);

            if(!n.empty()) {
                deviceName = n;
            } else {
                cout << "[IOKit:" << service->ID << "] Empty BSD Name" << endl;
                return;
            }
        }
        if(service->getProperty("IOBSDType", BSDProperty) && holds_alternative<string>(BSDProperty)) {
            string t = get<string>(BSDProperty);

            if(t == "character") {
                deviceType = 'c';
            } else
            if(t != "block") {
                cout << "[IOKit:" << service->ID << "] Wrong BSD Type ("  << t << ")" << endl;
                return;
            }
        } else {
            return;
        }

        auto deviceID = getBSDDeviceID(service);
        auto majorID = BSD::IO::getMajorID(deviceID);

        if(deviceType == 'b') {
            auto deviceSwitch = make_shared<BSD::IO::BlockDeviceSwitch>();

            BSD::IO::addBlockDeviceSwitch(majorID, deviceSwitch);
            BSD::IO::addBlockDevice(deviceID, deviceName);
        } else {
            auto deviceSwitch = make_shared<BSD::IO::CharacterDeviceSwitch>();

            deviceSwitch->write = [service](BSD::IO::DeviceID, const string& s){
                service->write(s);
                return 0;
            };
            deviceSwitch->read = [service](BSD::IO::DeviceID){
                return service->read();
            };

            BSD::IO::addCharacterDeviceSwitch(majorID, deviceSwitch);
            BSD::IO::addCharacterDevice(deviceID, deviceName);
        }
    }

    void publishService(IOService* service) {
        attachToBSD(service);

        for(auto& fn : publishNotifiers) {
            fn(service);
        }
    }

    IOService* findMatchingProvider(IOService* client) {
        for(auto& [pid, cand] : IORegistry) {
            if(cand.get() == client) continue;
            if(client->matches(cand.get()))
                return cand.get();
        }
        return nullptr;
    }

    void matchAndStartDevices() {
        for(auto& [ID, service] : IORegistry) {
            if(!service->probe())
                continue;

            IOService* provider = findMatchingProvider(service.get());
            if(!provider) {
                provider = rootService;
            } else {
            //    cout << ID << " found nonroot provider " << provider->ID << endl;
            }

            service->attach(provider);

            if(!service->start())
                continue;

            publishService(service.get());
        }
    }

    void dumpRegistry(IOService* root, int depth = 0) {
        if(!root) return;
        cout << string(depth*4, ' ') << root->ID;
        PropertyValue name;
        if(root->getProperty("IOClass", name) && holds_alternative<string>(name))
            cout << " (" << get<string>(name) << ")";
        cout << endl;

        for(auto* child : root->clients)
            dumpRegistry(child, depth+1);
    }
}

namespace IOKit::Services {
    struct IOSerial : IOService {
        vector<char> inputBuffer;    // данные, которые устройство само получило от физики
        vector<char> outputBuffer;   // данные, которые были записаны в устройство
        bool connected = false;

        IOSerial() : IOService() {
            setProperty("IOClass", "IOSerial");
            setPersonalityProperty("IOProviderClass", "IOConsole");
        }

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

        bool probe() override {
            PropertyValue type;
            if(getProperty("IOBSDType", type)) {
                if(holds_alternative<string>(type) && get<string>(type) == "character")
                    return true;
            }

            return true;
        }

        bool start() override {
            setProperty("IOBSDName", "tty0");
            setProperty("IOBSDType", "character");
            setProperty("IOBSDMajor", BSD::IO::MajorID(ID));
            setProperty("IOBSDMinor", BSD::IO::MinorID(0));
            return connect();
        }
    };

    struct IOFramebuffer : IOService {
        string framebufferLog;

        IOFramebuffer() : IOService() {
            setProperty("IOClass", "IOFramebuffer");
            setPersonalityProperty("IOProviderClass", "IOConsole");
        }

        void drawText(const string &text) {
            framebufferLog += text;

            cout << "[framebuffer:" << ID << "] " << text;
        }

        void write(const string &data) override {
            drawText(data);
        }

        string read() override { return framebufferLog; }

        bool start() override {
            drawText("Framebuffer initialized\n");
            return true;
        }
    };

    struct IOConsole : IOService {
        vector<size_t> branchTargets;

        IOConsole() : IOService() {
            setProperty("IOClass", "IOConsole");
        }

        void addTarget(size_t serviceID) {
            for(auto ID : branchTargets) if(ID == serviceID) return;
            branchTargets.push_back(serviceID);
        }

        void removeTarget(size_t serviceID) {
            branchTargets.erase(
                remove(branchTargets.begin(), branchTargets.end(), serviceID),
                branchTargets.end()
            );
        }

        void write(const string &data) override {
            for(size_t targetID : branchTargets)
                if(IOService *service = getService(targetID))
                    service->write(data);
        }

        bool start() override {
            PropertyValue name;
            for(auto& [ID, service] : IORegistry) {
                if(service->getProperty("IOClass", name) && holds_alternative<string>(name) && get<string>(name) == "IOFramebuffer") {
                    addTarget(ID);
                }
            }
            write("Console started\n");
            return true;
        }
    };
}

int main() {
    auto& console = IOKit::addService<IOKit::Services::IOConsole>();
    auto& fb0     = IOKit::addService<IOKit::Services::IOFramebuffer>();
    auto& serial  = IOKit::addService<IOKit::Services::IOSerial>();
    IOKit::matchAndStartDevices();

    auto& terminal = BSD::TTY::createTerminal(100);
    auto serialID = IOKit::getBSDDeviceID(&serial);
    terminal.attachDevice(serialID);
    serial.onDataReceived.push_back([&](const string& data) { terminal.pushInput(data); });
    serial.onDataSent.push_back([&](const string& data) { console.write("[serial TX] "+data); });

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

    console.write("Single-user mode started\n");
    string inputLine;

    while(true) {
        serial.write("shell> ");
        getline(cin, inputLine);
        if(inputLine == "exit") {
            break;
        } else
        if(inputLine == "help") {
            serial.pushFromHardware("help, exit, iotree, ls\n");
        } else
        if(inputLine == "iotree") {
            IOKit::dumpRegistry(IOKit::rootService);
            continue;
        } else
        if(inputLine.starts_with("ls ") && inputLine.size() > 3) {
            string path = inputLine.substr(3);
            serial.pushFromHardware("Listing: "+path+"\n");
            for(auto& dirent : BSD::VFS::readdir(path)) {
                serial.pushFromHardware(dirent.name+"\n");
            }
        } else {
            serial.pushFromHardware(inputLine+"\n");
        }
        string output = terminal.read();
        serial.write(output);
    }

    return 0;
}
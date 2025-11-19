#pragma once

#include "Std.cpp"
#include "BSD/IO.cpp"

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

        bool provides(IOService* provider) {
            for(IOService* p = provider; p != nullptr; p = p->provider) {
                if(p == this)
                    return true;
            }

            return false;
        }

        bool matches(IOService* provider) {
            if(provides(provider)) {
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

            if(provides(toProvider)) {
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
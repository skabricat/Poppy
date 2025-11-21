#pragma once

#include "Std.cpp"
#include "IO.cpp"

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
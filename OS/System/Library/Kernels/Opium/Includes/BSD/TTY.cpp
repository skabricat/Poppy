#pragma once

#include "Std.cpp"
#include "BSD/IO.cpp"

namespace BSD::TTY {
    struct Terminal {
        size_t ID = 0;
        size_t sessionID = 0;
        size_t foregroundProcessGroupID = 0;
        vector<char> inputBuffer;
        vector<char> outputBuffer;
        BSD::IO::DeviceID deviceID = 0;

        void attachDevice(BSD::IO::DeviceID dev) { deviceID = dev; }

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
            if (BSD::IO::characterDeviceSwitches.contains(majorID))
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
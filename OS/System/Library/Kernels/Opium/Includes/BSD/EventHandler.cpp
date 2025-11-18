#pragma once

#include "Std.cpp"

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
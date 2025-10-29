#pragma once

#include "Std.cpp"

namespace KEM {
    struct TTY {
        usize ID = 0;                // Условный идентификатор устройства
        usize sessionID = 0;         // Сессия, которой принадлежит терминал
        usize foregroundPgrpID = 0;  // Foreground process group
        vector<char> inputBuffer;    // данные, пришедшие в tty (read by process)
        vector<char> outputBuffer;   // данные, которые процесс записал в tty (write -> device)
        vector<function<void(const string&)>> onOutput; // ← подписчики на вывод

        void pushInput(const string &s) {
            inputBuffer.insert(inputBuffer.end(), s.begin(), s.end());
        }

        void pushOutput(const string &s) {
            outputBuffer.insert(outputBuffer.end(), s.begin(), s.end());
        }

        void write(const string& s) {  // когда процесс пишет в tty
            outputBuffer.insert(outputBuffer.end(), s.begin(), s.end());
            // уведомляем всех подписчиков
            for (auto& cb : onOutput) cb(s);
        }

        string read() { // процесс читает
            string s(inputBuffer.begin(), inputBuffer.end());
            inputBuffer.clear();
            return s;
        }

        string drainOutput() {
            string s(outputBuffer.begin(), outputBuffer.end());
            outputBuffer.clear();
            return s;
        }
    };

    struct Session {
        usize ID = 0;                // SID (обычно PID лидера)
        usize leaderID = 0;          // Процесс-лидер сессии
        usize ttyID = 0;             // Контролирующий TTY (если есть)
        string loginName;            // setlogin() — строка имени пользователя
    };

    struct ProcessGroup {
        usize ID = 0;                // PGID
        usize sessionID = 0;         // Сессия, к которой принадлежит группа
        unordered_set<usize> memberIDs; // PID всех процессов в группе
    };

    struct Process {
        usize ID = 0;                // PID
        usize parentID = 0;          // PPID
        usize userID = 0;            // UID
        usize groupID = 0;           // GID
        usize pgrpID = 0;            // Группа процессов
        usize sessionID = 0;         // Сессия
        usize ttyID = 0;             // Контролирующий терминал (если есть)

        vector<string> command;                         // argv[]
        unordered_map<string, string> environment;      // окружение (envp)
    };

    unordered_map<usize, Process> processes;
    unordered_map<usize, ProcessGroup> processGroups;
    unordered_map<usize, Session> sessions;
    unordered_map<usize, unique_ptr<TTY>> ttys;

    Process& createProcess(usize pid, usize ppid, usize uid, usize gid) {
        Process p;
        p.ID = pid;
        p.parentID = ppid;
        p.userID = uid;
        p.groupID = gid;
        return processes[pid] = move(p);
    }

    ProcessGroup& createProcessGroup(usize pgid, usize sid) {
        ProcessGroup g;
        g.ID = pgid;
        g.sessionID = sid;
        return processGroups[pgid] = move(g);
    }

    Session& createSession(usize sid, usize leaderID) {
        Session s;
        s.ID = sid;
        s.leaderID = leaderID;
        return sessions[sid] = move(s);
    }

    TTY &createTTY(usize id) {
        auto p = make_unique<TTY>();
        p->ID = id;
        auto &ref = *p;
        ttys[id] = move(p);
        return ref;
    }

    TTY *getTTY(usize id) {
        auto it = ttys.find(id);
        return it != ttys.end() ? it->second.get() : nullptr;
    }

    void attachProcessToGroup(usize pid, usize pgid) {
        auto& proc = processes[pid];
        auto& group = processGroups[pgid];
        proc.pgrpID = pgid;
        proc.sessionID = group.sessionID;
        group.memberIDs.insert(pid);
    }

    void setSessionTTY(usize sid, usize ttyid) {
        auto& sess = sessions[sid];
        auto& tty = ttys[ttyid];
        sess.ttyID = ttyid;
        getTTY(ttyid)->sessionID = sid;
    }
}
#pragma once

#include "Std.cpp"

namespace BSD::Scheduling {
    struct Session { size_t ID = 0; size_t leaderID = 0; size_t ttyID = 0; string loginName; };
    struct ProcessGroup { size_t ID = 0; size_t sessionID = 0; unordered_set<size_t> memberIDs; };
    struct Process {
        size_t ID = 0; size_t parentID = 0; size_t userID = 0; size_t groupID = 0;
        size_t pgrpID = 0; size_t sessionID = 0; size_t ttyID = 0;
        vector<string> command; unordered_map<string, string> environment;
    };

    unordered_map<size_t, Process> processes;
    unordered_map<size_t, ProcessGroup> processGroups;
    unordered_map<size_t, Session> sessions;

    Process& createProcess(size_t pid, size_t ppid, size_t uid, size_t gid) {
        Process p; p.ID = pid; p.parentID = ppid; p.userID = uid; p.groupID = gid; return processes[pid] = move(p); }

    ProcessGroup& createProcessGroup(size_t pgid, size_t sid) {
        ProcessGroup g; g.ID = pgid; g.sessionID = sid; return processGroups[pgid] = move(g); }

    Session& createSession(size_t sid, size_t leaderID) {
        Session s; s.ID = sid; s.leaderID = leaderID; return sessions[sid] = move(s); }
}
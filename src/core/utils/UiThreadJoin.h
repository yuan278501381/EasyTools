#pragma once

#include <windows.h>

#include <thread>

namespace easy::core {

// A worker that calls HWND APIs can be waiting on a synchronous message owned
// by the UI thread. A plain join() from that UI thread then deadlocks both
// sides. Dispatch sent (not queued) messages until the worker exits, without
// reopening ordinary application input during teardown.
inline void joinWorkerWhilePumpingSentMessages(std::jthread& worker) {
    if (!worker.joinable()) return;

    HANDLE threadHandle = worker.native_handle();
    while (threadHandle) {
        const DWORD waitResult = MsgWaitForMultipleObjectsEx(
            1, &threadHandle, INFINITE, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
        if (waitResult == WAIT_OBJECT_0) break;
        if (waitResult == WAIT_OBJECT_0 + 1) {
            MSG ignored{};
            // PeekMessage dispatches pending nonqueued sent messages before it
            // examines the queue. PM_NOREMOVE leaves queued user input alone.
            PeekMessageW(&ignored, nullptr, 0, 0, PM_NOREMOVE);
            continue;
        }
        break;
    }
    worker.join();
}

}  // namespace easy::core

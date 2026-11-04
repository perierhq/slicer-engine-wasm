// Serial oneTBB shim: tbb::task_group.
// run() executes the task at once on the calling thread. As in TBB, an exception thrown by a task
// is kept and rethrown by wait(), and the group is cancelled: later tasks are skipped.
#pragma once

#include <exception>
#include <utility>

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

enum task_group_status {
    not_complete,
    complete,
    canceled,
};

class task_group {
public:
    task_group() = default;
    task_group(const task_group &) = delete;
    task_group &operator=(const task_group &) = delete;

    template<typename F> void run(F &&f)
    {
        if (m_canceled)
            return;
        try {
            std::forward<F>(f)();
        } catch (...) {
            if (!m_exception)
                m_exception = std::current_exception();
            m_canceled = true;
        }
    }

    template<typename F> task_group_status run_and_wait(F &&f)
    {
        run(std::forward<F>(f));
        return wait();
    }

    task_group_status wait()
    {
        const bool was_canceled = m_canceled;
        m_canceled              = false;
        if (m_exception) {
            std::exception_ptr e = std::move(m_exception);
            m_exception          = nullptr;
            std::rethrow_exception(e);
        }
        return was_canceled ? canceled : complete;
    }

    void cancel() { m_canceled = true; }
    bool is_canceling() const { return m_canceled; }

private:
    bool               m_canceled = false;
    std::exception_ptr m_exception;
};

} // namespace tbb
} // namespace oneapi

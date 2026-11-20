#pragma once

// The process step is task's (process.hh): run_daemon, ProcessLock and locked_pid. Fork before
// constructing any threaded component; startup errors reach the waiting parent through a pipe.
#include <process.hh>

namespace detent_cli {

using task::locked_pid;
using task::ProcessLock;
using task::run_daemon;
using Ready = task::daemon_ready;

} // namespace detent_cli

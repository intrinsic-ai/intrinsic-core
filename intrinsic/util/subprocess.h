// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INTRINSIC_UTIL_SUBPROCESS_H_
#define INTRINSIC_UTIL_SUBPROCESS_H_

#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"

namespace intrinsic {

// Runs a subprocess in a thread-safe way.
//
// By default, the subprocess inherits the parent's environment variables and
// file descriptors.
// Destruction does not stop the subprocess. To stop the subprocess, use
// `Kill` and `Wait`.
class Subprocess {
 public:
  enum { kNoExitCode = -256 };

  Subprocess() = default;

  // Note, binary_path will be added to the front of the args.
  Subprocess(absl::string_view binary_path, absl::Span<const std::string> args)
      : binary_path_(binary_path), args_(args.begin(), args.end()) {}

  ~Subprocess() = default;

  // Set the binary and arguments to run.
  // Ignored if binary_path or args have already been set (by calling the
  // constructor or SetProgram).
  // Has no effect if the subprocess has already been started.
  void SetProgram(absl::string_view binary_path,
                  absl::Span<const std::string> args);

  // Sets a value of an environment variable.
  // If the variable is already set, it will be overwritten.
  // Has no effect if the subprocess has already been started.
  absl::Status SetEnv(absl::string_view name, std::string value);

  // If the parent process terminates for any reason, the subprocess will
  // receive the given `signal`.
  // Has no effect if the subprocess has already been started.
  void SetParentDeathSignal(int signal);

  // Set the file descriptor for the subprocess's stdin, stdout, or stderr.
  // Has no effect if the subprocess has already been started.
  void SetStdinFd(int fd);
  void SetStdoutFd(int fd);
  void SetStderrFd(int fd);

  // Begins running the subprocess. Note that this function is only best effort
  // and will not fail if the child process fails to start. In this case, you
  // should be able to errors in the logs.
  absl::Status Start();

  // Sends a signal to the subprocess.
  // Does not wait.
  absl::Status Kill(int signal);

  // Blocks and waits until the process is finished.
  //
  // Returns an FailedPreconditionError if the subprocess has not been started,
  // or as already been Wait'ed for, or an InternalError if the waitpid syscall
  // failed.
  // Also, updates the exit code if the subprocess has finished.
  absl::Status Wait();

  // Returns true if the subprocess is still running.
  // Also, updates the exit code if the subprocess has finished.
  bool CheckRunning();

  // Returns the exit code, or kNoExitCode if the subprocess is running or was
  // killed. The value is cached. To get the latest value, call CheckRunning()
  // or Wait() first.
  int exit_code() const;

 private:
  // Checks if the subprocess has finished.
  // If so, sets the exit_code.
  bool CheckIfFinished() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  mutable absl::Mutex mutex_;
  std::string binary_path_ ABSL_GUARDED_BY(mutex_);
  std::vector<std::string> args_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, std::string> env_vars_
      ABSL_GUARDED_BY(mutex_);
  // Default to an invalid id. So we can tell if a process has been started.
  pid_t child_pid_ ABSL_GUARDED_BY(mutex_) = 0;
  bool finished_ ABSL_GUARDED_BY(mutex_) = false;
  int parent_death_signal_ ABSL_GUARDED_BY(mutex_) = 0;
  bool exited_ ABSL_GUARDED_BY(mutex_) = false;
  int exit_code_ ABSL_GUARDED_BY(mutex_) = kNoExitCode;
  int stdin_fd_ ABSL_GUARDED_BY(mutex_) = -1;
  int stdout_fd_ ABSL_GUARDED_BY(mutex_) = -1;
  int stderr_fd_ ABSL_GUARDED_BY(mutex_) = -1;
};

// Returns the environment variables of the current process.
// Usually, this is not needed because Subprocess::SetEnv() can overwrite
// an existing environment variable.
absl::flat_hash_map<std::string, std::string>
GetThisProcessEnvironmentVariables();

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_SUBPROCESS_H_

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

#include "intrinsic/util/subprocess.h"

#include <errno.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"

namespace intrinsic {

absl::flat_hash_map<std::string, std::string>
GetThisProcessEnvironmentVariables() {
  absl::flat_hash_map<std::string, std::string> env;
  for (char** e = environ; *e != nullptr; e++) {
    std::vector<std::string> p = absl::StrSplit(*e, absl::MaxSplits('=', 1));
    if (p.empty()) continue;
    if (p.size() == 1) {
      env[p[0]] = "";
      continue;
    }
    env[p[0]] = p[1];
  }
  return env;
}

void Subprocess::SetProgram(absl::string_view binary_path,
                            absl::Span<const std::string> args) {
  absl::MutexLock l(mutex_);
  if (!binary_path_.empty() || !args_.empty()) {
    LOG(WARNING) << "SetProgram called multiple times; ignored.";
    return;
  }
  binary_path_ = binary_path;
  args_ = std::vector<std::string>(args.begin(), args.end());
}

absl::Status Subprocess::SetEnv(absl::string_view name, std::string value) {
  absl::MutexLock l(mutex_);
  if (name.empty() || absl::StrContains(name, '=')) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid variable name: '", name, "'"));
  }
  env_vars_[name] = std::move(value);
  return absl::OkStatus();
}

void Subprocess::SetParentDeathSignal(int signal) {
  absl::MutexLock l(mutex_);
  parent_death_signal_ = signal;
}

absl::Status Subprocess::Start() {
  absl::MutexLock l(mutex_);
  if (child_pid_ > 0) {
    return absl::FailedPreconditionError("Subprocess::Start already called");
  }
  // Construct the final args to be passed to the child's exec call.
  //
  // DO NOT move this code into the child. While functionally this is fine, the
  // code might run in a multi-threaded environment in which case any code that
  // might block may cause deadlock in the forked child. While it appears this
  // code will not block, it's possible that it may require memory allocations
  // and other operations that could block, especially in the context of running
  // a test with heap-check, etc.
  std::vector<const char*> combined_args;
  combined_args.reserve(args_.size() + 2);
  // It is NOT an error that binary_path is added to the args. It is both the
  // target binary to run, and the first of the commandline arguments.
  combined_args.push_back(binary_path_.c_str());
  for (const auto& arg : args_) {
    combined_args.push_back(arg.c_str());
  }
  combined_args.push_back(nullptr);

  // Override environment variables.
  absl::flat_hash_map<std::string, std::string> env =
      GetThisProcessEnvironmentVariables();
  for (const auto& [name, value] : env_vars_) {
    env[name] = value;
  }
  std::vector<std::string> env_strings;
  env_strings.reserve(env.size());
  for (const auto& [name, value] : env) {
    env_strings.push_back(absl::StrCat(name, "=", value));
  }
  std::vector<const char*> combined_env;
  combined_env.reserve(env.size() + 1);
  for (auto& env_string : env_strings) {
    combined_env.push_back(env_string.c_str());
  }
  combined_env.push_back(nullptr);

  child_pid_ = fork();
  if (child_pid_ == 0) {
    // Be careful when adding any code in between the `fork` and `execv`.
    // Verify that the code will NOT block. This could be as subtle as a lock
    // around memory allocation in a threaded environment.
    finished_ = false;
    if (stdin_fd_ >= 0 && stdin_fd_ != fileno(stdin)) {
      if (dup2(stdin_fd_, fileno(stdin)) == -1) {
        return absl::InternalError(
            absl::StrCat("Could not replace stdin: ", strerror(errno)));
      }
    }
    if (stdout_fd_ >= 0 && stdout_fd_ != fileno(stdout)) {
      if (dup2(stdout_fd_, fileno(stdout)) == -1) {
        return absl::InternalError(
            absl::StrCat("Could not replace stdout: ", strerror(errno)));
      }
    }
    if (stderr_fd_ >= 0 && stderr_fd_ != fileno(stderr)) {
      if (dup2(stderr_fd_, fileno(stderr)) == -1) {
        return absl::InternalError(
            absl::StrCat("Could not replace stderr: ", strerror(errno)));
      }
    }
    if (parent_death_signal_ > 0) {
      if (prctl(PR_SET_PDEATHSIG, parent_death_signal_) != 0) {
        return absl::InternalError(
            absl::StrCat("Setting death signal failed: ", strerror(errno)));
      }
    }
    execve(binary_path_.c_str(), const_cast<char**>(combined_args.data()),
           const_cast<char**>(combined_env.data()));
    // If you get down here, it is an error.
    int errval = errno;
    // It's ok to LOG(FATAL) here since this is the child process.
    LOG(FATAL) << "Exec failed: " << errval << ": " << strerror(errval);
  }

  // Parent code only now.
  if (child_pid_ == -1) {
    return absl::InternalError(
        absl::StrCat("Forking failed: ", strerror(errno)));
  }

  return absl::OkStatus();
}

absl::Status Subprocess::Wait() {
  int child_pid;
  {
    absl::MutexLock l(mutex_);
    child_pid = child_pid_;
  }
  if (child_pid == 0) {
    return absl::FailedPreconditionError("Subprocess not started");
  }
  int status;
  if (waitpid(child_pid, &status, 0) <= 0) {
    return absl::InternalError(absl::StrCat(
        "Failed to terminate underlying subprocess: ", strerror(errno)));
  }
  absl::MutexLock l(mutex_);
  finished_ = true;
  exited_ = WIFEXITED(status);
  if (exited_) exit_code_ = WEXITSTATUS(status);

  // Reset the child pid since the process terminated.
  child_pid_ = 0;

  return absl::OkStatus();
}

bool Subprocess::CheckIfFinished() {
  if (finished_) return true;
  if (child_pid_ <= 0) {
    LOG_EVERY_N_SEC(ERROR, 1) << "CheckIfFinished called without child_pid_";
  }
  int status;
  const int wait_result = waitpid(child_pid_, &status, WNOHANG);
  if ((wait_result > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) ||
      (wait_result < 0 && errno == ECHILD)) {
    child_pid_ = -1;
    finished_ = true;
    exited_ = WIFEXITED(status);
    if (exited_) exit_code_ = WEXITSTATUS(status);
  }
  return finished_;
}

absl::Status Subprocess::Kill(int signal) {
  absl::MutexLock l(mutex_);
  if (child_pid_ <= 0 || CheckIfFinished())
    return absl::FailedPreconditionError("The process is not running");
  if (kill(child_pid_, signal) != 0) {
    int errval = errno;
    return absl::InternalError(
        absl::StrCat("Failed to send signal: ", strerror(errval)));
  }
  return absl::OkStatus();
}

bool Subprocess::CheckRunning() {
  absl::MutexLock l(mutex_);
  if (child_pid_ <= 0) return false;
  bool finished = CheckIfFinished();
  return !finished;
}

void Subprocess::SetStdinFd(int fd) {
  absl::MutexLock l(mutex_);
  stdin_fd_ = fd;
}

void Subprocess::SetStdoutFd(int fd) {
  absl::MutexLock l(mutex_);
  stdout_fd_ = fd;
}

void Subprocess::SetStderrFd(int fd) {
  absl::MutexLock l(mutex_);
  stderr_fd_ = fd;
}

int Subprocess::exit_code() const {
  absl::MutexLock l(mutex_);
  return exit_code_;
}

}  // namespace intrinsic

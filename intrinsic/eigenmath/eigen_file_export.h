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

#ifndef INTRINSIC_EIGENMATH_EIGEN_FILE_EXPORT_H_
#define INTRINSIC_EIGENMATH_EIGEN_FILE_EXPORT_H_

#include <cstddef>
#include <fstream>
#include <iosfwd>
#include <string>
#include <vector>

#include "Eigen/Core"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"

namespace intrinsic::eigenmath {

struct EigenFileExport {
  // Create CSV formatting option, precision can be customized if desired.
  static Eigen::IOFormat CSVFormat(int precision = Eigen::StreamPrecision) {
    return Eigen::IOFormat(precision, Eigen::DontAlignCols, ", ", "\n");
  }

  // Create Octave formatting option, precision can be customized if desired.
  static Eigen::IOFormat OctaveFormat(int precision = Eigen::StreamPrecision) {
    return Eigen::IOFormat(precision, 0, ", ", ";\n", "", "", "[", "]");
  }
};

// Saves a single matrix to a single text file.
//
// Example:
//
//    MatrixToFile(EigenFileExport::CSVFormat(),"/tmp/matrix.csv", SomeMatrix);
inline void MatrixToFile(const Eigen::IOFormat& fmt, absl::string_view filename,
                         const Eigen::Ref<Eigen::MatrixXd>& m,
                         const std::string& heading = "") {
  std::ofstream file((std::string(filename)));
  if (!heading.empty()) file << heading << "\n";
  file << m.format(fmt);

  file.close();
}

// Saves a std::vector of matrices to a single text file. `transpose_eigen_type`
// can be used to store the transpose of a vector instead of a column vector,
// which may make the CSV file easier to read.
//
// Example:
//
//    VectorToFile(EigenFileExport::CSVFormat(),"/tmp/vector.csv", SomeVector);
template <typename M, typename Alloc = Eigen::aligned_allocator<M>>
inline void VectorToFile(const Eigen::IOFormat& fmt, absl::string_view filename,
                         const std::vector<M, Alloc>& m_vec,
                         bool transpose_eigen_type = false,
                         const std::string& heading = "") {
  std::ofstream file((std::string(filename)));

  if (!heading.empty()) file << heading << "\n";

  for (size_t i = 0; i < m_vec.size(); i++) {
    if (transpose_eigen_type) {
      file << m_vec[i].transpose().format(fmt) << "\n";
    } else {
      file << m_vec[i].format(fmt) << "\n";
    }
  }

  file.close();
}

// Reads from a previously exported text file and stores the content of each
// line into an eigenmath::VectorX<>. The flag `remove_heading` can be used to
// ignore the first line of the file (default false). If the file contains a
// heading and `remove_heading` is set to false, a FailedPreconditionedError is
// returned.
template <typename Scalar>
inline absl::StatusOr<std::vector<eigenmath::VectorX<Scalar>>> VectorsFromFile(
    absl::string_view filename, bool remove_heading = false) {
  std::ifstream file((std::string(filename)));
  if (file.fail()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Failed to open file ", filename));
  }

  std::string line;
  if (remove_heading) {
    std::getline(file, line);
  }

  std::vector<eigenmath::VectorX<Scalar>> v_vec;

  while (std::getline(file, line)) {
    const std::vector<std::string> line_values =
        // absl::StrSplit with a list of delimiters enabling reading from both
        // .csv and .mat files.
        absl::StrSplit(line, absl::ByAnyChar("[],;\t "), absl::SkipEmpty());

    eigenmath::VectorX<Scalar> v(line_values.size());
    for (int i = 0; i < v.size(); ++i) {
      double line_value;
      if (!absl::SimpleAtod(line_values[i], &line_value)) {
        return absl::FailedPreconditionError(
            absl::StrCat("Found non-numeric characters \"", line_values[i],
                         "\" while reading the file ", filename, "."));
      }
      v[i] = static_cast<Scalar>(line_value);
    }
    v_vec.push_back(v);
  }

  return v_vec;
}

// Reads a list of equally sized matrices from a previously exported text file
// with CSV format. `row_size` expresses the number of rows of the matrices in
// the file, and it is used to split the file content. Thus, every `row_size`
// lines of the file are assumed to form a matrix. The flag `remove_heading` can
// be used to ignore the first line of the file (default false). If the file
// contains a heading and `remove_heading` is set to false, a
// FailedPreconditionedError is returned.
template <typename Scalar>
inline absl::StatusOr<std::vector<eigenmath::MatrixX<Scalar>>>
MatricesFromCsvFile(absl::string_view filename, int row_size,
                    bool remove_heading = false) {
  std::ifstream file((std::string(filename)));
  if (file.fail()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Failed to open file ", filename));
  }

  if (row_size <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Row size must be positive. Got ", row_size, "."));
  }

  std::string line;
  std::streampos start_line = file.tellg();
  if (remove_heading) {
    std::getline(file, line);
    start_line = file.tellg();
  }

  // Infer the number of columns from the start line. Then go back to the
  // beginning of the line.
  std::getline(file, line);
  const std::vector<std::string> line_values =
      absl::StrSplit(line, absl::ByAnyChar(", "), absl::SkipEmpty());
  const int num_cols = line_values.size();
  file.clear();
  file.seekg(start_line);

  std::vector<eigenmath::MatrixX<Scalar>> m_vec;
  eigenmath::MatrixX<Scalar> matrix(row_size, num_cols);
  int line_count = 0;

  while (std::getline(file, line)) {
    ++line_count;
    const std::vector<std::string> line_values =
        absl::StrSplit(line, absl::ByAnyChar(", "), absl::SkipEmpty());

    if (line_values.size() != num_cols) {
      const int file_line = remove_heading ? line_count + 1 : line_count;
      return absl::FailedPreconditionError(
          absl::StrCat("Size mismatch at line ", file_line, ". Expected ",
                       num_cols, " values, got ", line_values.size(), "."));
    }

    const int row_idx = (line_count - 1) % row_size;
    for (int i = 0; i < num_cols; ++i) {
      double line_value;
      if (!absl::SimpleAtod(line_values[i], &line_value)) {
        const int file_line = remove_heading ? line_count + 1 : line_count;
        return absl::FailedPreconditionError(absl::StrCat(
            "Found non-numeric characters \"", line_values[i], "\" at line ",
            file_line, " while reading the file ", filename, "."));
      }
      matrix(row_idx, i) = static_cast<Scalar>(line_value);
    }
    if (row_idx == row_size - 1) {
      m_vec.push_back(matrix);
    }
  }

  if (line_count % row_size != 0) {
    return absl::FailedPreconditionError(
        absl::StrCat("Expected a number of lines that is an integer multiple "
                     "of the given row size. Got row size ",
                     row_size, " and ", line_count, " lines."));
  }

  return m_vec;
}

// Reads a list of matrices from a previously exported textfile with
// Octave(.mat) format. Possible heading lines are automatically ignored.
template <typename Scalar>
inline absl::StatusOr<std::vector<eigenmath::MatrixX<Scalar>>>
MatricesFromMatFile(absl::string_view filename, bool remove_heading = false) {
  std::ifstream file((std::string(filename)));
  if (file.fail()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Failed to open file ", filename));
  }

  std::string line;
  int line_count = 0;
  std::vector<eigenmath::MatrixX<Scalar>> m_vec;
  std::streampos start_of_matrix_line = file.tellg();

  while (std::getline(file, line)) {
    if (line.front() == '[') {
      // A matrix starts at the current line. Identify the number of rows and
      // columns.
      int num_rows = 1;
      const std::vector<std::string> line_values =
          absl::StrSplit(line, absl::ByAnyChar("[],; "), absl::SkipEmpty());
      int num_cols = line_values.size();

      while (line.back() != ']') {
        if (std::getline(file, line)) ++num_rows;
      }

      eigenmath::MatrixX<Scalar> matrix(num_rows, num_cols);

      // Go back to the line where the matrix starts.
      file.clear();
      file.seekg(start_of_matrix_line);

      for (int i = 0; i < num_rows; ++i) {
        std::getline(file, line);
        ++line_count;
        const std::vector<std::string> line_values =
            absl::StrSplit(line, absl::ByAnyChar("[],; "), absl::SkipEmpty());

        if (line_values.size() != num_cols) {
          const int file_line = remove_heading ? line_count + 1 : line_count;
          return absl::FailedPreconditionError(
              absl::StrCat("Size mismatch at line ", file_line, ". Expected ",
                           num_cols, " values, got ", line_values.size(), "."));
        }

        for (int j = 0; j < num_cols; ++j) {
          double line_value;
          if (!absl::SimpleAtod(line_values[j], &line_value)) {
            const int file_line = remove_heading ? line_count + 1 : line_count;
            return absl::FailedPreconditionError(
                absl::StrCat("Found non-numeric characters \"", line_values[i],
                             "\" at line ", file_line,
                             " while reading the file ", filename, "."));
          }
          matrix(i, j) = static_cast<Scalar>(line_value);
        }
      }
      m_vec.push_back(matrix);
    }
    start_of_matrix_line = file.tellg();
  }

  return m_vec;
}

}  // namespace intrinsic::eigenmath

#endif  // INTRINSIC_EIGENMATH_EIGEN_FILE_EXPORT_H_

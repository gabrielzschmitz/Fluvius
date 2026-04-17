// engine/logger.h
#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace logger {

// -------------------- CONFIG --------------------

enum class Level { Debug = 0, Info, Warn, Error };

// -------------------- DEFAULT LEVEL --------------------

constexpr Level defaultLevel() {
#ifdef LOG_LEVEL_DEBUG
  return Level::Debug;
#elif defined(LOG_LEVEL_INFO)
  return Level::Info;
#elif defined(LOG_LEVEL_WARN)
  return Level::Warn;
#elif defined(LOG_LEVEL_ERROR)
  return Level::Error;
#else
  return Level::Info;
#endif
}

inline Level CURRENT_LEVEL = defaultLevel();
inline std::mutex log_mutex;

// -------------------- LEVEL CONTROL --------------------
inline void setLevel(Level lvl) { CURRENT_LEVEL = lvl; }

inline Level getLevel() { return CURRENT_LEVEL; }

inline Level fromString(const std::string& s) {
  if (s == "debug") return Level::Debug;
  if (s == "info") return Level::Info;
  if (s == "warn") return Level::Warn;
  if (s == "error") return Level::Error;
  return Level::Info;
}

// -------------------- COLORS --------------------

namespace color {
inline const std::string RESET = "\033[0m";
inline const std::string RED_TXT = "\033[31m";
inline const std::string GREEN_TXT = "\033[32m";
inline const std::string YELLOW_TXT = "\033[33m";
inline const std::string WHITE_TXT = "\033[37m";
}  // namespace color

// -------------------- UTILS --------------------

inline std::string now() {
  using namespace std::chrono;

  auto tp = system_clock::now();
  auto s = time_point_cast<std::chrono::seconds>(tp);
  auto ms = duration_cast<milliseconds>(tp - s).count();

  std::time_t tt = system_clock::to_time_t(s);
  std::tm tm = *std::localtime(&tt);

  std::stringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0')
     << std::setw(3) << ms;

  return ss.str();
}

inline std::string level_to_string(Level lvl) {
  switch (lvl) {
    case Level::Debug:
      return "DEBUG";
    case Level::Info:
      return "INFO";
    case Level::Warn:
      return "WARN";
    case Level::Error:
      return "ERROR";
  }
  return "UNK";
}

inline std::string level_color(Level lvl) {
  switch (lvl) {
    case Level::Debug:
      return color::GREEN_TXT;
    case Level::Info:
      return color::WHITE_TXT;
    case Level::Warn:
      return color::YELLOW_TXT;
    case Level::Error:
      return color::RED_TXT;
  }
  return color::WHITE_TXT;
}

inline std::string repeat(const std::string& s, size_t n) {
  std::string out;
  for (size_t i = 0; i < n; ++i) out += s;
  return out;
}

inline void print_prefix(Level lvl) {
  std::cout << level_color(lvl) << "\033[1m"
            << "[" << now() << "] "
            << "[" << level_to_string(lvl) << "] "
            << "[T:" << std::this_thread::get_id() << "] " << color::RESET;
}

// -------------------- FORMATTER --------------------

namespace detail {

inline void format_impl(std::stringstream& ss, const std::string& fmt,
                        size_t pos) {
  ss << fmt.substr(pos);
}

template <typename T, typename... Args>
void format_impl(std::stringstream& ss, const std::string& fmt, size_t pos,
                 T&& value, Args&&... args) {
  size_t open = fmt.find("{}", pos);

  if (open == std::string::npos) {
    ss << fmt.substr(pos);
    return;
  }

  ss << fmt.substr(pos, open - pos);
  ss << std::forward<T>(value);

  format_impl(ss, fmt, open + 2, std::forward<Args>(args)...);
}

template <typename... Args>
std::string format(const std::string& fmt, Args&&... args) {
  std::stringstream ss;
  format_impl(ss, fmt, 0, std::forward<Args>(args)...);
  return ss.str();
}

}  // namespace detail

// -------------------- CORE LOGGER --------------------

template <typename... Args>
void log_stream(Level lvl, Args&&... args) {
  if (lvl < CURRENT_LEVEL) return;

  std::lock_guard<std::mutex> lock(log_mutex);

  std::stringstream ss;
  (ss << ... << args);

  print_prefix(lvl);
  std::cout << ss.str() << color::RESET << "\n";
}

template <typename... Args>
void log(Level lvl, const std::string& fmt, Args&&... args) {
  if (lvl < CURRENT_LEVEL) return;

  std::lock_guard<std::mutex> lock(log_mutex);

  print_prefix(lvl);
  std::cout << detail::format(fmt, std::forward<Args>(args)...) << color::RESET
            << "\n";
}

template <typename T, typename... Args>
void log(Level lvl, T&& first, Args&&... rest) {
  log_stream(lvl, std::forward<T>(first), std::forward<Args>(rest)...);
}

// -------------------- BOX LOGGER --------------------

inline void log_box(Level lvl, const std::vector<std::string>& lines) {
  if (lvl < CURRENT_LEVEL) return;

  std::lock_guard<std::mutex> lock(log_mutex);

  size_t width = 0;
  for (const auto& line : lines) width = std::max(width, line.size());

  print_prefix(lvl);
  std::cout << "\n";

  std::cout << "┌" << repeat("─", width + 2) << "┐\n";

  for (const auto& line : lines) {
    std::cout << "│ " << line << std::string(width - line.size(), ' ')
              << " │\n";
  }

  std::cout << "└" << repeat("─", width + 2) << "┘" << "\n";
}

// -------------------- TABLE LOGGER --------------------

inline void log_table(Level lvl, const std::string& title,
                      const std::vector<std::string>& headers,
                      const std::vector<std::vector<std::string>>& rows) {
  if (lvl < CURRENT_LEVEL) return;

  std::lock_guard<std::mutex> lock(log_mutex);

  size_t n_cols = headers.size();
  std::vector<size_t> col_widths(n_cols);

  for (size_t c = 0; c < n_cols; ++c) {
    col_widths[c] = headers[c].size();
    for (const auto& row : rows)
      if (c < row.size())
        col_widths[c] = std::max(col_widths[c], row[c].size());
  }

  // exact table width
  size_t inner_width = 0;
  for (size_t c = 0; c < n_cols; ++c) inner_width += col_widths[c] + 3;

  inner_width -= 1;

  print_prefix(lvl);
  std::cout << "\n";

  auto print_row = [&](const std::vector<std::string>& row) {
    std::cout << "│";
    for (size_t c = 0; c < n_cols; ++c) {
      std::string cell = (c < row.size()) ? row[c] : "";
      std::cout << " " << cell << std::string(col_widths[c] - cell.size(), ' ')
                << " │";
    }
    std::cout << "\n";
  };

  // top border
  std::cout << "┌" << repeat("─", inner_width) << "┐\n";

  // title centered INSIDE SAME WIDTH
  size_t pad_left = (inner_width - title.size()) / 2;
  size_t pad_right = inner_width - title.size() - pad_left;

  std::cout << "│" << std::string(pad_left, ' ') << title
            << std::string(pad_right, ' ') << "│\n";

  // separator after title
  std::cout << "├";
  for (size_t c = 0; c < n_cols; ++c) {
    std::cout << repeat("─", col_widths[c] + 2);
    if (c != n_cols - 1) std::cout << "┬";
  }
  std::cout << "┤\n";

  // headers
  print_row(headers);

  // header split
  std::cout << "├";
  for (size_t c = 0; c < n_cols; ++c) {
    std::cout << repeat("─", col_widths[c] + 2);
    if (c != n_cols - 1) std::cout << "┼";
  }
  std::cout << "┤\n";

  // rows
  for (const auto& row : rows) print_row(row);

  // bottom
  std::cout << "└";
  for (size_t c = 0; c < n_cols; ++c) {
    std::cout << repeat("─", col_widths[c] + 2);
    if (c != n_cols - 1) std::cout << "┴";
  }
  std::cout << "┘" << "\n";
}

// -------------------- GENERIC API --------------------

inline void box(Level lvl, const std::vector<std::string>& lines) {
  log_box(lvl, lines);
}

inline void table(Level lvl, const std::string& title,
                  const std::vector<std::string>& headers,
                  const std::vector<std::vector<std::string>>& rows) {
  log_table(lvl, title, headers, rows);
}

// -------------------- SHORTCUTS --------------------

template <typename... Args>
void debug(const std::string& fmt, Args&&... args) {
  log(Level::Debug, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void info(const std::string& fmt, Args&&... args) {
  log(Level::Info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(const std::string& fmt, Args&&... args) {
  log(Level::Warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void error(const std::string& fmt, Args&&... args) {
  log(Level::Error, fmt, std::forward<Args>(args)...);
}

}  // namespace logger

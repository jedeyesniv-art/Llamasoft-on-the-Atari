// settings.cpp - see settings.h.
#include "settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace settings {
namespace {

std::string g_path;
std::map<std::string, std::string> g_file, g_override;
std::filesystem::file_time_type g_stamp{};

const char* kDefaults = "# Defender 2000 (d2k.exe) settings. Edit while the game runs and press F5 to reload.\n";

void Parse(const std::string& text) {
  g_file.clear();
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    auto hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto trim = [](std::string s) {
      size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
      return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    g_file[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
}

}  // namespace

void SetDefaultText(const char* text) { kDefaults = text; }

void Load(const std::string& path) {
  g_path = path;
  if (!std::filesystem::exists(path)) std::ofstream(path) << kDefaults;
  Parse(kDefaults);
  auto defaults = g_file;
  Reload();
  for (auto& [k, v] : defaults) g_file.try_emplace(k, v);  // keys missing from an older file
}

bool Reload() {
  std::error_code ec;
  auto stamp = std::filesystem::last_write_time(g_path, ec);
  std::ifstream f(g_path);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  auto before = g_file;
  Parse(kDefaults);
  auto defaults = g_file;
  Parse(ss.str());
  for (auto& [k, v] : defaults) g_file.try_emplace(k, v);
  bool changed = before != g_file || stamp != g_stamp;
  g_stamp = stamp;
  return changed;
}

double Num(const std::string& key, double def) {
  std::string s = Str(key, "");
  return s.empty() ? def : std::atof(s.c_str());
}

std::string Str(const std::string& key, const std::string& def) {
  if (auto it = g_override.find(key); it != g_override.end()) return it->second;
  if (auto it = g_file.find(key); it != g_file.end()) return it->second;
  return def;
}

void Set(const std::string& key, const std::string& value) { g_override[key] = value; }

void Save(const std::string& key, const std::string& value) {
  g_override.erase(key);
  g_file[key] = value;
  std::ifstream f(g_path);
  std::vector<std::string> lines;
  std::string line;
  bool found = false;
  while (std::getline(f, line)) {
    std::string body = line.substr(0, line.find('#'));
    auto eq = body.find('=');
    if (eq != std::string::npos) {
      std::string k = body.substr(0, eq);
      k.erase(0, k.find_first_not_of(" \t"));
      k.erase(k.find_last_not_of(" \t\r") + 1);
      if (k == key) { line = key + " = " + value; found = true; }
    }
    lines.push_back(line);
  }
  f.close();
  if (!found) lines.push_back(key + " = " + value);
  std::ofstream o(g_path);
  for (auto& l : lines) o << l << "\n";
  std::error_code ec;
  g_stamp = std::filesystem::last_write_time(g_path, ec);
}

}  // namespace settings

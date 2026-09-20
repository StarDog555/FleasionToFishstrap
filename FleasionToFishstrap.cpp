#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Rule {
    std::string name;
    std::vector<unsigned long long> replaceIds;
    std::string mode;
    bool enabled = true;
    std::string localPath;
    unsigned long long withId = 0;
    bool hasWithId = false;
    std::string fishstrapPath;
};

static std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + p.string());
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

static std::string unescapeJson(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        char c = s[++i];
        switch (c) {
            case '\\': out += '\\'; break;
            case '"': out += '"'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            default: out += c; break;
        }
    }
    return out;
}

static std::string fieldString(const std::string& obj, const std::string& key) {
    std::regex r("\\\"" + key + "\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"");
    std::smatch m;
    if (!std::regex_search(obj, m, r)) return {};
    return unescapeJson(m[1].str());
}

static bool fieldBool(const std::string& obj, const std::string& key, bool fallback) {
    std::regex r("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (!std::regex_search(obj, m, r)) return fallback;
    return m[1].str() == "true";
}

static bool fieldUInt(const std::string& obj, const std::string& key, unsigned long long& out) {
    std::regex r("\\\"" + key + "\\\"\\s*:\\s*(\\d+)");
    std::smatch m;
    if (!std::regex_search(obj, m, r)) return false;
    try {
        out = std::stoull(m[1].str());
        return true;
    } catch (...) {
        return false;
    }
}

static std::vector<unsigned long long> fieldArrayUInt(const std::string& obj, const std::string& key) {
    std::vector<unsigned long long> ids;
    std::regex r("\\\"" + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch m;
    if (!std::regex_search(obj, m, r)) return ids;
    std::regex n("\\d+");
    for (auto it = std::sregex_iterator(m[1].first, m[1].second, n);
         it != std::sregex_iterator(); ++it) {
        try { ids.push_back(std::stoull(it->str())); } catch (...) {}
    }
    return ids;
}

static std::vector<std::string> objectBlocks(const std::string& json) {
    std::vector<std::string> out;
    auto pos = json.find("\"replacement_rules\"");
    if (pos == std::string::npos) return out;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return out;

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    size_t start = 0;

    for (size_t i = pos + 1; i < json.size(); ++i) {
        char c = json[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && start) {
                out.push_back(json.substr(start, i - start + 1));
                start = 0;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return out;
}

static std::vector<Rule> loadRules(const fs::path& config) {
    const auto json = readText(config);
    std::vector<Rule> rules;
    for (const auto& obj : objectBlocks(json)) {
        Rule r;
        r.name = fieldString(obj, "name");
        r.replaceIds = fieldArrayUInt(obj, "replace_ids");
        r.mode = fieldString(obj, "mode");
        r.enabled = fieldBool(obj, "enabled", true);
        r.localPath = fieldString(obj, "local_path");
        r.hasWithId = fieldUInt(obj, "with_id", r.withId);
        r.fishstrapPath = fieldString(obj, "fishstrap_path");
        rules.push_back(std::move(r));
    }
    return rules;
}

static std::string normalizeName(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string autoSkyPath(const std::string& name) {
    const auto n = normalizeName(name);
    if (n == "skybox bk") return R"(PlatformContent\pc\textures\sky\sky512_bk.tex)";
    if (n == "skybox dn") return R"(PlatformContent\pc\textures\sky\sky512_dn.tex)";
    if (n == "skybox ft") return R"(PlatformContent\pc\textures\sky\sky512_ft.tex)";
    if (n == "skybox lf") return R"(PlatformContent\pc\textures\sky\sky512_lf.tex)";
    if (n == "skybox rt") return R"(PlatformContent\pc\textures\sky\sky512_rt.tex)";
    if (n == "skybox up") return R"(PlatformContent\pc\textures\sky\sky512_up.tex)";
    return {};
}

static fs::path makeOutputPath(const fs::path& input) {
    return input.parent_path() / (input.stem().string() + "_FishstrapMod");
}

static void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write: " + p.string());
    f << text;
}

static int convert(const fs::path& config) {
    if (!fs::exists(config)) {
        std::cerr << "ERROR: JSON file does not exist: " << config << "\n";
        return 1;
    }

    const auto rules = loadRules(config);
    if (rules.empty()) {
        std::cerr << "ERROR: No replacement_rules found.\n";
        return 1;
    }

    const fs::path output = makeOutputPath(config);
    fs::create_directories(output);

    std::ofstream unsupported(output / "UNSUPPORTED_ID_RULES.txt", std::ios::binary);
    std::ofstream skipped(output / "SKIPPED_RULES.txt", std::ios::binary);

    size_t copied = 0;
    size_t unsupportedCount = 0;
    size_t skippedCount = 0;

    for (const auto& r : rules) {
        if (!r.enabled) continue;

        if (r.mode == "id") {
            ++unsupportedCount;
            unsupported << r.name << "\n";
            unsupported << "  replace_ids: ";
            for (size_t i = 0; i < r.replaceIds.size(); ++i) {
                if (i) unsupported << ", ";
                unsupported << r.replaceIds[i];
            }
            unsupported << "\n  with_id: " << (r.hasWithId ? std::to_string(r.withId) : "(missing)") << "\n";
            unsupported << "  reason: Fishstrap file mods need a local replacement file and a client file path; an asset-ID redirect cannot be represented by a Fishstrap file mod.\n\n";
            continue;
        }

        if (r.mode != "local") {
            ++skippedCount;
            skipped << r.name << ": unsupported mode '" << r.mode << "'\n";
            continue;
        }

        if (r.localPath.empty()) {
            ++skippedCount;
            skipped << r.name << ": missing local_path\n";
            continue;
        }

        fs::path source = r.localPath;
        if (!source.is_absolute()) source = config.parent_path() / source;
        if (!fs::exists(source) || !fs::is_regular_file(source)) {
            ++skippedCount;
            skipped << r.name << ": local file not found: " << source.string() << "\n";
            continue;
        }

        std::string target = r.fishstrapPath;
        if (target.empty()) target = autoSkyPath(r.name);

        if (target.empty()) {
            ++skippedCount;
            skipped << r.name << ": no fishstrap_path. Add \"fishstrap_path\" to this rule.\n";
            continue;
        }

        std::replace(target.begin(), target.end(), '/', '\\');
        fs::path dest = output / fs::path(target);
        fs::create_directories(dest.parent_path());

        // Fishstrap copies the file to the exact client path. Preserve the
        // source bytes; the source must already be in the format expected by
        // that Roblox client file (for example, .tex for sky512_*.tex).
        fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
        ++copied;

        std::cout << "COPIED: " << source << "\n"
                  << "     -> " << dest << "\n";
    }

    writeText(output / "README.txt",
        "Fishstrap mod output\r\n"
        "===================\r\n"
        "Copy the contents of this folder into Fishstrap's Modifications folder,\r\n"
        "or use the corresponding Fishstrap mod/import workflow.\r\n\r\n"
        "IMPORTANT:\r\n"
        "- Fishstrap file mods replace client files by path.\r\n"
        "- ID-only replacement rules cannot be converted automatically.\r\n"
        "- Replacement files must already be in the format expected by the target file.\r\n\r\n");

    std::cout << "\nOutput: " << output << "\n";
    std::cout << "Copied: " << copied << "\n";
    std::cout << "ID rules not convertible: " << unsupportedCount << "\n";
    std::cout << "Skipped: " << skippedCount << "\n";

    return (copied > 0 && unsupportedCount == 0 && skippedCount == 0) ? 0 : 2;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: FleasionToFishstrap.exe <replacement_rules.json>\n";
        return 1;
    }

    try {
        return convert(fs::absolute(argv[1]));
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}

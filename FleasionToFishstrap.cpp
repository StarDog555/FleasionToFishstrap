#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace fs = std::filesystem;

struct Rule {
    std::string name;
    std::vector<std::string> replaceIds;
    std::string mode;
    bool enabled = true;
    std::string localPath;
    std::string withId;
    bool hasWithId = false;
    std::string cdnUrl;
    std::string fishstrapPath;
};

struct Image {
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> bgra;
};

#pragma pack(push, 1)
struct DDS_PIXELFORMAT {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rMask;
    uint32_t gMask;
    uint32_t bMask;
    uint32_t aMask;
};

struct DDS_HEADER {
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};
#pragma pack(pop)

static_assert(sizeof(DDS_PIXELFORMAT) == 32, "DDS_PIXELFORMAT size is wrong");
static_assert(sizeof(DDS_HEADER) == 124, "DDS_HEADER size is wrong");

static std::string readText(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open JSON: " + path.string());
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

static bool readBinary(const fs::path& path, std::vector<uint8_t>& data) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    if (!data.empty()) f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return f.good() || data.empty();
}

static bool writeBinary(const fs::path& path, const std::vector<uint8_t>& data) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

static std::string unescapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        const char c = s[++i];
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
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"");
    std::smatch m;
    if (!std::regex_search(obj, m, r)) return {};
    return unescapeJson(m[1].str());
}

static bool fieldBool(const std::string& obj, const std::string& key, bool fallback) {
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (!std::regex_search(obj, m, r)) return fallback;
    return m[1].str() == "true";
}

static std::vector<std::string> fieldArrayDigits(const std::string& obj, const std::string& key) {
    std::vector<std::string> ids;
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch m;
    if (!std::regex_search(obj, m, r)) return ids;
    const std::regex n("\\d+");
    for (auto it = std::sregex_iterator(m[1].first, m[1].second, n); it != std::sregex_iterator(); ++it)
        ids.push_back(it->str());
    return ids;
}

static bool fieldUnsignedString(const std::string& obj, const std::string& key, std::string& out) {
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*(\\d+)");
    std::smatch m;
    if (!std::regex_search(obj, m, r)) return false;
    out = m[1].str();
    return !out.empty();
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
        const char c = json[i];
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
        r.replaceIds = fieldArrayDigits(obj, "replace_ids");
        r.mode = fieldString(obj, "mode");
        r.enabled = fieldBool(obj, "enabled", true);
        r.localPath = fieldString(obj, "local_path");
        r.hasWithId = fieldUnsignedString(obj, "with_id", r.withId);
        r.cdnUrl = fieldString(obj, "cdn_url");
        r.fishstrapPath = fieldString(obj, "fishstrap_path");
        rules.push_back(std::move(r));
    }
    return rules;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string skyFaceFromName(const std::string& name, const std::vector<std::string>& replaceIds) {
    const auto n = lower(name);
    if (n.find("skybox bk") != std::string::npos || n.find("sky bk") != std::string::npos) return "bk";
    if (n.find("skybox dn") != std::string::npos || n.find("sky dn") != std::string::npos) return "dn";
    if (n.find("skybox ft") != std::string::npos || n.find("sky ft") != std::string::npos) return "ft";
    if (n.find("skybox lf") != std::string::npos || n.find("sky lf") != std::string::npos) return "lf";
    if (n.find("skybox rt") != std::string::npos || n.find("sky rt") != std::string::npos) return "rt";
    if (n.find("skybox up") != std::string::npos || n.find("sky up") != std::string::npos) return "up";

    static const std::unordered_map<std::string, std::string> known = {
        {"14147881792", "bk"}, {"14147882149", "dn"}, {"14147882761", "ft"},
        {"14147883091", "lf"}, {"14147882405", "rt"}, {"14147881297", "up"}
    };
    for (const auto& id : replaceIds) {
        auto it = known.find(id);
        if (it != known.end()) return it->second;
    }
    return {};
}

static std::string skyTarget(const std::string& face) {
    return "PlatformContent\\pc\\textures\\sky\\sky512_" + face + ".tex";
}

static fs::path outputDirFor(const fs::path& config) {
    return config.parent_path() / (config.stem().string() + "_FishstrapModV3");
}

static std::string normalizeSlashes(std::string s) {
    std::replace(s.begin(), s.end(), '/', '\\');
    return s;
}

static fs::path findAssetById(const fs::path& configDir, const std::string& id) {
    const fs::path assets = configDir / "assets";
    const std::vector<fs::path> roots = {assets, configDir};

    for (const auto& root : roots) {
        if (!fs::exists(root) || !fs::is_directory(root)) continue;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
            const auto filename = it->path().filename().string();
            const auto stem = it->path().stem().string();
            if (filename == id || stem == id) return it->path();
        }
    }
    return {};
}

static bool isDDSBytes(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ';
}

static bool isPNGBytes(const std::vector<uint8_t>& data) {
    static const uint8_t sig[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return data.size() >= sizeof(sig) && std::equal(std::begin(sig), std::end(sig), data.begin());
}

static bool decodePNGBytes(const fs::path& source, Image& image) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;

    do {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory));
        if (FAILED(hr)) break;
        hr = factory->CreateDecoderFromFilename(source.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr)) break;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) break;

        UINT width = 0, height = 0;
        hr = frame->GetSize(&width, &height);
        if (FAILED(hr) || width == 0 || height == 0) break;

        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) break;
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr)) break;

        image.width = width;
        image.height = height;
        image.bgra.resize(static_cast<size_t>(width) * height * 4);
        hr = converter->CopyPixels(nullptr, width * 4,
                                   static_cast<UINT>(image.bgra.size()), image.bgra.data());
        if (FAILED(hr)) break;
        ok = true;
    } while (false);

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (needUninit) CoUninitialize();
    return ok;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r * 31 + 127) / 255) << 11 |
                                 ((g * 63 + 127) / 255) << 5 |
                                 ((b * 31 + 127) / 255));
}

static void rgbFrom565(uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31;
    g = ((c >> 5) & 63) * 255 / 63;
    b = (c & 31) * 255 / 31;
}

static void paletteFrom565(uint16_t c0, uint16_t c1, int palette[4][3]) {
    int r0, g0, b0, r1, g1, b1;
    rgbFrom565(c0, r0, g0, b0);
    rgbFrom565(c1, r1, g1, b1);
    palette[0][0] = r0; palette[0][1] = g0; palette[0][2] = b0;
    palette[1][0] = r1; palette[1][1] = g1; palette[1][2] = b1;
    palette[2][0] = (2 * r0 + r1) / 3;
    palette[2][1] = (2 * g0 + g1) / 3;
    palette[2][2] = (2 * b0 + b1) / 3;
    palette[3][0] = (r0 + 2 * r1) / 3;
    palette[3][1] = (g0 + 2 * g1) / 3;
    palette[3][2] = (b0 + 2 * b1) / 3;
}

static void compressBC1Block(const uint8_t* bgra, UINT width, UINT height,
                             UINT bx, UINT by, std::ofstream& out) {
    uint8_t pixels[16][3]{};
    int minR = 255, minG = 255, minB = 255;
    int maxR = 0, maxG = 0, maxB = 0;

    for (UINT y = 0; y < 4; ++y) {
        for (UINT x = 0; x < 4; ++x) {
            const UINT px = std::min(bx * 4 + x, width - 1);
            const UINT py = std::min(by * 4 + y, height - 1);
            const size_t i = (static_cast<size_t>(py) * width + px) * 4;
            const int p = static_cast<int>(y * 4 + x);
            pixels[p][0] = bgra[i + 2];
            pixels[p][1] = bgra[i + 1];
            pixels[p][2] = bgra[i + 0];
            minR = std::min(minR, static_cast<int>(pixels[p][0]));
            minG = std::min(minG, static_cast<int>(pixels[p][1]));
            minB = std::min(minB, static_cast<int>(pixels[p][2]));
            maxR = std::max(maxR, static_cast<int>(pixels[p][0]));
            maxG = std::max(maxG, static_cast<int>(pixels[p][1]));
            maxB = std::max(maxB, static_cast<int>(pixels[p][2]));
        }
    }

    uint16_t c0 = rgb565(static_cast<uint8_t>(maxR), static_cast<uint8_t>(maxG), static_cast<uint8_t>(maxB));
    uint16_t c1 = rgb565(static_cast<uint8_t>(minR), static_cast<uint8_t>(minG), static_cast<uint8_t>(minB));
    if (c0 < c1) std::swap(c0, c1);
    if (c0 == c1 && c0 > 0) --c1;

    int palette[4][3]{};
    paletteFrom565(c0, c1, palette);

    uint32_t indices = 0;
    for (int p = 0; p < 16; ++p) {
        int best = 0;
        int bestDist = INT_MAX;
        for (int q = 0; q < 4; ++q) {
            const int dr = static_cast<int>(pixels[p][0]) - palette[q][0];
            const int dg = static_cast<int>(pixels[p][1]) - palette[q][1];
            const int db = static_cast<int>(pixels[p][2]) - palette[q][2];
            const int d = dr * dr + dg * dg + db * db;
            if (d < bestDist) { bestDist = d; best = q; }
        }
        indices |= static_cast<uint32_t>(best) << (2 * p);
    }

    out.write(reinterpret_cast<const char*>(&c0), sizeof(c0));
    out.write(reinterpret_cast<const char*>(&c1), sizeof(c1));
    out.write(reinterpret_cast<const char*>(&indices), sizeof(indices));
}

static bool writeDDSBC1(const fs::path& destination, const Image& image) {
    if (image.width == 0 || image.height == 0) return false;
    if (image.width > 16384 || image.height > 16384) return false;
    fs::create_directories(destination.parent_path());
    std::ofstream out(destination, std::ios::binary);
    if (!out) return false;

    const uint32_t fourCC_DXT1 =
        static_cast<uint32_t>('D') |
        (static_cast<uint32_t>('X') << 8) |
        (static_cast<uint32_t>('T') << 16) |
        (static_cast<uint32_t>('1') << 24);

    DDS_HEADER header{};
    header.size = 124;
    header.flags = 0x0002100F;
    header.height = image.height;
    header.width = image.width;
    const uint32_t blocksX = (image.width + 3) / 4;
    const uint32_t blocksY = (image.height + 3) / 4;
    header.pitchOrLinearSize = blocksX * blocksY * 8;
    header.mipMapCount = 1;
    header.ddspf.size = 32;
    header.ddspf.flags = 0x00000004;
    header.ddspf.fourCC = fourCC_DXT1;
    header.caps = 0x00001000;

    const uint32_t magic = 0x20534444;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (UINT by = 0; by < blocksY; ++by)
        for (UINT bx = 0; bx < blocksX; ++bx)
            compressBC1Block(image.bgra.data(), image.width, image.height, bx, by, out);
    return static_cast<bool>(out);
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

static void printUsage() {
    std::cout <<
        "FleasionToFishstrap V3 - local asset converter\n\n"
        "Usage:\n"
        "  FleasionToFishstrapV3.exe <config.json>\n\n"
        "Assets:\n"
        "  Put downloaded replacement assets in an 'assets' folder beside the JSON.\n"
        "  Name each file with its Fleasion with_id, with or without an extension.\n"
        "\n"
        "Example:\n"
        "  MySky.json\n"
        "  assets\\93590148140827\n"
        "  assets\\102453743082771.png\n\n"
        "Output:\n"
        "  <config>_FishstrapModV3\\\n"
        "  PlatformContent\\pc\\textures\\sky\\sky512_*.tex\n\n";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printUsage();
        return 1;
    }

    try {
        const fs::path config = fs::absolute(argv[1]);
        if (!fs::exists(config) || !fs::is_regular_file(config)) {
            std::cerr << "ERROR: Config file not found: " << config << "\n";
            return 1;
        }

        const auto rules = loadRules(config);
        if (rules.empty()) {
            std::cerr << "ERROR: No replacement_rules found.\n";
            return 1;
        }

        const fs::path output = outputDirFor(config);
        const fs::path downloads = output / "_sources";
        fs::create_directories(downloads);

        std::ofstream report(output / "CONVERSION_REPORT.txt", std::ios::binary);
        report << "FleasionToFishstrap V3 conversion report\r\n\r\n";
        report << "Input: " << config.string() << "\r\n";
        report << "Assets: " << (config.parent_path() / "assets").string() << "\r\n\r\n";

        size_t converted = 0;
        size_t skipped = 0;

        for (const auto& rule : rules) {
            if (!rule.enabled) {
                report << "SKIP: " << rule.name << " (disabled)\r\n";
                ++skipped;
                continue;
            }

            const auto face = skyFaceFromName(rule.name, rule.replaceIds);
            if (face.empty()) {
                report << "SKIP: " << rule.name << " (no sky face detected; add a fishstrap_path for non-sky rules)\r\n";
                ++skipped;
                continue;
            }

            std::vector<uint8_t> bytes;
            fs::path source;
            bool gotSource = false;

            if (rule.mode == "id") {
                if (!rule.hasWithId) {
                    report << "SKIP: " << rule.name << " (id mode is missing with_id)\r\n";
                    ++skipped;
                    continue;
                }
                source = findAssetById(config.parent_path(), rule.withId);
                if (!source.empty()) gotSource = readBinary(source, bytes);
                if (!gotSource) {
                    report << "FAIL: " << rule.name << " (asset " << rule.withId
                           << " not found; put it in " << (config.parent_path() / "assets").string() << ")\r\n";
                    std::cerr << "  MISSING asset " << rule.withId << " for " << rule.name << "\n";
                    ++skipped;
                    continue;
                }
            } else if (rule.mode == "local") {
                if (rule.localPath.empty()) {
                    report << "SKIP: " << rule.name << " (local mode is missing local_path)\r\n";
                    ++skipped;
                    continue;
                }
                source = fs::path(rule.localPath);
                if (!source.is_absolute()) source = config.parent_path() / source;
                gotSource = readBinary(source, bytes);
                if (!gotSource) {
                    report << "FAIL: " << rule.name << " (local_path could not be read)\r\n";
                    ++skipped;
                    continue;
                }
            } else if (rule.mode == "cdn") {
                report << "SKIP: " << rule.name << " (cdn mode is not downloaded by V3; download the file into assets first)\r\n";
                ++skipped;
                continue;
            } else {
                report << "SKIP: " << rule.name << " (unsupported mode: " << rule.mode << ")\r\n";
                ++skipped;
                continue;
            }

            const fs::path sourceCopy = downloads / (rule.withId.empty() ? ("sky_" + face) : rule.withId);
            writeBinary(sourceCopy, bytes);

            const fs::path destination = output / skyTarget(face);

            if (isDDSBytes(bytes)) {
                if (!writeBinary(destination, bytes)) {
                    report << "FAIL: " << rule.name << " (could not write TEX file)\r\n";
                    ++skipped;
                    continue;
                }
                report << "OK: " << rule.name << " -> " << skyTarget(face) << " (DDS source)\r\n";
                std::cout << "OK: " << rule.name << " -> " << destination << "\n";
                ++converted;
                continue;
            }

            if (!isPNGBytes(bytes)) {
                report << "FAIL: " << rule.name << " (source is not PNG or DDS; bytes are kept in _sources)\r\n";
                std::cerr << "  BAD FORMAT for " << rule.name << "\n";
                ++skipped;
                continue;
            }

            const fs::path pngTemp = downloads / ("sky_" + face + ".png");
            if (!writeBinary(pngTemp, bytes)) {
                report << "FAIL: " << rule.name << " (could not write temporary PNG)\r\n";
                ++skipped;
                continue;
            }

            Image image;
            if (!decodePNGBytes(pngTemp, image)) {
                report << "FAIL: " << rule.name << " (Windows Imaging Component could not decode PNG)\r\n";
                ++skipped;
                continue;
            }

            if (image.width != 512 || image.height != 512) {
                report << "WARN: " << rule.name << " source is " << image.width << "x" << image.height
                       << "; 512x512 is the expected sky512 size.\r\n";
            }

            if (!writeDDSBC1(destination, image)) {
                report << "FAIL: " << rule.name << " (could not create BC1/DXT1 TEX file)\r\n";
                ++skipped;
                continue;
            }

            report << "OK: " << rule.name << " -> " << skyTarget(face)
                   << " (PNG -> BC1/DXT1 TEX)\r\n";
            std::cout << "OK: " << rule.name << " -> " << destination << "\n";
            ++converted;
        }

        report << "\r\nConverted: " << converted << "\r\n";
        report << "Skipped/failed: " << skipped << "\r\n";

        std::cout << "\nConverted: " << converted << "\n";
        std::cout << "Skipped/failed: " << skipped << "\n";
        std::cout << "Output: " << output << "\n";
        std::cout << "\nCopy the CONTENTS of that folder into:\n";
        std::cout << "%LocalAppData%\\Fishstrap\\Modifications\\\n";
        return converted == 0 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}

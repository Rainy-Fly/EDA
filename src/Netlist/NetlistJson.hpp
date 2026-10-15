#pragma once

#include "Netlist.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace NetlistData {

struct ReadResult {
    std::optional<Netlist> value;
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return value.has_value(); }
};

struct WriteResult {
    std::vector<Diagnostic> diagnostics;

    bool ok() const {
        for (const auto& diagnostic : diagnostics)
            if (diagnostic.severity == "error") return false;
        return true;
    }
};

class NetlistJson {
public:
    static ReadResult read(const std::filesystem::path& path);
    static ReadResult parse(const std::string& jsonText);
    static WriteResult write(const Netlist& netlist, const std::filesystem::path& path);
    static std::string serialize(const Netlist& netlist, std::vector<Diagnostic>* diagnostics = nullptr);
};

} // namespace NetlistData

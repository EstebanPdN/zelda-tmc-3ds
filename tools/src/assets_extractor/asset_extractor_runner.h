#pragma once

#include <filesystem>
#include <string>

bool RunEmbeddedAssetExtractor(const std::filesystem::path& root, std::string* error = nullptr);

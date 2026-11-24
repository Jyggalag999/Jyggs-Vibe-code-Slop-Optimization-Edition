#pragma once
#include <string>

// Glue between main.cpp and the fake data layer.
namespace Preview {
	bool InitData(const std::string& defaultsPath, const std::string& dataDir, const std::string& target, bool interior);
	bool CloseRequested();
	void SetUserValue(const std::string& section, const std::string& key, const std::string& value);
}

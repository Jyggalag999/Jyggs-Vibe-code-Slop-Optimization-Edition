#pragma once
/*
* Locations: line-preserving INI editor used by the preset manager.
*
* Keeps every raw line of the file. Parsing rules match IniFile (';' comment only at line start, trimmed
* keys/values, case-insensitive names, first duplicate wins). Set() rewrites only the line it changes.
* No engine dependencies (std + Win32 file functions).
*/

#include <string>
#include <vector>

namespace Locations {

class IniDocument {
public:
	struct Item { std::string section, key, value; };	// one key line, in file order (all duplicates included)

	bool Load(const std::string& path);					// false if the file can't be read
	void Parse(const std::string& text);
	std::string Text() const;							// the file as it would be written

	const std::string* Get(const std::string& section, const std::string& key) const;	// first match, nullptr if absent
	std::vector<Item> Items() const;
	std::vector<std::string> Sections() const;			// section names in file order (duplicates included)

	bool Set(const std::string& section, const std::string& key, const std::string& value);	// true if anything changed
	bool Remove(const std::string& section, const std::string& key);						// removes the first match
	bool Dirty() const { return Modified; }

	// Writes to path (temp file + replace; falls back to in-place). The first save of a path in this process
	// copies the existing file to "<path>.nvrbak" when no backup exists yet. Returns false and fills error on failure.
	bool Save(const std::string& path, std::string* error = nullptr);

private:
	enum Kind { Blank, Comment, SectionLine, KeyLine, Other };
	struct Line {
		std::string raw;		// without line ending
		Kind kind = Other;
		std::string section;	// owning section (SectionLine: its own name; KeyLine: the section it sits in)
		std::string key, value;
	};
	std::vector<Line> Lines;
	std::string Eol = "\r\n";
	bool TrailingEol = true;
	bool Bom = false;
	bool Modified = false;

	static void Classify(Line& l, const std::string& currentSection, bool& haveSection);
	int FindKey(const std::string& section, const std::string& key) const;
};

}

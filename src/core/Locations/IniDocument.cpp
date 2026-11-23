#include "IniDocument.h"
#include "LocationConfig.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <set>

namespace Locations {

static std::string TrimWs(const std::string& s) {
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
	return s.substr(b, e - b);
}

void IniDocument::Classify(Line& l, const std::string& currentSection, bool& haveSection) {
	std::string t = TrimWs(l.raw);
	l.section.clear(); l.key.clear(); l.value.clear();
	if (t.empty()) { l.kind = Blank; return; }
	if (t[0] == ';') { l.kind = Comment; return; }
	if (t[0] == '[') {
		size_t close = t.find(']');
		if (close == std::string::npos) { l.kind = Other; return; }
		l.kind = SectionLine;
		l.section = TrimWs(t.substr(1, close - 1));
		haveSection = true;
		return;
	}
	size_t eq = t.find('=');
	if (!haveSection || eq == std::string::npos) { l.kind = Other; return; }
	std::string k = TrimWs(t.substr(0, eq));
	if (k.empty()) { l.kind = Other; return; }
	l.kind = KeyLine;
	l.section = currentSection;
	l.key = k;
	l.value = TrimWs(t.substr(eq + 1));
}

void IniDocument::Parse(const std::string& textIn) {
	Lines.clear();
	Modified = false;
	Bom = false;
	std::string text = textIn;
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
		Bom = true;	// kept and written back so an untouched file stays byte-identical
		text = text.substr(3);
	}
	size_t crlf = 0, lf = 0;
	for (size_t i = 0; i < text.size(); i++) if (text[i] == '\n') { if (i > 0 && text[i - 1] == '\r') crlf++; else lf++; }
	Eol = (lf > crlf) ? "\n" : "\r\n";
	TrailingEol = text.empty() || text.back() == '\n';

	size_t pos = 0;
	while (pos < text.size()) {
		size_t nl = text.find('\n', pos);
		std::string raw = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = (nl == std::string::npos) ? text.size() : nl + 1;
		if (!raw.empty() && raw.back() == '\r') raw.pop_back();
		Line l; l.raw = raw;
		Lines.push_back(l);
	}
	std::string cur; bool have = false;
	for (auto& l : Lines) {
		Classify(l, cur, have);
		if (l.kind == SectionLine) cur = l.section;
	}
}

bool IniDocument::Load(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	std::stringstream ss; ss << f.rdbuf();
	Parse(ss.str());
	return true;
}

std::string IniDocument::Text() const {
	std::string out = Bom ? std::string("\xEF\xBB\xBF") : std::string();
	for (size_t i = 0; i < Lines.size(); i++) {
		out += Lines[i].raw;
		if (i + 1 < Lines.size() || TrailingEol) out += Eol;
	}
	return out;
}

int IniDocument::FindKey(const std::string& section, const std::string& key) const {
	std::string sl = Lower(section), kl = Lower(key);
	// IniFile semantics: only the first section with that name counts, first duplicate key wins.
	bool found = false; std::string firstSecLine;
	size_t firstSec = (size_t)-1;
	for (size_t i = 0; i < Lines.size(); i++) if (Lines[i].kind == SectionLine && Lower(Lines[i].section) == sl) { firstSec = i; found = true; break; }
	if (!found) return -1;
	for (size_t i = firstSec + 1; i < Lines.size(); i++) {
		if (Lines[i].kind == SectionLine) break;
		if (Lines[i].kind == KeyLine && Lower(Lines[i].key) == kl) return (int)i;
	}
	return -1;
}

const std::string* IniDocument::Get(const std::string& section, const std::string& key) const {
	int i = FindKey(section, key);
	return i < 0 ? nullptr : &Lines[i].value;
}

std::vector<IniDocument::Item> IniDocument::Items() const {
	std::vector<Item> out;
	for (auto& l : Lines) if (l.kind == KeyLine) out.push_back({ l.section, l.key, l.value });
	return out;
}

std::vector<std::string> IniDocument::Sections() const {
	std::vector<std::string> out;
	for (auto& l : Lines) if (l.kind == SectionLine) out.push_back(l.section);
	return out;
}

bool IniDocument::Set(const std::string& section, const std::string& key, const std::string& value) {
	int i = FindKey(section, key);
	if (i >= 0) {
		Line& l = Lines[i];
		if (l.value == value) return false;
		// keep the key's spelling and any leading whitespace
		size_t lead = 0; while (lead < l.raw.size() && (l.raw[lead] == ' ' || l.raw[lead] == '\t')) lead++;
		size_t eq = l.raw.find('=');
		std::string keyPart = l.raw.substr(0, eq);
		// "key = " style: keep the spacing around '=' if the original had a space after it
		bool spaceAfter = eq + 1 < l.raw.size() && l.raw[eq + 1] == ' ';
		l.raw = keyPart + "=" + (spaceAfter && !value.empty() ? " " : "") + value;
		l.value = value;
		Modified = true;
		return true;
	}
	// find the first section with that name and its last key line
	std::string sl = Lower(section);
	size_t sec = (size_t)-1;
	for (size_t j = 0; j < Lines.size(); j++) if (Lines[j].kind == SectionLine && Lower(Lines[j].section) == sl) { sec = j; break; }
	Line nl; nl.raw = key + "=" + value; nl.kind = KeyLine; nl.section = section; nl.key = key; nl.value = value;
	if (sec != (size_t)-1) {
		size_t last = sec;
		for (size_t j = sec + 1; j < Lines.size(); j++) {
			if (Lines[j].kind == SectionLine) break;
			if (Lines[j].kind == KeyLine) last = j;
		}
		nl.section = Lines[sec].section;
		Lines.insert(Lines.begin() + last + 1, nl);
	} else {
		if (!Lines.empty() && Lines.back().kind != Blank) { Line b; b.kind = Blank; Lines.push_back(b); }
		Line h; h.raw = "[" + section + "]"; h.kind = SectionLine; h.section = section;
		Lines.push_back(h);
		Lines.push_back(nl);
	}
	if (!TrailingEol && !Lines.empty()) TrailingEol = true;
	Modified = true;
	return true;
}

bool IniDocument::Remove(const std::string& section, const std::string& key) {
	int i = FindKey(section, key);
	if (i < 0) return false;
	Lines.erase(Lines.begin() + i);
	Modified = true;
	return true;
}

static std::set<std::string>& BackedUp() { static std::set<std::string> s; return s; }

bool IniDocument::Save(const std::string& path, std::string* error) {
	auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
	std::string lp = Lower(path);
	if (!BackedUp().count(lp)) {
		DWORD attr = GetFileAttributesA(path.c_str());
		if (attr != INVALID_FILE_ATTRIBUTES) {
			std::string bak = path + ".nvrbak";
			if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES && !CopyFileA(path.c_str(), bak.c_str(), TRUE))
				return fail("could not create backup " + bak);
		}
		BackedUp().insert(lp);
	}
	std::string data = Text();
	std::string tmp = path + ".tmp";
	bool ok = false;
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (f) { f.write(data.data(), (std::streamsize)data.size()); f.flush(); ok = f.good(); }
	}
	if (ok && MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { Modified = false; return true; }
	DeleteFileA(tmp.c_str());
	// fallback: write in place (some virtual file systems refuse the rename)
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) return fail("could not write " + path);
	f.write(data.data(), (std::streamsize)data.size());
	f.flush();
	if (!f.good()) return fail("write failed for " + path);
	Modified = false;
	return true;
}

}

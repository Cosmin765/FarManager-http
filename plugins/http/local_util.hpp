#ifndef LOCAL_UTIL_HPP
#define LOCAL_UTIL_HPP
#include "headers.hpp"
#include "structs.hpp"

extern PluginStartupInfo PsInfo;

inline const wchar_t* GetMsg(int MsgId)
{
	return PsInfo.GetMsg(&MainGuid, MsgId);
}

inline intptr_t BasicErrorMessage(std::initializer_list<const wchar_t*> items, int buttonsCount = 1)
{
	return PsInfo.Message(&MainGuid, NULL, FMSG_WARNING, L"Error", items.begin(), items.size(), buttonsCount);
}

inline std::string WideCharToMultiByte(const wchar_t* unicode, uint32_t codePage = CP_UTF8)
{
	int requiredSize = WideCharToMultiByte(codePage, 0, unicode, -1, {}, 0, {}, {});
	std::string str;
	str.resize(requiredSize - 1); // don't include the null character
	WideCharToMultiByte(codePage, 0, unicode, -1, str.data(), requiredSize, {}, {});
	return str;
}

inline std::string WideCharToMultiByte(const string& unicode, uint32_t codePage = CP_UTF8)
{
	return WideCharToMultiByte(unicode.c_str(), codePage);
}

inline string MultiByteToWideChar(const char* ansi, uint32_t codePage = CP_UTF8)
{
	int requiredSize = MultiByteToWideChar(codePage, 0, ansi, -1, NULL, 0); // get the required size
	string wideStr;
	wideStr.resize(requiredSize - 1);  // don't include the null character
	MultiByteToWideChar(CP_UTF8, 0, ansi, requiredSize, wideStr.data(), requiredSize);
	return wideStr;
}

inline string MultiByteToWideChar(const std::string& ansi, uint32_t codePage = CP_UTF8)
{
	return MultiByteToWideChar(ansi.c_str(), codePage);
}

struct LocalFreeDeleter
{
	void operator()(wchar_t* p) const
	{
		if (p) LocalFree(p);
	}
};
using WinErrorPtr = std::unique_ptr<wchar_t, LocalFreeDeleter>;

// get the string representation of the last WinAPI error
inline WinErrorPtr LastWinAPIError()
{
	DWORD errorCode = GetLastError();
	LPWSTR errorMessage = NULL;
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, errorCode, 0, (LPWSTR)&errorMessage, 0, NULL);
	if (errorMessage != NULL)
		errorMessage[lstrlenW(errorMessage) - 2] = L'\0';  // get rid of \r\n
	return WinErrorPtr(errorMessage);
}

inline const wchar_t* NullToEmpty(const wchar_t* Str)
{
	return Str? Str : L"";
}

inline bool GetTempPathWithExtension(wchar_t* buffer, size_t bufferChars, const wchar_t* extension, const wchar_t* prefixString = L"HTTPFAR")
{
	wchar_t dirPath[MAX_PATH + 1]{};
	{
		PluginSettings settings(MainGuid, PsInfo.SettingsControl);
		const wchar_t* dldPath = settings.Get(0, L"DownloadsPath", nullptr);
		std::filesystem::create_directories(dldPath);
		wcsncpy_s(dirPath, dldPath, MAX_PATH);
	}
	size_t dirLen = wcsnlen_s(dirPath, MAX_PATH);
	if (dirLen == 0)
	{
		if (GetTempPath2W(MAX_PATH, dirPath) == 0)
			return false;
	}

	if (GetTempFileNameW(dirPath, prefixString, 0, buffer) == 0)
		return false;
	DeleteFileW(buffer);
	size_t occupiedSize = wcsnlen_s(buffer, MAX_PATH);
	wcscpy_s(buffer + occupiedSize, bufferChars - occupiedSize, extension);
	return true;
}

static constexpr auto
	none_pressed = 0,
	any_ctrl_pressed = LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED,
	any_alt_pressed = LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED,
	any_shift_pressed = SHIFT_PRESSED;

constexpr auto check_control(unsigned const ControlState, unsigned const Mask)
{
	constexpr auto ValidMask = any_ctrl_pressed | any_alt_pressed | any_shift_pressed;

	const auto FilteredControlState = ControlState & ValidMask;
	const auto OtherKeys = ValidMask & ~Mask;

	return ((FilteredControlState & Mask) || !Mask) && !(FilteredControlState & OtherKeys);
};

template<typename InputType>
bool isValidJSON(InputType&& i)
{
	try
	{
		auto j = nlohmann::json::parse(i);
		return true;
	}
	catch (const nlohmann::json::parse_error& e)
	{
		// parsing failed, invalid JSON
		return false;
	}
}


static std::vector<std::pair<std::string, std::string>> GetAllHeaders(CURL* curl)
{
	curl_header* prev = nullptr;
	curl_header* h;
	std::vector<std::pair<std::string, std::string>> headers;
	while ((h = curl_easy_nextheader(curl, CURLH_HEADER, -1, prev)))
	{
		headers.push_back({ h->name, h->value });
		prev = h;
	}
	return headers;
}

#endif // LOCAL_UTIL_HPP

#include "HTTP.hpp"

static inline intptr_t BasicErrorMessage(std::initializer_list<const wchar_t*> items, int buttonsCount = 1)
{
	return PsInfo.Message(&MainGuid, NULL, FMSG_WARNING, L"Error", items.begin(), items.size(), buttonsCount);
}

static inline const wchar_t* NullToEmpty(const wchar_t* Str)
{
	return Str? Str : L"";
}

HTTPclass::HTTPclass() {
	assert(test_StringSerializer());
	assert(test_HTTPTemplateSerializer());
}

HTTPclass::~HTTPclass() {
	if (curl)
	{
		// curl cleanup
		curl_easy_cleanup(curl);
		curl_global_cleanup();
	}
}

void HTTPclass::GetOpenPanelInfo(OpenPanelInfo* Info)
{
	Info->StructSize = sizeof(*Info);
	Info->Flags = OPIF_ADDDOTS | OPIF_SHOWNAMESONLY | OPIF_USEATTRHIGHLIGHTING;
	Info->CurDir = L"";

	static std::wstring Title = std::format(L" {} ", GetMsg(MTitle));
	Info->PanelTitle = Title.c_str();

	static WORD FKeys[] =
	{
		VK_F3, 0, MOpenView,
		VK_F4, 0, MOpenEdit,
	};

	static KeyBarLabel kbl[std::size(FKeys) / 3];
	static KeyBarTitles kbt = { std::size(kbl), kbl };

	for (size_t j = 0, i = 0; i < std::size(FKeys); i += 3, ++j)
	{
		kbl[j].Key.VirtualKeyCode = FKeys[i];
		kbl[j].Key.ControlKeyState = FKeys[i + 1];

		if (FKeys[i + 2])
		{
			kbl[j].Text = kbl[j].LongText = GetMsg(FKeys[i + 2]);
		}
		else
		{
			kbl[j].Text = kbl[j].LongText = L"";
		}
	}

	Info->KeyBar = &kbt;
}


bool HTTPclass::EnsureTemplatesPath()
{
	static bool checked = false;
	if (checked)
		return true;
	PluginSettings settings(MainGuid, PsInfo.SettingsControl);
	const wchar_t* templatesPath = settings.Get(0, L"TemplatesPath", L"");

	if (!templatesPath || templatesPath[0] == L'\0')
	{
		// initialise the path
		// TODO: internationalise
		wchar_t buffer[MAX_PATH] = L"C:\\FarManager-HTTP";
		const wchar_t* boxTitle = L"Templates Path";
		const wchar_t* boxSubTitle = L"Where will HTTP templates be stored?";

		for (;;)
		{
			PsInfo.InputBox(&MainGuid, &InputBoxGuid, boxTitle, boxSubTitle, boxTitle, buffer, buffer, MAX_PATH, {}, FIB_BUTTONS);

			std::error_code ec;
			if (std::filesystem::exists(buffer, ec) && !ec)
				break;

			if (std::filesystem::create_directories(buffer, ec) && !ec)
				break;

			intptr_t btn = BasicErrorMessage({ L"Error", L"Could not create templates directory", buffer, L"\x01", L"&Retry", L"&Ok"}, 2);
			if (btn == 0)  // retry
				continue;
			else
			{
				PsInfo.PanelControl(this, FCTL_CLOSEPANEL, 0, nullptr);
				return false;
			}
		}
		settings.Set(0, L"TemplatesPath", buffer);
	}
	checked = true;
	return true;
}


bool HTTPclass::IsValidTemplate(const PluginPanelItem& item)
{
	static constexpr wchar_t extension[] = L".htmpl";

	const wchar_t* fileName = item.FileName;
	size_t nameLen = wcsnlen_s(fileName, MAX_PATH);

	// check extension
	size_t extLen = std::size(extension) - 1; // exclude null
	if (nameLen < extLen)
		return false;
	if (_wcsicmp(fileName + nameLen - extLen, extension) != 0)
		return false;

	return true;
}


bool HTTPclass::LoadTemplateItems()
{
	static bool loaded = false;
	// TODO: somehow reset this flag after the plugin has been exited
	if (loaded)
		return true;

	PluginSettings settings(MainGuid, PsInfo.SettingsControl);
	const wchar_t* templatesPath = settings.Get(0, L"TemplatesPath", L"");
	PluginPanelItem* ppi;
	size_t count;
	PsInfo.GetDirList(templatesPath, &ppi, &count);

	for (const auto& i : std::span(ppi, count))
	{
		if (!IsValidTemplate(i))
			continue;
		auto& newItem = pp.Items.emplace_back(i);
		newItem.FileName = pp.StringData.emplace_back(newItem.FileName).c_str();
		newItem.Owner = pp.OwnerData.emplace_back(NullToEmpty(newItem.Owner)).c_str();
		//NewItem.UserData.Data = reinterpret_cast<void*>(m_Panel->Items.size() - 1);
	}

	PsInfo.FreeDirList(ppi, count);

	loaded = true;
	return true;
}


int HTTPclass::GetFindData(PluginPanelItem*& pPanelItem, size_t& pItemsNumber, const OPERATION_MODES OpMode)
{
	if (!EnsureTemplatesPath())
		return false;
	if (!LoadTemplateItems())
		return false;

	pPanelItem = pp.Items.data();
	pItemsNumber = pp.Items.size();
	return true;
}


bool HTTPclass::PutOneFile(const string& SrcPath, const PluginPanelItem& PanelItem)
{
	auto& NewItem = pp.Items.emplace_back(PanelItem);
	auto& NewStr = pp.StringData.emplace_back(NewItem.FileName);
	if (!SrcPath.empty() && !contains(PanelItem.FileName, L'\\'))
		NewStr = concat(SrcPath, SrcPath.back() == L'\\'? L"" : L"\\", PanelItem.FileName);

	NewItem.FileName = NewStr.c_str();
	NewItem.AlternateFileName = {};
	NewItem.Owner = pp.OwnerData.emplace_back(NullToEmpty(NewItem.Owner)).c_str();
	NewItem.UserData.Data = reinterpret_cast<void*>(pp.Items.size() - 1);

	return true;
}


bool HTTPclass::PutFiles(const std::span<const PluginPanelItem> Files, const wchar_t* const SrcPath, const OPERATION_MODES)
{
	for (const auto& file : Files)
	{
		if (file.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;  // skip directories for now

		if (!IsValidTemplate(file))
			continue;

		PutOneFile(SrcPath, file);
	}
	return true;
}


// the return value of this function needs to be freed using delete[]
static wchar_t* AnsiToUnicode(const char* ansi, uint32_t codePage = CP_UTF8)
{
	int requiredSize = MultiByteToWideChar(codePage, 0, ansi, -1, NULL, 0); // get the required size
	wchar_t* wideErrorMessage = new wchar_t[requiredSize + 1];
	MultiByteToWideChar(CP_UTF8, 0, ansi, requiredSize, wideErrorMessage, requiredSize);
	wideErrorMessage[requiredSize] = L'\0';
	return wideErrorMessage;
}


// get the string representation of the last WinAPI error
// LocalFree() must be called on the return value once it's no longer needed
static wchar_t* LastWinAPIError()
{
	DWORD errorCode = GetLastError();
	LPWSTR errorMessage = NULL;
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, errorCode, 0, (LPWSTR)&errorMessage, 0, NULL);
	if (errorMessage != NULL)
		errorMessage[lstrlenW(errorMessage) - 2] = L'\0';  // get rid of \r\n
	return errorMessage;
}


static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
	HANDLE fileHandle = static_cast<HANDLE>(userp);
	if (fileHandle == INVALID_HANDLE_VALUE)
		return 0;

	const size_t totalSize = size * nmemb;
	DWORD written;
	WriteFile(fileHandle, contents, (DWORD)nmemb, &written, NULL);
	return written;
}


CURLcode HTTPclass::ObtainHttpHeaders(const char* url)
{
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

	CURLcode result = curl_easy_perform(curl);

	return result;
}


ContentType HTTPclass::GetHTTPContentType()
{
	curl_header* header = nullptr;
	CURLHcode hCode = curl_easy_header(curl, "content-type", 0, CURLH_HEADER, -1, &header);
	if (hCode != CURLHE_OK || !header)
		return ContentType::Other;

	if (strstr(header->value, "application/json"))
		return ContentType::JSON;
	else if (strstr(header->value, "text/html"))
		return ContentType::HTML;
	else
		return ContentType::Other;
}


CURLcode HTTPclass::HttpDownload(const char* url, HANDLE fileHandle)
{
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fileHandle);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 0);

	CURLcode result = curl_easy_perform(curl);
	return result;
}


static bool GetTempPathWithExtension(wchar_t* buffer, size_t bufferChars, const wchar_t* extension)
{
	wchar_t tempDir[MAX_PATH];
	if (GetTempPath2W(MAX_PATH, tempDir) == 0)
		return false;
	if (GetTempFileNameW(tempDir, L"HTTPFAR", 0, buffer) == 0)
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


int HTTPclass::ProcessKey(const INPUT_RECORD* Rec)
{
	if (Rec->EventType != KEY_EVENT)
		return FALSE;

	const auto Key = Rec->Event.KeyEvent.wVirtualKeyCode;
	const auto ControlState = Rec->Event.KeyEvent.dwControlKeyState;

	const auto
		NonePressed = check_control(ControlState, none_pressed),
		//OnlyAnyShiftPressed = check_control(ControlState, any_shift_pressed),
		OnlyAnyAltPressed = check_control(ControlState, any_alt_pressed);

	if (NonePressed && Key == VK_F3)
	{
		OpenURL("http://localhost:8000/sample2.json");
		return TRUE;
	}

	return FALSE;
}

// TODO: for ineracting with the plugin (e.g. creating an endpoint file)
//PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MViewWithOptions, L"Config");
//Builder.AddText(MIncludeAdditionalInfo);
//Builder.AddCheckbox(MInclEnvironment, &LocalOpt.ExportEnvironment);
//Builder.AddCheckbox(MInclModuleInfo, &LocalOpt.ExportModuleInfo);
//Builder.AddCheckbox(MInclModuleVersion, &LocalOpt.ExportModuleVersion);
//Builder.AddCheckbox(MInclPerformance, &LocalOpt.ExportPerformance);
//Builder.AddCheckbox(MInclHandles, &LocalOpt.ExportHandles);
//Builder.AddCheckbox(MInclHandlesUnnamed, &LocalOpt.ExportHandlesUnnamed)->X1 += 4;
//Builder.AddOKCancel(MOk, MCancel);

bool HTTPclass::OpenURL(const char* Url)
{
	if (!curl)  // not initialised
	{
		CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
		if (result != CURLE_OK)
		{
			BasicErrorMessage({ L"Error", L"CURL global init failed", L"\x01", L"&Ok" });
			return false;
		}

		curl = curl_easy_init();
		if (!curl)  // failure
		{
			curl_global_cleanup();
			BasicErrorMessage({ L"Error", L"CURL easy init failed", L"\x01", L"&Ok" });
			return false;
		}
	}

	
	ObtainHttpHeaders(Url);
	const wchar_t* extension;
	switch (GetHTTPContentType())
	{
	case ContentType::JSON:
		extension = L".json";
		break;
	case ContentType::HTML:
		extension = L".html";
		break;
	case ContentType::Other:
	default:
		extension = L"";
	}

	wchar_t tempFile[MAX_PATH];
	if (!GetTempPathWithExtension(tempFile, MAX_PATH, extension))
	{
		wchar_t* errorMessage = LastWinAPIError();
		BasicErrorMessage({ L"Error", L"Could not reserve name for temp file", errorMessage, L"\x01", L"&Ok" });
		LocalFree(errorMessage);
		return false;
	}

	HANDLE fileHandle = CreateFileW(tempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		wchar_t* errorMessage = LastWinAPIError();
		BasicErrorMessage({ L"Error", L"Could not create temp file", tempFile, errorMessage, L"\x01", L"&Ok" });
		LocalFree(errorMessage);
		return false;
	}

	CURLcode curlCode = HttpDownload(Url, fileHandle);
	if (curlCode != CURLE_OK)
	{
		wchar_t* errorMessage = AnsiToUnicode(curl_easy_strerror(curlCode));
		wchar_t* wideUrl = AnsiToUnicode(Url);
		BasicErrorMessage({ L"HTTP error", wideUrl, errorMessage, L"\x01", L"&Ok" });
		delete[] wideUrl;
		delete[] errorMessage;
		return false;
	}

	CloseHandle(fileHandle);

	// open response buffer in editor
	PsInfo.Editor(tempFile, tempFile, 0, 0, -1, -1, VF_NONE, 1, 1, CP_DEFAULT);

	// delete the temp file
	bool retryDelete = true;
	while (retryDelete)
	{
		if (!DeleteFileW(tempFile))
		{
			DWORD errorCode = GetLastError();
			if (errorCode == ERROR_FILE_NOT_FOUND)
				break;
			wchar_t* errorMessage = LastWinAPIError();
			intptr_t choice = BasicErrorMessage({ L"Error", L"Could not delete temp file", tempFile, errorMessage, L"\x01", L"&Retry", L"&Ignore" }, 2);
			LocalFree(errorMessage);
			switch (choice)
			{
			case 0: // retry
				break;
			case -1: // escape key
			case 1: // ignore
				retryDelete = false;
				break;
			default:
				std::unreachable();
				break;
			}
		}
		else
		{
			retryDelete = false;
		}
	}

	return true;
}

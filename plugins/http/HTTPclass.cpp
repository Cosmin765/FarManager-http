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


bool HTTPclass::IsValidTemplateExtension(const wchar_t* templateName)
{
	size_t nameLen = wcsnlen_s(templateName, MAX_PATH);
	size_t extLen = std::size(extension) - 1; // exclude null
	if (nameLen < extLen)
		return false;
	if (_wcsicmp(templateName + nameLen - extLen, extension) != 0)
		return false;
	return true;
}


bool HTTPclass::IsValidTemplate(const PluginPanelItem& item, bool vebose = false)
{
	const wchar_t* fileName = item.FileName;
	
	// check extension
	if (!IsValidTemplateExtension(fileName))
	{
		if (vebose)
			BasicErrorMessage({ L"Error", L"Template extension not valid", L"\x01", L"&Ok" });
		return false;
	}

	HTTPTemplate tmpl;
	if (!DeserializeTemplateFromFile(fileName, tmpl, vebose))
		return false;

	return true;
}


bool HTTPclass::LoadTemplateItems()
{
	PluginSettings settings(MainGuid, PsInfo.SettingsControl);
	const wchar_t* templatesPath = settings.Get(0, L"TemplatesPath", L"");
	PluginPanelItem* ppi;
	size_t count;
	PsInfo.GetDirList(templatesPath, &ppi, &count);

	for (const auto& i : std::span(ppi, count))
	{
		if (!IsValidTemplate(i))
			continue;
		string FileName = i.FileName;
		if (pp.AddedItems.find(FileName) != pp.AddedItems.end())
			continue;  // already added
		pp.AddedItems.insert(FileName);
		auto& newItem = pp.Items.emplace_back(i);
		newItem.FileName = pp.StringData.emplace_back(newItem.FileName).c_str();
		newItem.Owner = pp.OwnerData.emplace_back(NullToEmpty(newItem.Owner)).c_str();
		newItem.AlternateFileName = {};  // access violation thrown if this is not set
		//NewItem.UserData.Data = reinterpret_cast<void*>(m_Panel->Items.size() - 1);
	}

	PsInfo.FreeDirList(ppi, count);

	return true;
}


void HTTPclass::CheckLoadedTemplates()
{
	for (size_t i = 0; i < pp.Items.size();)
	{
		const auto& item = pp.Items[i];
		DWORD dwAttrib = GetFileAttributes(item.FileName);
		bool isFile = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
		if (!isFile)
		{
			pp.Items.erase(pp.Items.begin() + i);
			pp.AddedItems.erase(item.FileName);
			// TODO: technically, we should also remove from pp.StringData, pp.OwnerData
			// but I'm not going to worry about that right now
		}
		else
		{
			++i;
		}
	}
}


int HTTPclass::GetFindData(PluginPanelItem*& pPanelItem, size_t& pItemsNumber, const OPERATION_MODES OpMode)
{
	CheckLoadedTemplates();
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
	string FileName = PanelItem.FileName;
	if (!SrcPath.empty() && !contains(PanelItem.FileName, L'\\'))
		FileName = concat(SrcPath, SrcPath.back() == L'\\'? L"" : L"\\", FileName);

	if (pp.AddedItems.find(FileName) != pp.AddedItems.end())
		return false;  // already added
	pp.AddedItems.insert(FileName);

	auto& NewItem = pp.Items.emplace_back(PanelItem);
	string& NewName = pp.StringData.emplace_back(FileName);

	NewItem.FileName = NewName.c_str();
	NewItem.AlternateFileName = {};
	NewItem.Owner = pp.OwnerData.emplace_back(NullToEmpty(NewItem.Owner)).c_str();
	//NewItem.UserData.Data = reinterpret_cast<void*>(pp.Items.size() - 1);

	return true;
}


bool HTTPclass::PutFiles(const std::span<const PluginPanelItem> Files, const wchar_t* const SrcPath, const OPERATION_MODES)
{
	for (const auto& file : Files)
	{
		if (file.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;  // skip directories for now

		if (!IsValidTemplate(file, true))
			continue;

		PutOneFile(SrcPath, file);
	}
	return true;
}


// the return value of this function needs to be freed using delete[]
static wchar_t* MultiByteToWideChar(const char* ansi, uint32_t codePage = CP_UTF8)
{
	int requiredSize = MultiByteToWideChar(codePage, 0, ansi, -1, NULL, 0); // get the required size
	wchar_t* wideStr = new wchar_t[requiredSize + 1];
	MultiByteToWideChar(CP_UTF8, 0, ansi, requiredSize, wideStr, requiredSize);
	wideStr[requiredSize] = L'\0';
	return wideStr;
}


// the return value of this function needs to be freed using delete[]
static char* WideCharToMultiByte(const wchar_t* unicode, uint32_t codePage = CP_UTF8)
{
	int requiredSize = WideCharToMultiByte(codePage, 0, unicode, -1, {}, 0, {}, {});
	char* str = new char[requiredSize + 1];
	WideCharToMultiByte(codePage, 0, unicode, -1, str, requiredSize, {}, {});
	str[requiredSize] = '\0';
	return str;
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

	DWORD written;
	auto success = WriteFile(fileHandle, contents, (DWORD)nmemb, &written, NULL);
	if (!success)
		BasicErrorMessage({ L"Error", L"Error writing to temp file", L"\x01", L"&Ok" });
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


bool HTTPclass::DeserializeTemplateFromFile(const wchar_t* filename, HTTPTemplate& httpTemplate, bool verbose)
{
	HANDLE templateFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	SCOPE_EXIT{ CloseHandle(templateFile); };
	DWORD bufferSize = GetFileSize(templateFile, NULL);
	std::vector<uint8_t> templateBuffer(bufferSize);
	DWORD bytesRead;
	if (!ReadFile(templateFile, templateBuffer.data(), bufferSize, &bytesRead, NULL))
	{
		if (verbose)
		{
			wchar_t* errStr = LastWinAPIError();
			BasicErrorMessage({ L"Error", L"Error reading from template file", filename, errStr, L"\x01", L"&Ok" });
			LocalFree(errStr);
		}
		return false;
	}

	try
	{
		httpTemplate.Deserialize(templateBuffer);
	}
	catch (std::runtime_error e)
	{
		if (verbose)
		{
			wchar_t* errWide = MultiByteToWideChar(e.what());
			BasicErrorMessage({ L"Error", L"Error deserializing from template file", filename, errWide, L"\x01", L"&Ok" });
			delete[] errWide;
		}
		return false;
	}
	return true;
}


int HTTPclass::ProcessKey(const INPUT_RECORD* Rec)
{
	if (Rec->EventType != KEY_EVENT)
		return FALSE;

	const auto Key = Rec->Event.KeyEvent.wVirtualKeyCode;
	const auto ControlState = Rec->Event.KeyEvent.dwControlKeyState;

	const auto
		NonePressed = check_control(ControlState, none_pressed),
		OnlyAnyShiftPressed = check_control(ControlState, any_shift_pressed),
		OnlyAnyAltPressed = check_control(ControlState, any_alt_pressed);

	if (NonePressed && (Key == VK_F3 || Key == VK_F4))
	{
		bool edit = Key == VK_F4;

		if (const size_t Size = PsInfo.PanelControl(this, FCTL_GETCURRENTPANELITEM, 0, {}))
		{
			PluginPanelItem* ppi = (PluginPanelItem*)malloc(Size);
			FarGetPluginPanelItem gpi{ sizeof(gpi), Size, ppi };
			PsInfo.PanelControl(this, FCTL_GETCURRENTPANELITEM, 0, &gpi);
			SCOPE_EXIT{ free(ppi); };

			if (wcsncmp(ppi->FileName, TEXT(".."), MAX_PATH) == 0)
				return FALSE; // not handled

			HTTPTemplate httpTemplate;
			if (!DeserializeTemplateFromFile(ppi->FileName, httpTemplate))
				return TRUE;  // event was handled

			OpenURL(httpTemplate, edit);
		}

		return TRUE;
	}

	if (OnlyAnyShiftPressed && Key == VK_F4)
	{
		// create the template file

		intptr_t result = -1;
		int okId = 0;
		int cancelId = 1;

		HTTPTemplate httpTemplate;
		std::vector<HTTPArgument>& arguments = httpTemplate.arguments;
		string filename;

		int listSelectedArgument;
		int selectedVerb;

		do
		{
			HTTPTemplateDialog Builder;

			Builder.AddText(TEXT("&Name"));
			Builder.AddEditField(filename, 100, TEXT("Template_Name"), true);

			Builder.AddText(TEXT("&URL"));
			Builder.AddEditField(httpTemplate.url, 100, TEXT("Edit_URL"), true);

			Builder.AddSeparator();

			Builder.AddText(TEXT("&Verb"));
			int verbMessageIds[] = { MVerbGET, MVerbPOST };
			selectedVerb = 0;
			Builder.AddRadioButtons(&selectedVerb, 2, verbMessageIds);

			Builder.AddSeparator();

			Builder.AddText(TEXT("Ar&guments"));

			std::vector<const wchar_t*> argumentsStr;
			argumentsStr.reserve(arguments.size());
			for (const auto& argument : arguments)
			{
				argumentsStr.push_back(argument.name.c_str());
			}
			listSelectedArgument = -1;
			Builder.AddListBox(&listSelectedArgument, 100, (int)std::min(argumentsStr.size() + 1, (size_t)5), argumentsStr.data(), argumentsStr.size(), DIF_NONE);

			int buttonMessageIds[] = { MAdd, MRemoveSelected, MRemoveAll };
			Builder.AddButtons(std::size(buttonMessageIds), buttonMessageIds, -1);
			int addArgumentId = Builder.GetLastID() - 2;
			int removeSelectedId = Builder.GetLastID() - 1;
			int removeAllArgumentsId = Builder.GetLastID();

			Builder.AddOKCancel(MOk, MCancel);

			result = Builder.ShowDialogEx();

			if (result == addArgumentId)
			{
				// configure argument
				PluginDialogBuilder LocalBuilder(PsInfo, MainGuid, ConfigDialogGuid, MHTTPArgument, TEXT("Argument"));

				HTTPArgument argument;

				LocalBuilder.AddText(TEXT("Name"));
				LocalBuilder.AddEditField(argument.name, 100, TEXT("HTTP_Argument_Name"), true);

				LocalBuilder.AddSeparator();

				LocalBuilder.AddText(TEXT("Value"));
				LocalBuilder.AddEditField(argument.value, 100, TEXT("HTTP_Argument_Value"), true);

				LocalBuilder.AddSeparator();

				LocalBuilder.AddText(TEXT("Type"));
				int argType = 0;
				int argTypes[] = { MArgQuery, MArgPath };
				LocalBuilder.AddRadioButtons(&argType, std::size(argTypes), argTypes);

				LocalBuilder.AddSeparator();

				LocalBuilder.AddText(TEXT("Retention"));
				int retention = 0;
				int retentions[] = { MRetentionAsk, MRetentionRemember };
				LocalBuilder.AddRadioButtons(&retention, std::size(retentions), retentions);

				LocalBuilder.AddOKCancel(MOk, MCancel);

				intptr_t localResult = LocalBuilder.ShowDialogEx();
				if (localResult == okId)
				{
					argument.type = (HTTPArgumentType)argType;
					argument.retention = (HTTPArgumentRetention)retention;
					arguments.push_back(argument);
				}
			}
			else if (result == removeSelectedId)
			{
				if (listSelectedArgument >= 0 && listSelectedArgument < (int)arguments.size())
				{
					arguments.erase(arguments.begin() + listSelectedArgument);
				}
			}
			else if (result == removeAllArgumentsId)
			{
				arguments.clear();
			}
		}
		while (result > cancelId);

		httpTemplate.verb = (HTTPVerb)selectedVerb;

		if (result == okId)
		{
			// save the template
			PluginSettings settings(MainGuid, PsInfo.SettingsControl);
			string templatesPath = settings.Get(0, L"TemplatesPath", L"");
			filename = concat(templatesPath, templatesPath.back() == L'\\'? L"" : L"\\", filename);
			if (!IsValidTemplateExtension(filename.c_str()))
			{
				filename = concat(filename, extension);
			}

			std::vector<uint8_t> templateBuffer;
			httpTemplate.Serialize(templateBuffer);

			HANDLE templateFile = CreateFile(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (!WriteFile(templateFile, templateBuffer.data(), (DWORD)templateBuffer.size(), NULL, NULL))
			{
				wchar_t* errStr = LastWinAPIError();
				BasicErrorMessage({ L"Error", L"Error writing to template file", filename.c_str(), errStr, L"\x01", L"&Ok"});
				LocalFree(errStr);
			}
			CloseHandle(templateFile);
		}

		PsInfo.PanelControl(this, FCTL_UPDATEPANEL, 1, {});
		PsInfo.PanelControl(this, FCTL_REDRAWPANEL, NULL, {});
		//PsInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, this);  // TODO: implement thread that calls this from time to time
		return TRUE;
	}

	return FALSE;
}


bool HTTPclass::OpenURL(const HTTPTemplate& httpTemplate, bool edit)
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

	char* url = WideCharToMultiByte(httpTemplate.url.c_str());
	// TODO: add arguments to the url
	// TODO: handle post verb
	SCOPE_EXIT{ delete[] url; };

	ObtainHttpHeaders(url);
	const wchar_t* fileExtension;
	switch (GetHTTPContentType())
	{
	case ContentType::JSON:
		fileExtension = L".json";
		break;
	case ContentType::HTML:
		fileExtension = L".html";
		break;
	case ContentType::Other:
	default:
		fileExtension = L"";
	}

	wchar_t tempFile[MAX_PATH];
	if (!GetTempPathWithExtension(tempFile, MAX_PATH, fileExtension))
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

	CURLcode curlCode = HttpDownload(url, fileHandle);
	if (curlCode != CURLE_OK)
	{
		wchar_t* errorMessage = MultiByteToWideChar(curl_easy_strerror(curlCode));
		const wchar_t* wideUrl = httpTemplate.url.c_str();
		BasicErrorMessage({ L"HTTP error", wideUrl, errorMessage, L"\x01", L"&Ok" });
		delete[] errorMessage;
		return false;
	}

	CloseHandle(fileHandle);

	// open response buffer in viewer/editor
	if (edit)
		PsInfo.Editor(tempFile, tempFile, 0, 0, -1, -1, EF_NONE, 1, 1, CP_DEFAULT);
	else
		PsInfo.Viewer(tempFile, tempFile, 0, 0, -1, -1, VF_NONE, CP_DEFAULT);

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

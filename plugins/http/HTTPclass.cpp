#include "HTTP.hpp"

PluginStartupInfo PsInfo;

// send ACTL_SYNCHRO AdvControl events
// TODO: add events - use ReadDirectoryChangesW (https://gist.github.com/nickav/a57009d4fcc3b527ed0f5c9cf30618f8) to monitor for changes

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
		VK_F5, 0, MInfo,
		VK_F2, SHIFT_PRESSED, MGET,

		VK_F1, 0, NULL,
		VK_F1, SHIFT_PRESSED, NULL,
		VK_F3, SHIFT_PRESSED, NULL,
		VK_F5, SHIFT_PRESSED, NULL,
		VK_F6, SHIFT_PRESSED, NULL,
		VK_F8, SHIFT_PRESSED, NULL,
		VK_F5, LEFT_ALT_PRESSED, NULL,
		VK_F6, LEFT_ALT_PRESSED, NULL,
		VK_F6, 0, NULL,
		VK_F7, 0, NULL,
		VK_F8, 0, NULL,
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
		wchar_t templatesPath[MAX_PATH] = L"C:\\FarManager-HTTP";
		const wchar_t* boxTitle = L"Templates Path";
		const wchar_t* boxSubTitle = L"Where will HTTP templates be stored?";

		for (;;)
		{
			PsInfo.InputBox(&MainGuid, &InputBoxGuid, boxTitle, boxSubTitle, boxTitle, templatesPath, templatesPath, MAX_PATH, {}, FIB_BUTTONS);

			std::error_code ec;
			if (std::filesystem::exists(templatesPath, ec) && !ec)
				break;

			if (std::filesystem::create_directories(templatesPath, ec) && !ec)
				break;

			intptr_t btn = BasicErrorMessage({ L"Error", L"Could not create templates directory", templatesPath, L"\x01", L"&Retry", L"&Ok"}, 2);
			if (btn == 0)  // retry
				continue;
			else
			{
				PsInfo.PanelControl(this, FCTL_CLOSEPANEL, 0, nullptr);
				return false;
			}
		}

		settings.Set(0, L"TemplatesPath", templatesPath);
	}

	string downloadsPath = settings.Get(0, L"DownloadsPath", L"");

	if (downloadsPath.size() == 0)
	{
		downloadsPath = concat(templatesPath, TEXT("\\Downloads"));
		for (;;)
		{
			std::error_code ec;
			if (std::filesystem::exists(downloadsPath, ec) && !ec)
				break;

			if (std::filesystem::create_directories(downloadsPath, ec) && !ec)
				break;

			intptr_t btn = BasicErrorMessage({ L"Error", L"Could not create downloads directory", downloadsPath.c_str(), L"\x01", L"&Retry", L"&Ok" }, 2);
			if (btn == 0)  // retry
				continue;
			else
			{
				PsInfo.PanelControl(this, FCTL_CLOSEPANEL, 0, nullptr);
				return false;
			}
		}

		settings.Set(0, L"DownloadsPath", downloadsPath.c_str());
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
			pp.AddedItems.erase(item.FileName);
			pp.Items.erase(pp.Items.begin() + i);
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
	if (!EnsureTemplatesPath())
		return false;
	CheckLoadedTemplates(); // remove items if they do not exist anymore
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


CURLcode HTTPclass::ObtainHttpHeaders(const HTTPTemplate& httpTemplate)
{
	std::string url = httpTemplate.GetFullUrl(curl);
	// sends a HEAD request
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

	SListPtr headers = httpTemplate.GetHeadersList();
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());

	CURLcode result = curl_easy_perform(curl);

	return result;
}


std::vector<std::pair<std::string, std::string>> HTTPclass::GetAllHeaders()
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


static int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	if (dlnow == 0)
		return 0;  // little quirk of libcurl, clientp is invalid

	HTTPclass* panel = reinterpret_cast<HTTPclass*>(clientp);

	WaitForSingleObject(panel->dldRun, INFINITE);

	panel->currentDld.dlnow = dlnow;
	panel->currentDld.dltotal = dltotal;

	if (WaitForSingleObject(panel->dldCancel, 0) == WAIT_OBJECT_0)
		return 1;  // non-zero aborts transfer
	return 0; // continue
}


template<typename InputType>
static bool isValidJSON(InputType&& i)
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


CURLcode HTTPclass::HttpDownload(const HTTPTemplate& httpTemplate, HANDLE fileHandle, const char* postdata)
{
	std::string url = httpTemplate.GetFullUrl(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fileHandle);

	string wideUrl = MultiByteToWideChar(url);
	currentDld.url = wideUrl.c_str();
	SCOPE_EXIT{ currentDld.url = nullptr; };

	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

	switch (httpTemplate.verb)
	{
	case HTTPVerb::GET:
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
		break;
	case HTTPVerb::POST:
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
		break;
	default:
		std::unreachable();
	}

	SListPtr headers = httpTemplate.GetHeadersList();
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());

	ResetEvent(dldDone);
	HANDLE progressShowThread = CreateThread({}, 0, [](void* data) -> DWORD
		{
			HTTPclass* panel = static_cast<HTTPclass*>(data);

			for (;;)
			{
				WaitForSingleObject(panel->dldRun, INFINITE);
				bool cancelled = WaitForSingleObject(panel->dldCancel, 0) == WAIT_OBJECT_0;
				bool done = WaitForSingleObject(panel->dldDone, 0) == WAIT_OBJECT_0;
				if (cancelled || done)
					break;
				panel->SendSynchroEvent(SynchroEventType::SHOW_PROGRESS);
				Sleep(100);
			}

			return 0;
		}, this, {}, {});

	CURLcode result = curl_easy_perform(curl);
	SetEvent(dldDone);

	if (progressShowThread != NULL)
	{
		WaitForSingleObject(progressShowThread, INFINITE);
		CloseHandle(progressShowThread);
	}

	if (result == CURLE_OK && GetHTTPContentType() == ContentType::JSON)
	{
		// prettify, maybe refactor later
		SetFilePointer(fileHandle, 0, 0, FILE_BEGIN);
		std::string responseBody;
		responseBody.resize(GetFileSize(fileHandle, NULL));
		DWORD read;
		if (ReadFile(fileHandle, responseBody.data(), responseBody.capacity(), &read, NULL) && read == responseBody.size())
		{
			responseBody = nlohmann::json::parse(responseBody).dump(4);
			SetFilePointer(fileHandle, 0, 0, FILE_BEGIN);
			DWORD written;
			if (WriteFile(fileHandle, responseBody.c_str(), responseBody.size(), &written, NULL) && written == responseBody.size())
				SetEndOfFile(fileHandle);
		}
	}

	return result;
}


bool HTTPclass::DeserializeTemplateFromFile(const wchar_t* filename, HTTPTemplate& httpTemplate, bool verbose)
{
	HANDLE templateFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (templateFile == INVALID_HANDLE_VALUE)
	{
		BasicErrorMessage({ L"Error", L"Error opening template file", filename, LastWinAPIError().get(), L"\x01", L"&Ok"});
		return false;
	}
	SCOPE_EXIT{ CloseHandle(templateFile); };
	DWORD bufferSize = GetFileSize(templateFile, NULL);
	std::vector<uint8_t> templateBuffer(bufferSize);
	DWORD bytesRead;
	if (!ReadFile(templateFile, templateBuffer.data(), bufferSize, &bytesRead, NULL))
	{
		if (verbose)
			BasicErrorMessage({ L"Error", L"Error reading from template file", filename, LastWinAPIError().get(), L"\x01", L"&Ok"});
		return false;
	}

	try
	{
		std::span<uint8_t> remaining = httpTemplate.Deserialize(templateBuffer);
		if (remaining.size_bytes() > 0)
			throw std::runtime_error("Residual buffer remained");
	}
	catch (std::runtime_error e)
	{
		if (verbose)
		{
			string errWide = MultiByteToWideChar(e.what());
			BasicErrorMessage({ L"Error", L"Error deserializing from template file", filename, errWide.c_str(), L"\x01", L"&Ok"});
		}
		return false;
	}
	return true;
}


void HTTPclass::DisplayInfo(const std::string& buffer)
{
	if (WaitForSingleObject(showingHeaders, 0) == WAIT_OBJECT_0)
	{
		// already showing
		return;
	}
	SetEvent(showingHeaders);
	SCOPE_EXIT{ ResetEvent(showingHeaders); };

	wchar_t headersFilepath[MAX_PATH + 1];
	headersFilepath[MAX_PATH] = TEXT('\0');
	if (!GetTempPathWithExtension(headersFilepath, MAX_PATH, TEXT(".headers")))
	{
		BasicErrorMessage({ L"Error", L"Could not reserve name for headers file", LastWinAPIError().get(), L"\x01", L"&Ok" });
		return;
	}

	{
		HANDLE hFile = CreateFile(headersFilepath, GENERIC_WRITE, FILE_SHARE_READ, {}, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, {});
		if (hFile == INVALID_HANDLE_VALUE)
		{
			BasicErrorMessage({ L"Error", L"Could not create headers file", LastWinAPIError().get(), L"\x01", L"&Ok" });
			return;
		}
		SCOPE_EXIT{ CloseHandle(hFile); };
		DWORD written;
		if (!WriteFile(hFile, buffer.c_str(), buffer.size(), &written, {}))
		{
			BasicErrorMessage({ L"Error", L"Could not reserve name for headers file", LastWinAPIError().get(), L"\x01", L"&Ok" });
			return;
		}
	}

	PsInfo.Editor(headersFilepath, headersFilepath, 0, 0, -1, -1, EF_DELETEONCLOSE, 1, 1, CP_DEFAULT);
}


int HTTPclass::ProcessEditorKey(const INPUT_RECORD* Rec)
{
	if (editorIds.find(currentlyOpenEditorId) == editorIds.end())
	{
		// not our editor
		return FALSE;
	}
	if (Rec->EventType != KEY_EVENT)
		return FALSE;

	const auto Key = Rec->Event.KeyEvent.wVirtualKeyCode;
	const auto ControlState = Rec->Event.KeyEvent.dwControlKeyState;

	const bool
		NonePressed = check_control(ControlState, none_pressed),
		OnlyAnyShiftPressed = check_control(ControlState, any_shift_pressed),
		OnlyAnyAltPressed = check_control(ControlState, any_alt_pressed);

	if (NonePressed && Key == VK_F5)
	{
		// show response headers
		DisplayInfo(editorInfoBuffers[currentlyOpenEditorId]);
		return TRUE;
	}

	return FALSE;
}


static bool IsHTTPEditor(intptr_t editorId)
{
	intptr_t requiredSize = PsInfo.EditorControl(editorId, ECTL_GETTITLE, 0, NULL);
	string title;
	title.resize(requiredSize);
	PsInfo.EditorControl(editorId, ECTL_GETTITLE, requiredSize, title.data());
	string prefixes[] = { L"http://", L"https://" };
	for (const string& prefix : prefixes)
	{
		size_t sizeToCheck = std::min(title.size(), prefix.size());
		if (wcsncmp(title.c_str(), prefix.c_str(), sizeToCheck) == 0)
			return true;
	}
	return false;
}

std::string HTTPclass::GetInfoBuffer()
{
	std::string buffer;
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	buffer += std::format("Status code: {}\n", httpCode);

	buffer += "\n--------------\n\n";

	char* destinationIp;
	curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &destinationIp);
	buffer += std::format("Destination IP: {}\n", destinationIp);

	long port;
	curl_easy_getinfo(curl, CURLINFO_PRIMARY_PORT, &port);
	buffer += std::format("Destination port: {}\n", port);

	char* effectiveUrl;
	curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
	buffer += std::format("Effective URL: {}\n", effectiveUrl);

	buffer += "\n--------------\n\n";

	for (const auto& [name, value] : GetAllHeaders())
	{
		buffer += name + ": " + value + "\n";
	}
	return buffer;
}


intptr_t HTTPclass::ProcessEditorEventW(const ProcessEditorEventInfo* Info)
{
	switch (Info->Event)
	{
	case EE_GOTFOCUS:
		{
			currentlyOpenEditorId = Info->EditorID;

			if (editorIds.find(Info->EditorID) == editorIds.end() && !IsHTTPEditor(Info->EditorID))
				break;

			KeyBarLabel kbl[1];
			kbl[0] = {
				.Key = {
					.VirtualKeyCode = VK_F5,
					.ControlKeyState = 0,
			},
			.Text = GetMsg(MInfo),
			.LongText = GetMsg(MInfo),
			};
			KeyBarTitles kbt = { ARRAYSIZE(kbl), kbl };
			FarSetKeyBarTitles barTitles = { sizeof(FarSetKeyBarTitles), &kbt };
			PsInfo.EditorControl(Info->EditorID, ECTL_SETKEYBAR, {}, &barTitles);

			std::unique_ptr<SynchroEvent> editorUpdateEvent = std::make_unique<SynchroFunctionEvent>([=](void*)
			{
				INPUT_RECORD rec{};
				rec.EventType = KEY_EVENT;
				rec.Event.KeyEvent.bKeyDown = true;
				rec.Event.KeyEvent.wVirtualKeyCode = VK_LEFT;
				PsInfo.EditorControl(currentlyOpenEditorId, ECTL_PROCESSINPUT, {}, &rec);
			});
			SendSynchroEvent(std::move(editorUpdateEvent)); // execute async
		} break;
	case EE_READ:
		{
			if (IsHTTPEditor(Info->EditorID))
			{
				editorIds.insert(Info->EditorID);
				editorInfoBuffers[Info->EditorID] = GetInfoBuffer();
			}
		} break;
	case EE_CLOSE:
		{
			if (editorIds.find(Info->EditorID) == editorIds.end())
				break;
			editorIds.erase(Info->EditorID);
			editorInfoBuffers.erase(Info->EditorID);
		} break;
	case EE_KILLFOCUS:
		{
			currentlyOpenEditorId = -1;
		} break;
	}

	return FALSE;
}


int HTTPclass::ProcessKey(const INPUT_RECORD* Rec)
{
	if (Rec->EventType != KEY_EVENT)
		return FALSE;

	bool dlding = WaitForSingleObject(dldInProgress, 0) == WAIT_OBJECT_0;

	// TODO: this doesn't work, mouse still propagates through
	if (Rec->EventType == MOUSE_EVENT)
		return dlding; // don't handle if dlding

	const auto Key = Rec->Event.KeyEvent.wVirtualKeyCode;
	const auto ControlState = Rec->Event.KeyEvent.dwControlKeyState;

	const bool
		NonePressed = check_control(ControlState, none_pressed),
		OnlyAnyShiftPressed = check_control(ControlState, any_shift_pressed),
		OnlyAnyAltPressed = check_control(ControlState, any_alt_pressed);

	if (Key == VK_ESCAPE)
	{
		if (!dlding)
			return FALSE;

		bool cancelled = WaitForSingleObject(dldCancel, 0) == WAIT_OBJECT_0;
		bool done = WaitForSingleObject(dldDone, 0) == WAIT_OBJECT_0;
		if (cancelled || done)
			return FALSE;

		std::unique_ptr<SynchroEvent> cancelDialogEvent = std::make_unique<SynchroFunctionEvent>([&](void*)
			{
				ResetEvent(dldRun);
				PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MCancelDownload, TEXT("Download_Cancel"), {}, {}, FDLG_WARNING);
				Builder.AddOKCancel(MYes, MNo);
				if (Builder.ShowDialog())
				{
					SetEvent(dldCancel);
				}
				SetEvent(dldRun);  // allow the download thread to run
			});
		SendSynchroEvent(std::move(cancelDialogEvent)); // execute async

		return TRUE;
	}

	if (dlding)
		return TRUE;  // don't handle any other event

	auto startDownload = [this](const HTTPTemplate& httpTemplate, bool edit, bool headOnly)
		{
			if (hDldThread != NULL)
			{
				// just in case
				WaitForSingleObject(hDldThread, INFINITE);
				CloseHandle(hDldThread);
			}
			SetEvent(dldInProgress);

			currentDld = { .httpTemplate = httpTemplate, .edit = edit };

			if (headOnly)
				currentDld.httpTemplate.verb = HTTPVerb::HEAD;

			hDldThread = CreateThread({}, {}, [](void* data) -> DWORD
				{
					HTTPclass* panel = reinterpret_cast<HTTPclass*>(data);
					auto& currentDld = panel->currentDld;
					panel->OpenURL(currentDld.httpTemplate, currentDld.edit);
					return 0;
				}, this, {}, {});

			if (hDldThread == NULL)
				BasicErrorMessage({ L"Error", L"Error creating download thread", LastWinAPIError().get(), L"\x01", L"&Ok" });
		};

	if (OnlyAnyShiftPressed && Key == VK_F2)
	{
		HTTPTemplate httpTemplate;
		httpTemplate.verb = HTTPVerb::GET;
		PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MEphemeralGET, TEXT("Ephemeral_GET"));
		Builder.AddText(TEXT("&URL"));
		Builder.AddEditField(httpTemplate.url, 100, TEXT("Ephemeral_GET"), false);
		Builder.AddSeparator();
		Builder.AddText(TEXT("Mode"));
		int edit = 1;
		int messageIds[] = { MView, MEdit };
		Builder.AddRadioButtons(&edit, std::size(messageIds), messageIds);
		Builder.AddOKCancel(MOk, MCancel);
		if (Builder.ShowDialog() && httpTemplate.url.size() > 0)
			startDownload(httpTemplate, static_cast<bool>(edit), false);
		return TRUE;
	}

	if (NonePressed && (Key == VK_F3 || Key == VK_F4 || Key == VK_F5))
	{
		bool edit = Key == VK_F4;
		bool headOnly = Key == VK_F5;

		if (const size_t Size = PsInfo.PanelControl(this, FCTL_GETCURRENTPANELITEM, 0, {}))
		{
			PluginPanelItem* ppi = reinterpret_cast<PluginPanelItem*>(malloc(Size));
			FarGetPluginPanelItem gpi{ sizeof(gpi), Size, ppi };
			PsInfo.PanelControl(this, FCTL_GETCURRENTPANELITEM, 0, &gpi);
			SCOPE_EXIT{ free(ppi); };

			if (wcsncmp(ppi->FileName, TEXT(".."), MAX_PATH) == 0)
				return FALSE; // not handled

			HTTPTemplate httpTemplate;
			if (!DeserializeTemplateFromFile(ppi->FileName, httpTemplate))
				return TRUE;  // event was handled

			for (auto& arg : httpTemplate.arguments)
			{
				if (arg.retention == HTTPArgumentRetention::AskEverytime)
				{
					PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MHTTPArgumentValue, TEXT("Argument_Retention"));

					Builder.AddText(TEXT("&Name"));
					Builder.AddReadonlyEditField(arg.name.c_str(), 100);

					Builder.AddSeparator();

					Builder.AddText(TEXT("&Value"));
					string historyId = concat(TEXT("Argument_"), arg.name);
					string newValue = arg.value;
					Builder.AddEditField(newValue, 100, historyId.c_str(), false);

					Builder.AddOKCancel(MOk, MCancel);

					if (Builder.ShowDialog())
					{
						arg.value = trim(string(newValue));
					}
					else
					{
						return TRUE;
					}
				}
				else if (arg.retention == HTTPArgumentRetention::Clipboard)
				{
					if (!OpenClipboard(NULL))
					{
						BasicErrorMessage({ L"Error", L"Error opening clipboard", LastWinAPIError().get(), L"\x01", L"&Ok" });
						return TRUE;
					}

					SCOPE_EXIT{ CloseClipboard(); };

					HANDLE hData = GetClipboardData(CF_UNICODETEXT);
					if (hData == NULL)
					{
						BasicErrorMessage({ L"Error", L"No text in clipboard", LastWinAPIError().get(), L"\x01", L"&Ok" });
						return TRUE;
					}

					wchar_t* clipboardText = reinterpret_cast<wchar_t*>(GlobalLock(hData));
					if (clipboardText == NULL)
					{
						BasicErrorMessage({ L"Error", L"Could not lock clipboard", LastWinAPIError().get(), L"\x01", L"&Ok" });
						return TRUE;
					}

					SCOPE_EXIT{ GlobalUnlock(hData); };

					// TODO: do something with Content-Disposition -> attachment; filename=Far.x64.3.0.6644.4772.1a4340d7d218edd01cd5bd09b2cfe011711e0125.msi
					arg.value = trim(string(clipboardText));
				}
			}

			startDownload(httpTemplate, edit, headOnly);
		}

		return TRUE;
	}

	if (Key == VK_F4 && (OnlyAnyShiftPressed || OnlyAnyAltPressed))
	{
		bool inPlaceEdit = OnlyAnyAltPressed;

		// create the template file

		intptr_t result = -1;
		int okId = 0;
		int cancelId = 1;

		HTTPTemplateDialogData templateDlgData{};
		HTTPTemplate& httpTemplate = templateDlgData.httpTemplate;
		std::vector<HTTPArgument>& arguments = httpTemplate.arguments;
		std::vector<Header>& requestHeaders = httpTemplate.requestHeaders;

		int& addArgId = templateDlgData.addArgId;
		int& editSelectedArgId = templateDlgData.editSelectedArgId;
		int& removeSelectedArgId = templateDlgData.removeSelectedArgId;
		int& removeAllArgsId = templateDlgData.removeAllArgsId;
		int& listSelectedArg = templateDlgData.listSelectedArg;

		int& addHeaderId = templateDlgData.addHeaderId;
		int& editSelectedHeaderId = templateDlgData.editSelectedHeaderId;
		int& removeSelectedHeaderId = templateDlgData.removeSelectedHeaderId;
		int& removeAllHeadersId = templateDlgData.removeAllHeadersId;
		int& listSelectedHeader = templateDlgData.listSelectedHeader;

		if (inPlaceEdit)
		{
			// load the current file into httpTemplate
			if (const size_t Size = PsInfo.PanelControl(this, FCTL_GETCURRENTPANELITEM, 0, {}))
			{
				PluginPanelItem* ppi = (PluginPanelItem*)malloc(Size);
				FarGetPluginPanelItem gpi{ sizeof(gpi), Size, ppi };
				PsInfo.PanelControl(this, FCTL_GETCURRENTPANELITEM, 0, &gpi);
				SCOPE_EXIT{ free(ppi); };

				if (wcsncmp(ppi->FileName, TEXT(".."), MAX_PATH) == 0)
					return FALSE; // not handled

				if (!DeserializeTemplateFromFile(ppi->FileName, httpTemplate))
					return TRUE;  // event was handled

				templateDlgData.filename = std::filesystem::path(ppi->FileName).filename();
			}
		}

		auto ValidArgument = [&](const HTTPArgument& argument, int argIndex = -1) -> bool
			{
				if (argument.retention != HTTPArgumentRetention::Clipboard) return true;

				for (int i = 0; i < arguments.size(); ++i)
				{
					if (i == argIndex) continue;  // same argument
					const auto& arg = arguments[i];
					if (arg.retention == HTTPArgumentRetention::Clipboard)
					{
						BasicErrorMessage({ L"Error", L"Cannot have multiple arguments with Clipboard retention", L"\x01", L"&Ok" });
						return false;
					}
				}
				return true;
			};

		do
		{
			result = HTTPTemplateDialog().ShowDialogEx(templateDlgData);
			if (result == addArgId)
			{
				// configure argument
				HTTPArgument argument{};
				if (HTTPArgumentDialog().ShowDialogEx(argument) == okId)
					if (ValidArgument(argument))
						arguments.push_back(argument);
			}
			else if (result == editSelectedArgId)
			{
				HTTPArgument argument = arguments[listSelectedArg];
				if (HTTPArgumentDialog().ShowDialogEx(argument) == okId)
					if (ValidArgument(argument, listSelectedArg))
						arguments[listSelectedArg] = argument;
			}
			else if (result == removeSelectedArgId)
			{
				if (listSelectedArg >= 0 && listSelectedArg < (int)arguments.size())
					arguments.erase(arguments.begin() + listSelectedArg);
			}
			else if (result == removeAllArgsId)
			{
				arguments.clear();
			}
			else if (result == addHeaderId)
			{
				Header requestHeader;
				if (HTTPRequestHeaderDialog().ShowDialogEx(requestHeader) == okId)
					requestHeaders.push_back(requestHeader);
			}
			else if (result == editSelectedHeaderId)
			{
				Header requestHeader = requestHeaders[listSelectedHeader];
				if (HTTPRequestHeaderDialog().ShowDialogEx(requestHeader) == okId)
					requestHeaders[listSelectedHeader] = requestHeader;
			}
			else if (result == removeSelectedHeaderId)
			{
				if (listSelectedHeader >= 0 && listSelectedHeader < (int)requestHeaders.size())
					requestHeaders.erase(requestHeaders.begin() + listSelectedHeader);
			}
			else if (result == removeAllHeadersId)
			{
				requestHeaders.clear();
			}
		}
		while (result > cancelId);

		if (result == okId)
		{
			// save the template
			PluginSettings settings(MainGuid, PsInfo.SettingsControl);
			string templatesPath = settings.Get(0, L"TemplatesPath", L"");
			string filename = concat(templatesPath, templatesPath.back() == L'\\'? L"" : L"\\", templateDlgData.filename);
			if (!IsValidTemplateExtension(filename.c_str()))
				filename = concat(filename, extension);

			std::vector<uint8_t> templateBuffer;
			httpTemplate.Serialize(templateBuffer);

			HANDLE templateFile = CreateFile(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (templateFile == INVALID_HANDLE_VALUE)
				BasicErrorMessage({ L"Error", L"Error creating template file", filename.c_str(), LastWinAPIError().get(), L"\x01", L"&Ok"});
			else
			{
				if (!WriteFile(templateFile, templateBuffer.data(), (DWORD)templateBuffer.size(), NULL, NULL))
					BasicErrorMessage({ L"Error", L"Error writing to template file", filename.c_str(), LastWinAPIError().get(), L"\x01", L"&Ok"});
				CloseHandle(templateFile);
			}
		}

		return TRUE;
	}

	return FALSE;
}


intptr_t HTTPclass::ProcessSynchroEventW(SynchroEvent* event)
{
	SCOPE_EXIT{
		if (event->heap)
			delete event;
		else
			SetEvent(synchroEventFree);
	};

	switch (event->type)
	{
	case SynchroEventType::UPDATE_PANEL:
		{
			PsInfo.PanelControl(this, FCTL_UPDATEPANEL, 1, {});
			PsInfo.PanelControl(this, FCTL_REDRAWPANEL, NULL, {});
		} break;
	case SynchroEventType::SAVE_SCREEN:
		{
			SynchroDataEvent<HANDLE>* _event = dynamic_cast<SynchroDataEvent<HANDLE>*>(event);
			HANDLE& screen = _event->arg;
			screen = PsInfo.SaveScreen(0, 0, -1, -1);
		} break;
	case SynchroEventType::RESTORE_SCREEN:
		{
			SynchroDataEvent<HANDLE>* _event = dynamic_cast<SynchroDataEvent<HANDLE>*>(event);
			HANDLE& screen = _event->arg;
			PsInfo.RestoreScreen(screen);
		} break;
	case SynchroEventType::SHOW_PROGRESS:
		{
			const auto& dlnow = currentDld.dlnow;
			const auto& dltotal = currentDld.dltotal;
			const auto& url = currentDld.url;
			if (dltotal == 0)
			{
				const wchar_t* MsgItems[]{ TEXT("Reading from URL"), url };
				PsInfo.Message(&MainGuid, &DldInfoMsg, 0, TEXT("DldInfo"), MsgItems, std::size(MsgItems), 0);
			}
			else
			{
				string sizeFormatted = std::format(TEXT("Downloaded {} / {} bytes [{:.2f}%]"), dlnow, dltotal, 100 * (float)dlnow / (float)dltotal);
				const wchar_t* MsgItems[]{ TEXT("Reading from URL"), url, sizeFormatted.c_str() };
				PsInfo.Message(&MainGuid, &ProgressMsg, 0, TEXT("DldProgress"), MsgItems, std::size(MsgItems), 0);
			}
		} break;
	case SynchroEventType::FUNCTION:
		{
			SynchroFunctionEvent* _event = dynamic_cast<SynchroFunctionEvent*>(event);
			_event->func(_event->arg);
		} break;
	default:
		std::unreachable();
	}
	return 1;
}


void HTTPclass::SendSynchroEvent(const SynchroEvent& event)
{
	WaitForSingleObject(synchroMutex, INFINITE);
	if (!event.heap)
		ResetEvent(synchroEventFree);

	PsInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, const_cast<SynchroEvent*>(&event));

	if (!event.heap)
		WaitForSingleObject(synchroEventFree, INFINITE);
	ReleaseMutex(synchroMutex);
}


void HTTPclass::SendSynchroEvent(std::unique_ptr<SynchroEvent> event)
{
	event->heap = true;
	PsInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, event.release());
}


bool HTTPclass::OpenURL(HTTPTemplate& httpTemplate, bool edit)
{
	{
		FarPanelDirectory fpd{};
		fpd.StructSize = sizeof(FarPanelDirectory);
		fpd.PluginId = MainGuid;
		string downloadsPath;
		{
			PluginSettings settings(MainGuid, PsInfo.SettingsControl);
			downloadsPath = settings.Get(0, L"DownloadsPath", nullptr);
		}
		if (downloadsPath.size() > 0)
		{
			fpd.Name = downloadsPath.c_str();
			// TODO: check how it should be done in order to set the current directory for saving the files with shift + F2
			//std::filesystem::current_path(downloadsPath);
			PsInfo.PanelControl(PANEL_ACTIVE, FCTL_SETPANELDIRECTORY, 0, &fpd);


			size_t Size = PsInfo.PanelControl(PANEL_ACTIVE, FCTL_GETPANELDIRECTORY, 0, {});
			FarPanelDirectory* Dir = reinterpret_cast<FarPanelDirectory*>(malloc(Size));
			SCOPE_EXIT{ free(Dir); };

			Dir->StructSize = sizeof(*Dir);
			PsInfo.PanelControl(PANEL_ACTIVE, FCTL_GETPANELDIRECTORY, Size, Dir);
			string ListPath = Dir->Name;
		}
	}

	ResetEvent(dldCancel);
	SCOPE_EXIT{ ResetEvent(dldInProgress); };

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

	std::string url = httpTemplate.GetFullUrl(curl);

	SynchroDataEvent<HANDLE> saveScreenEvent(SynchroEventType::SAVE_SCREEN);
	SendSynchroEvent(saveScreenEvent);

	string wideUrl = MultiByteToWideChar(url);

	SCOPE_EXIT{
		SynchroDataEvent<HANDLE>& restoreScreenEvent = saveScreenEvent;
		restoreScreenEvent.type = SynchroEventType::RESTORE_SCREEN;
		SendSynchroEvent(restoreScreenEvent);  // this restores the screen
	};

	if (httpTemplate.verb == HTTPVerb::HEAD)
	{
		SendSynchroEvent(SynchroFunctionEvent([=](void*)
			{
				ObtainHttpHeaders(httpTemplate);
				DisplayInfo(GetInfoBuffer());
			})); // execute sync
		return true;
	}

	wchar_t tempFile[MAX_PATH + 1]{};
	if (!GetTempPathWithExtension(tempFile, MAX_PATH, L""))
	{
		BasicErrorMessage({ L"Error", L"Could not reserve name for temp file", LastWinAPIError().get(), L"\x01", L"&Ok"});
		return false;
	}

	HANDLE fileHandle = CreateFile(tempFile, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		BasicErrorMessage({ L"Error", L"Could not create temp file", tempFile, LastWinAPIError().get(), L"\x01", L"&Ok"});
		return false;
	}

	// delete the temp file
	SCOPE_EXIT{
		if (fileHandle != INVALID_HANDLE_VALUE)
			CloseHandle(fileHandle);  // release it in case it wasn't
	};

	CURLcode curlCode;
	if (httpTemplate.verb == HTTPVerb::POST)
	{
		string widePostdata;

		bool dlgResult;
		SynchroFunctionEvent postdataEvent([&](void*)
			{
				PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MHTTPPostdata, TEXT("HTTP_Postdata"));
				Builder.AddEditField(widePostdata, 100, {}, false);
				Builder.AddOKCancel(MOk, MCancel);
				dlgResult = Builder.ShowDialog();
			});
		SendSynchroEvent(postdataEvent);

		if (!dlgResult)
			return false;  // cancelled

		std::string postdata = WideCharToMultiByte(widePostdata);

		if (isValidJSON(postdata))
		{
			auto& headers = httpTemplate.requestHeaders;
			Header header = { TEXT("content-type"), TEXT("application/json") };
			if (std::find(headers.begin(), headers.end(), header) == headers.end())
				httpTemplate.requestHeaders.push_back(header);
		}

		curlCode = HttpDownload(httpTemplate, fileHandle, postdata.c_str());
	}
	else
	{
		curlCode = HttpDownload(httpTemplate, fileHandle, nullptr);
	}

	if (curlCode != CURLE_OK)
	{
		if (curlCode == CURLE_ABORTED_BY_CALLBACK)
		{
			// intentional cancel
			return false;
		}
		if (WaitForSingleObject(dldCancel, 0) != WAIT_OBJECT_0)
		{
			string errorMessage = MultiByteToWideChar(curl_easy_strerror(curlCode));
			BasicErrorMessage({ L"HTTP error", wideUrl.c_str(), errorMessage.c_str(), L"\x01", L"&Ok"});
		}
		return false;
	}

	CloseHandle(fileHandle);
	fileHandle = INVALID_HANDLE_VALUE;

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

	{
		string oldName = tempFile;
		size_t len = wcslen(tempFile);
		wcscpy_s(tempFile + len, MAX_PATH - len, fileExtension);
		if (!MoveFile(oldName.c_str(), tempFile))
		{
			BasicErrorMessage({ L"HTTP error", L"Could not add extension", LastWinAPIError().get(), L"\x01", L"&Ok"});
			return false;
		}
	}

	SynchroFunctionEvent openEvent([&](void*)
		{
			// open response buffer in viewer/editor
			
			if (edit)
				PsInfo.Editor(tempFile, wideUrl.c_str(), 0, 0, -1, -1, EF_NONMODAL | EF_DELETEONCLOSE | EF_ENABLE_F6, 1, 1, CP_DEFAULT);
			else
				PsInfo.Viewer(tempFile, wideUrl.c_str(), 0, 0, -1, -1, VF_NONMODAL | VF_DELETEONCLOSE | VF_ENABLE_F6, CP_DEFAULT);
		});
	SendSynchroEvent(openEvent);

	return true;
}

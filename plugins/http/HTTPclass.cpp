#include "HTTP.hpp"

PluginStartupInfo PsInfo;

// send ACTL_SYNCHRO AdvControl events
// TODO: add events - use ReadDirectoryChangesW (https://gist.github.com/nickav/a57009d4fcc3b527ed0f5c9cf30618f8) to monitor for changes

HTTPclass::HTTPclass() {
	assert(test_StringSerializer());
	assert(test_HTTPTemplateSerializer());

	if (!curlm)  // not initialised
	{
		CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
		if (result != CURLE_OK)
		{
			BasicErrorMessage({ L"Error", L"CURL global init failed", L"\x01", L"&Ok" });
			return;
		}

		curlm = curl_multi_init();
		if (!curlm)
		{
			curl_global_cleanup();
			BasicErrorMessage({ L"Error", L"CURL multi init failed", L"\x01", L"&Ok" });
			return;
		}
	}

	hCurlThread = CreateThread({}, {}, [](void* data) -> DWORD
		{
			HTTPclass* panel = reinterpret_cast<HTTPclass*>(data);
			panel->CurlPerformDaemon();
			return 0;
		}, this, {}, {});

	if (hCurlThread == NULL)
		BasicErrorMessage({ L"Error", L"Error creating curl daemon thread", LastWinAPIError().get(), L"\x01", L"&Ok" });
}

HTTPclass::~HTTPclass() {
	// curl cleanup

	if (curlm)
	{
		curl_multi_cleanup(curlm);
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
	if (nameLen < EXTENSION_LENGTH)
		return false;
	if (_wcsicmp(templateName + nameLen - EXTENSION_LENGTH, EXTENSION) != 0)
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
		PutOneFile(templatesPath, i);
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
		FileName = concat(SrcPath, SrcPath.back() == L'\\' ? L"" : L"\\", FileName);

	FileName = FileName.substr(0, FileName.size() - EXTENSION_LENGTH);

	if (pp.AddedItems.find(FileName) != pp.AddedItems.end())
		return false;  // already added
	pp.AddedItems.insert(FileName);

	auto& NewItem = pp.Items.emplace_back(PanelItem);
	string& NewName = pp.StringData.emplace_back(FileName);

	NewItem.FileName = NewName.c_str();
	NewItem.AlternateFileName = {};  // access violation thrown if this is not set
	NewItem.Owner = pp.OwnerData.emplace_back(NullToEmpty(NewItem.Owner)).c_str();

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


void HTTPclass::CurlPerformDaemon()
{
	int lastRunningHandles = -1;
	int runningHandles;
	CURLMcode mresult;
	CURLMsg* msg;
	int msgq = 0;

	for (;;)
	{
		WaitForSingleObject(dldShouldRun, INFINITE);

		mresult = curl_multi_perform(curlm, &runningHandles);
		if (mresult != CURLM_OK)  // error
		{
			string errorMessage = MultiByteToWideChar(curl_multi_strerror(mresult));
			BasicErrorMessage({ L"CURL multi perform error", errorMessage.c_str(), L"\x01", L"&Ok" });
			return;
		}

		// poll
		if (runningHandles > 0)
		{
			mresult = curl_multi_poll(curlm, NULL, 0, 1000, NULL);
			if (mresult != CURLM_OK)  // error
			{
				string errorMessage = MultiByteToWideChar(curl_multi_strerror(mresult));
				BasicErrorMessage({ L"CURL multi poll error", errorMessage.c_str(), L"\x01", L"&Ok" });
				return;
			}
		}
		else
			ResetEvent(dldShouldRun);

		// show progress
		if (!dldShouldCancel)
			SendSynchroAction(SynchroActionType::SHOW_PROGRESS);

		if (runningHandles != lastRunningHandles)
		{
			do
			{
				msg = curl_multi_info_read(curlm, &msgq);
				if (msg && (msg->msg == CURLMSG_DONE))
				{
					DldData& dldData = downloadsInProgress[msg->easy_handle];
					SetEvent(dldData.completed);
					dldData.result = msg->data.result;
					curl_multi_remove_handle(curlm, msg->easy_handle);
				}
			}
			while (msg);
		}

		lastRunningHandles = runningHandles;
	}
	// TODO: better error handling
}


std::vector<std::pair<std::string, std::string>> HTTPclass::GetAllHeaders(CURL* curl)
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


ContentType HTTPclass::GetHTTPContentType(CURL* curl)
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
	if (dlnow == 0 || dltotal == 0)
		return 0;  // little quirk of libcurl, clientp is invalid

	CurlProgressArgument* progressArg = reinterpret_cast<CurlProgressArgument*>(clientp);
	HTTPclass* panel = progressArg->panel;
	CURL* curl = progressArg->curl;

	WaitForSingleObject(panel->downloadsMutex, INFINITE);
	if (panel->downloadsInProgress.find(curl) != panel->downloadsInProgress.end())
	{
		auto& currentDld = panel->downloadsInProgress[curl];
		currentDld.dlnow = dlnow;
		currentDld.dltotal = dltotal;
	}
	ReleaseMutex(panel->downloadsMutex);

	if (panel->dldShouldCancel)
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


bool HTTPclass::DeserializeTemplateFromFile(const wchar_t* filename, HTTPTemplate& httpTemplate, bool verbose)
{
	string Filename = filename;
	if (!IsValidTemplateExtension(Filename.c_str()))
		Filename = concat(Filename, EXTENSION);

	HANDLE templateFile = CreateFile(Filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (templateFile == INVALID_HANDLE_VALUE)
	{
		BasicErrorMessage({ L"Error", L"Error opening template file", Filename.c_str(), LastWinAPIError().get(), L"\x01", L"&Ok"});
		return false;
	}
	SCOPE_EXIT{ CloseHandle(templateFile); };
	DWORD bufferSize = GetFileSize(templateFile, NULL);
	std::vector<uint8_t> templateBuffer(bufferSize);
	DWORD bytesRead;
	if (!ReadFile(templateFile, templateBuffer.data(), bufferSize, &bytesRead, NULL))
	{
		if (verbose)
			BasicErrorMessage({ L"Error", L"Error reading from template file", Filename.c_str(), LastWinAPIError().get(), L"\x01", L"&Ok"});
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
			BasicErrorMessage({ L"Error", L"Error deserializing from template file", Filename.c_str(), errWide.c_str(), L"\x01", L"&Ok"});
		}
		return false;
	}
	httpTemplate.Filename = Filename;
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

	if (OnlyAnyShiftPressed && Key == VK_F3)
	{
		// TODO: shift + F3 to expand the selection inside quotes?
	}

	if (OnlyAnyShiftPressed && Key == VK_F4)
	{
		HTTPDialogs::OpenSelectionDialogData osdd;

		// retrieve the selected text in order to forward it as the clipboard argument
		EditorInfo editorInfo = { sizeof(EditorInfo) };
		PsInfo.EditorControl(currentlyOpenEditorId, ECTL_GETINFO, NULL, &editorInfo);
		auto CurLine = editorInfo.CurLine;
		auto TotalLines = editorInfo.TotalLines;

		std::unordered_map<std::intptr_t, int> lineNumbers = { { CurLine, 0 } };

		EditorGetString egs = { sizeof(EditorGetString) };

		while (lineNumbers.size() > 0)
		{
			// SelStart = -1 means that there is no selection
			// SelEnd = -1 means that the selection exceeds current line

			auto [StringNumber, direction] = *lineNumbers.begin();
			egs.StringNumber = StringNumber;
			lineNumbers.erase(lineNumbers.begin());

			PsInfo.EditorControl(currentlyOpenEditorId, ECTL_GETSTRING, NULL, &egs);

			if (egs.SelStart == -1 || !egs.StringText)
				continue;

			// extract line selection
			string StringText = egs.StringText;
			string LineSelection;
			if (egs.SelEnd != -1)
				LineSelection = StringText.substr(egs.SelStart, egs.SelEnd - egs.SelStart);
			else
				LineSelection = StringText.substr(egs.SelStart) + egs.StringEOL;

			// figure out where to add the line content
			if (direction == -1)
				osdd.selectedText = LineSelection + osdd.selectedText;
			else if (direction == 1 || direction == 0)
				osdd.selectedText = osdd.selectedText + LineSelection;

			// traverse adjacent lines if needed
			if ((direction == -1 || direction == 0) && StringNumber > 0)
				lineNumbers.insert({StringNumber - 1, -1});
			if ((direction == 1 || direction == 0) && StringNumber < TotalLines - 1)
				lineNumbers.insert({ StringNumber + 1, 1 });
		}

		std::deque<HTTPTemplate> httpTemplates;

		for (const auto& item : pp.Items)
		{
			HTTPTemplate httpTemplate;
			if (!DeserializeTemplateFromFile(item.FileName, httpTemplate))
				continue;

			// check if it accepts a Clipboard argument

			bool hasClipboardArg = false;
			for (auto& arg : httpTemplate.arguments)
			{
				if (arg.retention == HTTPArgumentRetention::Clipboard)
				{
					hasClipboardArg = true;
					arg.value = osdd.selectedText;
					break;
				}
			}

			if (!hasClipboardArg)
				continue;

			if (editorData[currentlyOpenEditorId] == httpTemplate.Filename)
			{
				osdd.selectedIndices.push_front(true);
				httpTemplates.push_front(httpTemplate);
				osdd.httpTemplateFilenames.push_front(item.FileName);
			}
			else
			{
				osdd.selectedIndices.push_back(false);
				httpTemplates.push_back(httpTemplate);
				osdd.httpTemplateFilenames.push_back(item.FileName);
			}
		}

		if (HTTPDialogs::OpenSelection().ShowDialogEx(osdd) == HTTPDialogs::OK_ID)
		{
			bool clipboardError = true;
			curlProgressArguments.clear();
			for (const auto& [selected, httpTemplate] : std::views::zip(osdd.selectedIndices, httpTemplates))
			{
				if (!selected)
					continue;

				if (!PrepareTemplateArguments(httpTemplate, clipboardError, true))
					continue;

				ScheduleDownload(httpTemplate, true);
			}
			WaitDownloadsWrapper();
		}

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

std::string HTTPclass::GetInfoBuffer(CURL* curl)
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

	for (const auto& [name, value] : GetAllHeaders(curl))
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
		} break;
	case EE_CLOSE:
		{
			if (editorIds.find(Info->EditorID) == editorIds.end())
				break;
			editorIds.erase(Info->EditorID);
			editorInfoBuffers.erase(Info->EditorID);
			editorData.erase(Info->EditorID);
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

	// TODO: this doesn't work, mouse still propagates through
	if (Rec->EventType == MOUSE_EVENT)
		return dldInProgress; // don't handle if dlding

	const auto Key = Rec->Event.KeyEvent.wVirtualKeyCode;
	const auto ControlState = Rec->Event.KeyEvent.dwControlKeyState;

	const bool
		NonePressed = check_control(ControlState, none_pressed),
		OnlyAnyShiftPressed = check_control(ControlState, any_shift_pressed),
		OnlyAnyAltPressed = check_control(ControlState, any_alt_pressed);

	if (Key == VK_ESCAPE)
	{
		if (!dldInProgress)
			return FALSE;

		if (dldShouldCancel)
			return FALSE;

		ResetEvent(dldShouldRun);
		PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MCancelDownload, TEXT("Download_Cancel"), {}, {}, FDLG_WARNING);
		Builder.AddOKCancel(MYes, MNo);
		if (Builder.ShowDialog())
		{
			dldShouldCancel = true;
			WaitForSingleObject(downloadsMutex, INFINITE);
			for (const auto& [curl, dldData] : downloadsInProgress)
			{
				curl_multi_remove_handle(curlm, curl);
				DldData& dldData = downloadsInProgress[curl];
				SetEvent(dldData.completed);
			}
			ReleaseMutex(downloadsMutex);
			curl_multi_wakeup(curlm);
		}
		SetEvent(dldShouldRun);  // allow the download thread to continue

		return TRUE;
	}

	if (dldInProgress)
		return TRUE;  // don't handle any other event

	if (OnlyAnyShiftPressed && Key == VK_F2)
	{
		curlProgressArguments.clear();
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
			ScheduleDownload(httpTemplate, edit);

		WaitDownloadsWrapper();
		return TRUE;
	}

	if (NonePressed && (Key == VK_F3 || Key == VK_F4 || Key == VK_F5))
	{
		curlProgressArguments.clear();
		bool edit = Key == VK_F4;
		bool headOnly = Key == VK_F5;

		bool skipClipboard = false;

		PanelInfo panelInfo = { sizeof(PanelInfo) };
		PsInfo.PanelControl(this, FCTL_GETPANELINFO, 0, &panelInfo);

		// TODO: multiselect doesn't work
		for (size_t itemNumber = 0; itemNumber < panelInfo.SelectedItemsNumber; ++itemNumber)
		{
			if (const size_t Size = PsInfo.PanelControl(this, FCTL_GETSELECTEDPANELITEM, itemNumber, {}))
			{
				PluginPanelItem* ppi = reinterpret_cast<PluginPanelItem*>(malloc(Size));
				SCOPE_EXIT{ free(ppi); };

				FarGetPluginPanelItem gpi{ sizeof(gpi), Size, ppi };
				PsInfo.PanelControl(this, FCTL_GETSELECTEDPANELITEM, itemNumber, &gpi);

				if (wcsncmp(ppi->FileName, TEXT(".."), MAX_PATH) == 0)
					continue;

				HTTPTemplate httpTemplate;
				if (!DeserializeTemplateFromFile(ppi->FileName, httpTemplate))
					continue;

				if (!PrepareTemplateArguments(httpTemplate, skipClipboard, false))
					continue;

				if (Key == VK_F5)
					httpTemplate.verb = HTTPVerb::HEAD;

				ScheduleDownload(httpTemplate, edit);
			}
		}

		WaitDownloadsWrapper();
		return TRUE;
	}

	if (Key == VK_F4 && (OnlyAnyShiftPressed || OnlyAnyAltPressed))
	{
		bool inPlaceEdit = OnlyAnyAltPressed;

		// create the template file

		intptr_t result = -1;

		HTTPDialogs::TemplateDialogData templateDlgData{};
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
			result = HTTPDialogs::Template().ShowDialogEx(templateDlgData);
			if (result == addArgId)
			{
				// configure argument
				HTTPArgument argument{};
				if (HTTPDialogs::Argument().ShowDialogEx(argument) == HTTPDialogs::OK_ID)
					if (ValidArgument(argument))
						arguments.push_back(argument);
			}
			else if (result == editSelectedArgId)
			{
				if (listSelectedArg == -1)
					BasicErrorMessage({ L"Error", L"No argument was selected", L"\x01", L"&Ok" });
				else if (listSelectedArg >= 0 && listSelectedArg < (int)arguments.size())
				{
					HTTPArgument argument = arguments[listSelectedArg];
					if (HTTPDialogs::Argument().ShowDialogEx(argument) == HTTPDialogs::OK_ID)
						if (ValidArgument(argument, listSelectedArg))
							arguments[listSelectedArg] = argument;
				}
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
				if (HTTPDialogs::RequestHeader().ShowDialogEx(requestHeader) == HTTPDialogs::OK_ID)
					requestHeaders.push_back(requestHeader);
			}
			else if (result == editSelectedHeaderId)
			{
				if (listSelectedHeader == -1)
					BasicErrorMessage({ L"Error", L"No header was selected", L"\x01", L"&Ok" });
				else if (listSelectedHeader >= 0 && listSelectedHeader < (int)requestHeaders.size())
				{
					Header requestHeader = requestHeaders[listSelectedHeader];
					if (HTTPDialogs::RequestHeader().ShowDialogEx(requestHeader) == HTTPDialogs::OK_ID)
						requestHeaders[listSelectedHeader] = requestHeader;
				}
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
		while (result > HTTPDialogs::CANCEL_ID);

		if (result == HTTPDialogs::OK_ID)
		{
			// save the template
			PluginSettings settings(MainGuid, PsInfo.SettingsControl);
			string templatesPath = settings.Get(0, L"TemplatesPath", L"");
			string filename = concat(templatesPath, templatesPath.back() == L'\\'? L"" : L"\\", templateDlgData.filename);
			if (!IsValidTemplateExtension(filename.c_str()))
				filename = concat(filename, EXTENSION);

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


bool HTTPclass::PrepareTemplateArguments(HTTPTemplate& httpTemplate, bool& clipboardError, bool skipClipboard)
{
	for (auto& arg : httpTemplate.arguments)
	{
		if (arg.retention == HTTPArgumentRetention::AskEverytime)
		{
			PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MHTTPArgumentValue, TEXT("Argument_Retention"));

			string fname = httpTemplate.Filename;
			if (fname.rfind(L'\\') != string::npos)
				fname = fname.substr(fname.rfind(L'\\') + 1);
			string tName = std::format(TEXT("Template Name: {}"), fname.substr(0, fname.size() - EXTENSION_LENGTH));

			Builder.AddSeparator(tName.c_str());

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
				return false;
			}
		}
		else if (arg.retention == HTTPArgumentRetention::Clipboard)
		{
			if (skipClipboard)
				continue;

			if (clipboardError)
				return false;

			if (!OpenClipboard(NULL))
			{
				BasicErrorMessage({ L"Error", L"Error opening clipboard", LastWinAPIError().get(), L"\x01", L"&Ok" });
				clipboardError = true;
				return false;
			}

			SCOPE_EXIT{ CloseClipboard(); };

			HANDLE hData = GetClipboardData(CF_UNICODETEXT);
			if (hData == NULL)
			{
				BasicErrorMessage({ L"Error", L"No text in clipboard", LastWinAPIError().get(), L"\x01", L"&Ok" });
				clipboardError = true;
				return false;
			}

			wchar_t* clipboardTextPtr = reinterpret_cast<wchar_t*>(GlobalLock(hData));
			if (clipboardTextPtr == NULL)
			{
				BasicErrorMessage({ L"Error", L"Could not lock clipboard", LastWinAPIError().get(), L"\x01", L"&Ok" });
				clipboardError = true;
				return false;
			}

			SCOPE_EXIT{ GlobalUnlock(hData); };

			// TODO: do something with Content-Disposition -> attachment; filename=Far.x64.3.0.6644.4772.1a4340d7d218edd01cd5bd09b2cfe011711e0125.msi
			arg.value = trim(string(clipboardTextPtr));
		}
	}
	return true;
}


void HTTPclass::WaitDownloadsWrapper()
{
	if (downloadsInProgress.size() > 0)
	{
		if (hWaitThread != NULL)
		{
			WaitForSingleObject(hWaitThread, INFINITE);
			CloseHandle(hWaitThread);
		}

		hWaitThread = CreateThread({}, {}, [](void* data) -> DWORD
			{
				HTTPclass* panel = reinterpret_cast<HTTPclass*>(data);
				panel->WaitDownloads();
				return 0;
			}, this, {}, {});

		if (hWaitThread == NULL)
			BasicErrorMessage({ L"Error", L"Error creating wait thread", LastWinAPIError().get(), L"\x01", L"&Ok" });
	}
}


void HTTPclass::WaitDownloads()
{
	dldShouldCancel = false;
	dldInProgress = true;
	SCOPE_EXIT{ dldInProgress = false; };
	SetEvent(dldShouldRun);

	completedDownloads.clear();

	while (downloadsInProgress.size() > 0)
	{
		std::deque<CURL*> completedHandles;
		for (auto& [curl, dldData] : downloadsInProgress)
		{
			if (WaitForSingleObject(dldData.completed, 10) == WAIT_OBJECT_0)
			{
				completedHandles.push_back(curl);
				completedDownloads.insert({ curl, dldData });

				CloseHandle(dldData.completed);
				dldData.completed = INVALID_HANDLE_VALUE;
			}
		}

		if (completedHandles.size() > 0)
		{
			WaitForSingleObject(downloadsMutex, INFINITE);
			for (const auto& curl : completedHandles)
				downloadsInProgress.erase(curl);
			ReleaseMutex(downloadsMutex);
		}
	}

	for (auto& [curl, dldData] : completedDownloads)
		ProcessResponse(curl, dldData);
}


intptr_t HTTPclass::ProcessSynchroEventW(SynchroAction* action)
{
	SCOPE_EXIT{
		if (action->heap)
			delete action;
		else
			SetEvent(synchroActionExecuted);
	};

	switch (action->type)
	{
	case SynchroActionType::UPDATE_PANEL:
		{
			PsInfo.PanelControl(this, FCTL_UPDATEPANEL, 1, {});
			PsInfo.PanelControl(this, FCTL_REDRAWPANEL, NULL, {});
		} break;
	case SynchroActionType::SAVE_SCREEN:
		{
			SynchroDataAction<HANDLE>* _action = dynamic_cast<SynchroDataAction<HANDLE>*>(action);
			HANDLE& screen = _action->arg;
			screen = PsInfo.SaveScreen(0, 0, -1, -1);
		} break;
	case SynchroActionType::RESTORE_SCREEN:
		{
			SynchroDataAction<HANDLE>* _action = dynamic_cast<SynchroDataAction<HANDLE>*>(action);
			HANDLE& screen = _action->arg;
			PsInfo.RestoreScreen(screen);
		} break;
	case SynchroActionType::SHOW_PROGRESS:
		{
			if (WaitForSingleObject(dldShouldRun, 0) != WAIT_OBJECT_0)
				break;  // discard the event

			if (downloadsInProgress.size() == 1)
			{
				const auto& currentDld = downloadsInProgress.begin()->second;
				const auto& dlnow = currentDld.dlnow;
				const auto& dltotal = currentDld.dltotal;
				if (dltotal == 0)
				{
					const wchar_t* MsgItems[]{ TEXT("Reading from URL"), currentDld.wideUrl.c_str() };
					PsInfo.Message(&MainGuid, &DldInfoMsg, 0, TEXT("DldInfo"), MsgItems, std::size(MsgItems), 0);
				}
				else
				{
					string sizeFormatted = std::format(TEXT("Downloaded {} / {} bytes [{:.2f}%]"), dlnow, dltotal, 100 * (float)dlnow / (float)dltotal);
					const wchar_t* MsgItems[]{ TEXT("Reading from URL"), currentDld.wideUrl.c_str(), sizeFormatted.c_str() };
					PsInfo.Message(&MainGuid, &ProgressMsg, 0, TEXT("DldProgress"), MsgItems, std::size(MsgItems), 0);
				}
			}
			else
			{
				// display downloads count
				string msg = std::format(L"Executing in parallel {} requests", downloadsInProgress.size());

				const wchar_t* MsgItems[]{ TEXT("Executing "), msg.c_str() };
				PsInfo.Message(&MainGuid, &DldInfoMsg, 0, TEXT("DldProgress"), MsgItems, std::size(MsgItems), 0);
			}
		} break;
	case SynchroActionType::FUNCTION:
		{
			SynchroFunctionAction* _action = dynamic_cast<SynchroFunctionAction*>(action);
			_action->func(_action->arg);
		} break;
	default:
		std::unreachable();
	}
	return 1;
}


void HTTPclass::SendSynchroAction(const SynchroAction& action)
{
	// warning: this can result in deadlock if not carefully used
	WaitForSingleObject(synchroMutex, INFINITE);
	if (!action.heap)
		ResetEvent(synchroActionExecuted);

	PsInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, const_cast<SynchroAction*>(&action));

	if (!action.heap)
		WaitForSingleObject(synchroActionExecuted, INFINITE);
	ReleaseMutex(synchroMutex);
}


void HTTPclass::SendSynchroAction(std::unique_ptr<SynchroAction> action)
{
	action->heap = true;
	PsInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, action.release());
}


bool HTTPclass::ScheduleDownload(HTTPTemplate& httpTemplate, bool edit)
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

	if (!curlm)  // not initialised
	{
		BasicErrorMessage({ L"Error", L"CURL multi was not properly initialized", L"\x01", L"&Ok" });
		return false;
	}

	CURL* curl = curl_easy_init();
	if (!curl)  // failure
	{
		BasicErrorMessage({ L"Error", L"CURL easy init failed", L"\x01", L"&Ok" });
		return false;
	}

	std::string url = httpTemplate.GetFullUrl(curl);
	string wideUrl = MultiByteToWideChar(url);

	DldData dldData = {
		.httpTemplate = httpTemplate,
		.edit = edit,
		.url = url,
		.wideUrl = wideUrl,
	};

	if (!GetTempPathWithExtension(dldData.tempFile, MAX_PATH, L""))
	{
		BasicErrorMessage({ L"Error", L"Could not reserve name for temp file", LastWinAPIError().get(), L"\x01", L"&Ok" });
		return false;
	}

	dldData.tempFileHandle = CreateFile(dldData.tempFile, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (dldData.tempFileHandle == INVALID_HANDLE_VALUE)
	{
		BasicErrorMessage({ L"Error", L"Could not create temp file", dldData.tempFile, LastWinAPIError().get(), L"\x01", L"&Ok" });
		return false;
	}

	dldData.headers = httpTemplate.GetHeadersList();
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, dldData.headers);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, dldData.tempFileHandle);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

	auto& progressArg = curlProgressArguments.emplace_back(std::make_unique<CurlProgressArgument>());
	progressArg->panel = this;
	progressArg->curl = curl;
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progressArg.get());

	switch (httpTemplate.verb)
	{
	case HTTPVerb::HEAD:
		
		// sends a HEAD request
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

		break;
	case HTTPVerb::POST:
		{
			string widePostdata;

			PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MHTTPPostdata, TEXT("HTTP_Postdata"));
			Builder.AddEditField(widePostdata, 100, {}, false);
			Builder.AddOKCancel(MOk, MCancel);
			bool dlgResult = Builder.ShowDialog();

			if (!dlgResult)
				return false;  // cancelled

			dldData.postdata = WideCharToMultiByte(widePostdata);

			if (isValidJSON(dldData.postdata))
			{
				auto& headers = httpTemplate.requestHeaders;
				Header header = { TEXT("content-type"), TEXT("application/json") };
				if (std::find(headers.begin(), headers.end(), header) == headers.end())
					httpTemplate.requestHeaders.push_back(header);
			}

			curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, dldData.postdata.c_str());
		}
		break;
	case HTTPVerb::GET:
		curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
		break;
	default:
		std::unreachable();
	}

	CURLMcode mcode = curl_multi_add_handle(curlm, curl);
	if (mcode != CURLM_OK)
	{
		string errorMessage = MultiByteToWideChar(curl_multi_strerror(mcode));
		BasicErrorMessage({ L"CURL multi error", errorMessage.c_str(), L"\x01", L"&Ok" });
		return false;
	}

	downloadsInProgress.insert({ curl, dldData });
	return true;
}


bool HTTPclass::ProcessResponse(CURL* curl, DldData& dldData)
{
	// delete the temp file
	SCOPE_EXIT{
		if (dldData.tempFileHandle != INVALID_HANDLE_VALUE)
			CloseHandle(dldData.tempFileHandle);  // release it in case it wasn't
		curl_easy_cleanup(curl);
	};

	const auto& curlCode = dldData.result;
	if (curlCode != CURLE_OK)
	{
		if (curlCode != CURLE_ABORTED_BY_CALLBACK && !dldShouldCancel)
		{
			string errorMessage = MultiByteToWideChar(curl_easy_strerror(curlCode));
			BasicErrorMessage({ L"HTTP error", dldData.wideUrl.c_str(), errorMessage.c_str(), L"\x01", L"&Ok" });
		}
		return false;
	}

	if (dldData.httpTemplate.verb == HTTPVerb::HEAD)
	{
		SendSynchroAction(SynchroFunctionAction([=](void*)
			{
				DisplayInfo(GetInfoBuffer(curl));
			})); // execute sync
		return true;
	}

	string fileExtension;
	switch (GetHTTPContentType(curl))
	{
	case ContentType::JSON:
		fileExtension = L".json";
		{
			// prettify
			SetFilePointer(dldData.tempFileHandle, 0, 0, FILE_BEGIN);
			std::string responseBody;
			responseBody.resize(GetFileSize(dldData.tempFileHandle, NULL));
			DWORD read;
			if (ReadFile(dldData.tempFileHandle, responseBody.data(), responseBody.capacity(), &read, NULL) && read == responseBody.size())
			{
				try
				{
					responseBody = nlohmann::ordered_json::parse(responseBody).dump(4);
				}
				catch (const nlohmann::ordered_json::parse_error& e) {}
				SetFilePointer(dldData.tempFileHandle, 0, 0, FILE_BEGIN);
				DWORD written;
				if (WriteFile(dldData.tempFileHandle, responseBody.c_str(), responseBody.size(), &written, NULL) && written == responseBody.size())
					SetEndOfFile(dldData.tempFileHandle);
			}
		}
		break;
	case ContentType::HTML:
		fileExtension = L".html";
		break;
	case ContentType::Other:
	default:
		fileExtension = L"";
	}

	CloseHandle(dldData.tempFileHandle);
	dldData.tempFileHandle = INVALID_HANDLE_VALUE;

	if (fileExtension.size() > 0)
	{
		string oldName = dldData.tempFile;
		size_t len = wcslen(dldData.tempFile);
		wcscpy_s(dldData.tempFile + len, MAX_PATH - len, fileExtension.c_str());
		if (!MoveFile(oldName.c_str(), dldData.tempFile))
		{
			BasicErrorMessage({ L"HTTP error", L"Could not add extension", LastWinAPIError().get(), L"\x01", L"&Ok"});
			return false;
		}
	}

	SendSynchroAction(SynchroFunctionAction([&](void*)
		{
			// open response buffer in viewer/editor
			if (dldData.edit)
			{
				PsInfo.Editor(dldData.tempFile, dldData.wideUrl.c_str(), 0, 0, -1, -1, EF_NONMODAL | EF_DELETEONCLOSE | EF_ENABLE_F6 | EF_IMMEDIATERETURN, 1, 1, CP_DEFAULT);
				EditorInfo editorInfo = { sizeof(EditorInfo) };
				PsInfo.EditorControl(CURRENT_EDITOR, ECTL_GETINFO, {}, &editorInfo);
				editorIds.insert(editorInfo.EditorID);
				editorInfoBuffers[editorInfo.EditorID] = GetInfoBuffer(curl);
				editorData[editorInfo.EditorID] = dldData.httpTemplate.Filename;

				KeyBarLabel kbl[2];
				kbl[0] = {
					.Key = {
						.VirtualKeyCode = VK_F5,
						.ControlKeyState = 0,
				},
				.Text = GetMsg(MInfo),
				.LongText = GetMsg(MInfo),
				};
				kbl[1] = {
					.Key = {
						.VirtualKeyCode = VK_F4,
						.ControlKeyState = SHIFT_PRESSED,
				},
				.Text = GetMsg(MRequest),
				.LongText = GetMsg(MRequest),
				};
				KeyBarTitles kbt = { ARRAYSIZE(kbl), kbl };
				FarSetKeyBarTitles barTitles = { sizeof(FarSetKeyBarTitles), &kbt };
				PsInfo.EditorControl(editorInfo.EditorID, ECTL_SETKEYBAR, {}, &barTitles);
			}
			else
			{
				// TODO: now immediate return works!!!
				PsInfo.Viewer(dldData.tempFile, dldData.wideUrl.c_str(), 0, 0, -1, -1, VF_NONMODAL | VF_DELETEONCLOSE | VF_ENABLE_F6 | VF_IMMEDIATERETURN, CP_DEFAULT);
			}
		}));

	return true;
}

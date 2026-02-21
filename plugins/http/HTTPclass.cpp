#include "HTTP.hpp"

PluginStartupInfo PsInfo;

// send ACTL_SYNCHRO AdvControl events
DWORD ThreadFunc(LPVOID classPtr)
{
	// TODO: add events - use ReadDirectoryChangesW (https://gist.github.com/nickav/a57009d4fcc3b527ed0f5c9cf30618f8)
	for (;;)
	{
		PsInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, classPtr);
		Sleep(1000);
	}
	return 0;
}

HTTPclass::HTTPclass() {
	assert(test_StringSerializer());
	assert(test_HTTPTemplateSerializer());

	// TODO: store this handle somewhere
	// TODO: convert to functional style
	//HANDLE hThread = CreateThread(NULL, 0, ThreadFunc, this, 0, NULL);
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
	curl_easy_setopt(curl, CURLOPT_URL, url);
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


// TODO: display the keybar for the editor


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

	// TODO: make it possible to specify only a rvalue for an event, that way it doesn't have to be blocking
	HTTPclass* panel = reinterpret_cast<HTTPclass*>(clientp);

	WaitForSingleObject(panel->dldRun, INFINITE);

	panel->currentDld.dlnow = dlnow;
	panel->currentDld.dltotal = dltotal;
	SynchroEvent event(SynchroEventType::SHOW_PROGRESS);
	panel->SendSynchroEvent(&event);

	if (WaitForSingleObject(panel->dldCancel, 0) == WAIT_OBJECT_0)
	{
		return 1;  // non-zero aborts transfer
	}
	return 0; // continue
}


CURLcode HTTPclass::HttpDownload(const HTTPTemplate& httpTemplate, HANDLE fileHandle, const char* postdata)
{
	std::string url = httpTemplate.GetFullUrl(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fileHandle);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);

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

	CURLcode result = curl_easy_perform(curl);
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


int HTTPclass::ProcessEditorKey(const INPUT_RECORD* Rec)
{
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

		if (WaitForSingleObject(showingHeaders, 0) == WAIT_OBJECT_0)
		{
			// already showing
			return TRUE;
		}
		SetEvent(showingHeaders);
		SCOPE_EXIT{ ResetEvent(showingHeaders); };

		wchar_t headersFilepath[MAX_PATH + 1];
		headersFilepath[MAX_PATH] = TEXT('\0');
		if (!GetTempPathWithExtension(headersFilepath, MAX_PATH, TEXT(".headers")))
		{
			BasicErrorMessage({ L"Error", L"Could not reserve name for headers file", LastWinAPIError().get(), L"\x01", L"&Ok"});
			return TRUE;
		}

		std::string buffer;
		for (const auto& [name, value] : GetAllHeaders())
		{
			buffer += name + " -> " + value + "\n\n";
		}

		{
			HANDLE hFile = CreateFile(headersFilepath, GENERIC_WRITE, FILE_SHARE_READ, {}, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, {});
			if (hFile == INVALID_HANDLE_VALUE)
			{
				BasicErrorMessage({ L"Error", L"Could not create headers file", LastWinAPIError().get(), L"\x01", L"&Ok"});
				return TRUE;
			}
			SCOPE_EXIT{ CloseHandle(hFile); };
			DWORD written;
			if (!WriteFile(hFile, buffer.c_str(), buffer.size(), &written, {}))
			{
				BasicErrorMessage({ L"Error", L"Could not reserve name for headers file", LastWinAPIError().get(), L"\x01", L"&Ok"});
				return TRUE;
			}
		}

		PsInfo.Editor(headersFilepath, headersFilepath, 0, 0, -1, -1, EF_DELETEONCLOSE, 1, 1, CP_DEFAULT);

		return TRUE;
	}

	return FALSE;
}


int HTTPclass::ProcessKey(const INPUT_RECORD* Rec)
{
	if (Rec->EventType != KEY_EVENT)
		return FALSE;

	const auto Key = Rec->Event.KeyEvent.wVirtualKeyCode;
	const auto ControlState = Rec->Event.KeyEvent.dwControlKeyState;

	const bool
		NonePressed = check_control(ControlState, none_pressed),
		OnlyAnyShiftPressed = check_control(ControlState, any_shift_pressed),
		OnlyAnyAltPressed = check_control(ControlState, any_alt_pressed);

	bool dlding = WaitForSingleObject(dldInProgress, 0) == WAIT_OBJECT_0;

	if (Key == VK_ESCAPE)
	{
		if (!dlding)
			return FALSE;

		auto cancelDialog = [](void* data) -> DWORD
			{
				HTTPclass* panel = reinterpret_cast<HTTPclass*>(data);
				SynchroFunctionEvent cancelDialogEvent([&](void*)
					{
						ResetEvent(panel->dldRun);
						PluginDialogBuilder Builder(PsInfo, MainGuid, ConfigDialogGuid, MCancelDownload, TEXT("Download_Cancel"), {}, {}, FDLG_WARNING);
						Builder.AddOKCancel(MYes, MNo);
						if (Builder.ShowDialog())
						{
							SetEvent(panel->dldCancel);
						}
						SetEvent(panel->dldRun);  // allow the download thread to run
					});
				panel->SendSynchroEvent(&cancelDialogEvent);

				return 0;
			};

		// TODO: save this
		CreateThread({}, {}, cancelDialog, this, {}, {});
		return TRUE;
	}

	if (dlding)
		return TRUE;  // don't handle any other event

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
						arg.value = newValue;
					}
					else
					{
						return TRUE;
					}
				}
			}

			auto func = [](void* data) -> DWORD {
				HTTPclass* panel = reinterpret_cast<HTTPclass*>(data);
				const auto& dldData = panel->currentDld;
				panel->OpenURL(dldData.httpTemplate, dldData.edit);
				return 0;
				};

			// TODO: don't leave this dangling
			currentDld = { .httpTemplate = httpTemplate, .edit = edit };
			hDldThread = CreateThread(NULL, 0, func, this, 0, NULL);
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

		do
		{
			result = HTTPTemplateDialog().ShowDialogEx(templateDlgData);
			if (result == addArgId)
			{
				// configure argument
				HTTPArgument argument{};
				if (HTTPArgumentDialog().ShowDialogEx(argument) == okId)
					arguments.push_back(argument);
			}
			else if (result == editSelectedArgId)
			{
				HTTPArgument argument = arguments[listSelectedArg];
				if (HTTPArgumentDialog().ShowDialogEx(argument) == okId)
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
					arguments.erase(arguments.begin() + listSelectedHeader);
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

	// TODO: display status code in some way
	// Have a general info key, which also displays other stuff?

	return FALSE;
}


intptr_t HTTPclass::ProcessSynchroEventW(SynchroEvent* event)
{
	SCOPE_EXIT{ SetEvent(synchroEventFree); };
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
			string sizeFormatted = std::format(TEXT("Downloaded {} / {} bytes [{:.2f}%]"), dlnow, dltotal, 100 * (float)dlnow / (float)dltotal);
			const wchar_t* MsgItems[]{ TEXT("Reading from URL"), url, sizeFormatted.c_str() };
			PsInfo.Message(&MainGuid, &ProgressMsg, 0, TEXT("DldProgress"), MsgItems, std::size(MsgItems), 0);
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


void HTTPclass::SendSynchroEvent(SynchroEvent* event, bool block)
{
	if (block)
	{
		WaitForSingleObject(synchroMutex, INFINITE);
		ResetEvent(synchroEventFree);
	}
	PsInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, event);
	if (block)
	{
		WaitForSingleObject(synchroEventFree, INFINITE);
		ReleaseMutex(synchroMutex);
	}
}


bool HTTPclass::OpenURL(const HTTPTemplate& httpTemplate, bool edit)
{
	ResetEvent(dldCancel);
	SetEvent(dldInProgress);
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

	// TODO: implement fallback for HEAD method not being available

	SynchroDataEvent<HANDLE> saveScreenEvent(SynchroEventType::SAVE_SCREEN);
	SendSynchroEvent(&saveScreenEvent);

	string wideUrl = MultiByteToWideChar(url);

	SynchroFunctionEvent displayMsgEvent([&](void*)
		{
			const wchar_t* MsgItems[]{ TEXT("Reading from URL"), wideUrl.c_str() };
			PsInfo.Message(&MainGuid, &DldInfoMsg, 0, TEXT("DldInfo"), MsgItems, std::size(MsgItems), 0);
		});
	SendSynchroEvent(&displayMsgEvent);

	SCOPE_EXIT{
		SynchroDataEvent<HANDLE>& restoreScreenEvent = saveScreenEvent;
		restoreScreenEvent.type = SynchroEventType::RESTORE_SCREEN;
		SendSynchroEvent(&restoreScreenEvent);  // this restores the screen
	};

	ObtainHttpHeaders(httpTemplate);
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

	wchar_t tempFile[MAX_PATH + 1];
	tempFile[MAX_PATH] = TEXT('\0');
	if (!GetTempPathWithExtension(tempFile, MAX_PATH, fileExtension))
	{
		BasicErrorMessage({ L"Error", L"Could not reserve name for temp file", LastWinAPIError().get(), L"\x01", L"&Ok"});
		return false;
	}

	HANDLE fileHandle = CreateFile(tempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		BasicErrorMessage({ L"Error", L"Could not create temp file", tempFile, LastWinAPIError().get(), L"\x01", L"&Ok"});
		return false;
	}

	// delete the temp file
	SCOPE_EXIT{
		if (fileHandle != INVALID_HANDLE_VALUE)
			CloseHandle(fileHandle);  // release it in case it wasn't

		bool retryDelete = true;
		while (retryDelete)
		{
			if (!DeleteFileW(tempFile))
			{
				DWORD errorCode = GetLastError();
				if (errorCode == ERROR_FILE_NOT_FOUND)
					break;
				intptr_t choice = BasicErrorMessage({ L"Error", L"Could not delete temp file", tempFile, LastWinAPIError().get(), L"\x01", L"&Retry", L"&Ignore" }, 2);
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
		SendSynchroEvent(&postdataEvent);

		if (!dlgResult)
			return false;  // cancelled

		std::string postdata = WideCharToMultiByte(widePostdata);
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
		string errorMessage = MultiByteToWideChar(curl_easy_strerror(curlCode));
		BasicErrorMessage({ L"HTTP error", wideUrl.c_str(), errorMessage.c_str(), L"\x01", L"&Ok"});
		return false;
	}

	CloseHandle(fileHandle);
	fileHandle = INVALID_HANDLE_VALUE;

	SynchroFunctionEvent openEvent([&](void*)
		{
			// open response buffer in viewer/editor
			if (edit)
				PsInfo.Editor(tempFile, tempFile, 0, 0, -1, -1, EF_NONE, 1, 1, CP_DEFAULT);
			else
				PsInfo.Viewer(tempFile, tempFile, 0, 0, -1, -1, VF_NONE, CP_DEFAULT);
		});
	SendSynchroEvent(&openEvent);

	return true;
}

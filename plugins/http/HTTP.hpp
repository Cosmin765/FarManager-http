#include <plugin.hpp>
#include <initguid.h>
#include <string_utils.hpp>
#include <DlgBuilder.hpp>
#include <PluginSettings.hpp>
#include <scope_exit.hpp>

#include "curl/curl.h"

#include "guid.hpp"
#include "version.hpp"
#include "HTTPLng.hpp"
#include "structs.hpp"
#include "SynchroEvents.hpp"

#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <utility>
#include <format>
#include <filesystem>
#include <assert.h>
#include <unordered_set>

const wchar_t* GetMsg(int MsgId);

using string = std::wstring;
using string_view = std::wstring_view;

extern PluginStartupInfo PsInfo;
extern HANDLE PanelHandle;

struct PluginPanel
{
	void clear()
	{
		Items.clear();
		StringData.clear();
		OwnerData.clear();
	}

	std::vector<PluginPanelItem> Items;
	// Lists for stable item addresses
	std::list<string> StringData;
	std::list<string> OwnerData;
	std::unordered_set<string> AddedItems;
};

struct HTTPTemplateDialogData
{
	IN OUT HTTPTemplate httpTemplate;
	IN OUT string filename;
	IN OUT int listSelectedArgument = 0;
	OUT int addArgumentId;
	OUT int editSelectedId;
	OUT int removeSelectedId;
	OUT int removeAllArgumentsId;
};

class HTTPTemplateDialog : public PluginDialogBuilder
{
public:
	HTTPTemplateDialog();

	intptr_t ShowDialogEx(HTTPTemplateDialogData& data);
};

class HTTPArgumentDialog: public PluginDialogBuilder
{
public:
	HTTPArgumentDialog();

	intptr_t ShowDialogEx(
		IN OUT HTTPArgument& argument
	);
};

struct DldThreadData
{
	HTTPTemplate httpTemplate;
	bool edit = false;
	wchar_t* url = nullptr;
	curl_off_t dlnow = 0;
	curl_off_t dltotal = 0;
};

class HTTPclass
{
public:
	HTTPclass();
	~HTTPclass();

	// Exports

	void GetOpenPanelInfo(OpenPanelInfo* info);
	int GetFindData(PluginPanelItem*& pPanelItem, size_t& pItemsNumber, const OPERATION_MODES OpMode);
	bool PutFiles(std::span<const PluginPanelItem> Files, const wchar_t* SrcPath, OPERATION_MODES OpMode);
	int ProcessKey(const INPUT_RECORD* rec);
	intptr_t ProcessSynchroEventW(SynchroEvent* event);
	void SendSynchroEvent(SynchroEvent* event, bool block = true);

private:
	// Internals

	void CheckLoadedTemplates();
	bool EnsureTemplatesPath();
	bool IsValidTemplate(const PluginPanelItem& item, bool verbose);
	bool IsValidTemplateExtension(const wchar_t* templateName);
	bool DeserializeTemplateFromFile(const wchar_t* filename, HTTPTemplate& httpTemplate, bool verbose = true);
	bool LoadTemplateItems();
	bool PutOneFile(const string& srcPath, const PluginPanelItem& panelItem);

	// sends the HEAD request for gathering the HTTP headers from the server
	CURLcode ObtainHttpHeaders(const char* url);
	// returns a vector of pairs of all the headers
	// useful for displaying but computationally expensive
	std::vector<std::pair<std::string, std::string>> GetAllHeaders();
	// obtains the value for the content-type header
	// a call to ObtainHttpHeaders needs to be made before calling this function
	ContentType GetHTTPContentType();
	// performs a GET request and saves the body to a specified file
	CURLcode HttpDownload(const char* url, HANDLE fileHandle, HTTPVerb verb, const char* postdata);
	bool OpenURL(const HTTPTemplate& httpTemplate, bool edit = false);

public:
	DldThreadData currentDld;
	HANDLE dldCancel = CreateEvent({}, TRUE, FALSE, {});
	HANDLE dldRun = CreateEvent({}, TRUE, TRUE, {});

private:
	PluginPanel pp;
	CURL* curl = nullptr;
	HANDLE hDldThread = INVALID_HANDLE_VALUE;
	HANDLE synchroEventFree = CreateEvent({}, TRUE, TRUE, {});
	HANDLE synchroMutex = CreateMutex({}, FALSE, {});
	HANDLE dldInProgress = CreateEvent({}, TRUE, FALSE, {});

	static constexpr wchar_t extension[] = L".htmpl";
};

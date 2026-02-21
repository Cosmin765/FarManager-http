#include "headers.hpp"
#include "guid.hpp"
#include "version.hpp"
#include "HTTPLng.hpp"
#include "structs.hpp"
#include "SynchroEvents.hpp"
#include "local_util.hpp"
#include "dialogs.hpp"

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

struct DldThreadData
{
	HTTPTemplate httpTemplate;
	bool edit = false;
	const wchar_t* url = nullptr;
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
	int ProcessKey(const INPUT_RECORD* Rec);
	int ProcessEditorKey(const INPUT_RECORD* Rec);
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
	CURLcode ObtainHttpHeaders(const HTTPTemplate& httpTemplate);
	// returns a vector of pairs of all the headers
	// useful for displaying but computationally expensive
	std::vector<std::pair<std::string, std::string>> GetAllHeaders();
	// obtains the value for the content-type header
	// a call to ObtainHttpHeaders needs to be made before calling this function
	ContentType GetHTTPContentType();
	// performs a GET request and saves the body to a specified file
	CURLcode HttpDownload(const HTTPTemplate& httpTemplate, HANDLE fileHandle, const char* postdata);
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
	HANDLE showingHeaders = CreateEvent({}, TRUE, FALSE, {});

	static constexpr wchar_t extension[] = L".htmpl";
};

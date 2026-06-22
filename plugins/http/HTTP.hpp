#include "headers.hpp"
#include "guid.hpp"
#include "version.hpp"
#include "HTTPLng.hpp"
#include "structs.hpp"
#include "SynchroActions.hpp"
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
	std::string GetInfoBuffer();
	void DisplayInfo(const std::string& buffer);
	int ProcessEditorKey(const INPUT_RECORD* Rec);
	intptr_t ProcessSynchroEventW(SynchroAction* event);
	intptr_t ProcessEditorEventW(const ProcessEditorEventInfo* Info);

	// send blocking event to synchro
	void SendSynchroAction(const SynchroAction& event);
	// send async event to synchro
	void SendSynchroAction(std::unique_ptr<SynchroAction> event);

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
	bool OpenURL(HTTPTemplate& httpTemplate, bool edit = false);

public:
	DldThreadData currentDld;
	std::atomic<bool> dldShouldCancel = false;
	std::atomic<bool> curlEasyPerformInProgress = false;
	std::atomic<bool> dldInProgress = false;
	HANDLE dldShouldRun = CreateEvent({}, TRUE, TRUE, {});

private:
	PluginPanel pp;
	CURL* curl = nullptr;
	HANDLE hDldThread = NULL;
	HANDLE synchroActionExecuted = CreateEvent({}, TRUE, TRUE, {});
	HANDLE synchroMutex = CreateMutex({}, FALSE, {});
	HANDLE showingHeaders = CreateEvent({}, TRUE, FALSE, {});
	std::unordered_set<intptr_t> editorIds;
	intptr_t currentlyOpenEditorId = -1;
	std::unordered_map<intptr_t, std::string> editorInfoBuffers;

	static constexpr wchar_t extension[] = L".htmpl";
};

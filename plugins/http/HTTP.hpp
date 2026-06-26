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

struct DldData
{
	HTTPTemplate httpTemplate;
	bool edit = false;
	const std::string url;
	const string wideUrl;
	curl_slist* headers = nullptr;  // TODO: switch back to SListPtr
	std::string postdata;
	CURLcode result = CURLE_ABORTED_BY_CALLBACK;
	HANDLE completed = CreateEvent({}, TRUE, FALSE, {});
	wchar_t tempFile[MAX_PATH + 1]{};
	HANDLE tempFileHandle = INVALID_HANDLE_VALUE;
	curl_off_t dlnow = 0;
	curl_off_t dltotal = 0;
};

class HTTPclass;

struct CurlProgressArgument
{
	HTTPclass* panel = nullptr;
	CURL* curl = nullptr;
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
	std::string GetInfoBuffer(CURL* curl);
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

	// returns a vector of pairs of all the headers
	// useful for displaying but computationally expensive
	std::vector<std::pair<std::string, std::string>> GetAllHeaders(CURL* curl);
	// obtains the value for the content-type header
	// a call to ObtainHttpHeaders needs to be made before calling this function
	ContentType GetHTTPContentType(CURL* curl);

	void CurlPerformDaemon();
	bool ScheduleDownload(HTTPTemplate& httpTemplate, bool edit = false);
	void WaitDownloadsWrapper();
	void WaitDownloads();
	bool ProcessResponse(CURL* curl, DldData& dldData);
	bool PrepareTemplateArguments(HTTPTemplate& httpTemplate, bool& clipboardError, bool skipClipboard = false);

public:
	std::unordered_map<CURL*, DldData> downloadsInProgress;
	std::unordered_map<CURL*, DldData> completedDownloads;
	HANDLE downloadsMutex = CreateMutex({}, FALSE, {});
	std::atomic<bool> dldShouldCancel = false;
	std::atomic<bool> dldInProgress = false;
	std::atomic<HANDLE> dldShouldRun = CreateEvent({}, TRUE, FALSE, {});
	std::vector<std::unique_ptr<CurlProgressArgument>> curlProgressArguments;

private:
	PluginPanel pp;
	CURLM* curlm = nullptr;
	HANDLE hCurlThread = NULL;
	HANDLE hWaitThread = NULL;
	HANDLE synchroActionExecuted = CreateEvent({}, TRUE, TRUE, {});
	HANDLE synchroMutex = CreateMutex({}, FALSE, {});
	HANDLE showingHeaders = CreateEvent({}, TRUE, FALSE, {});
	std::unordered_set<intptr_t> editorIds;
	intptr_t currentlyOpenEditorId = -1;
	std::unordered_map<intptr_t, std::string> editorInfoBuffers;
	std::unordered_map<intptr_t, string> editorData;

	static constexpr wchar_t EXTENSION[] = L".htmpl";
	static constexpr size_t EXTENSION_LENGTH = std::size(EXTENSION) - 1;
};

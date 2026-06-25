#include "HTTP.hpp"

namespace HTTPDialogs
{
	Template::Template()
		: PluginDialogBuilder(PsInfo, MainGuid, ConfigDialogGuid, MTitle, TEXT("Template")) {}

	intptr_t Template::ShowDialogEx(
		IN OUT TemplateDialogData& data
	)
	{
		this->AddText(TEXT("&Name"));
		this->AddEditField(data.filename, 100, TEXT("Template_Name"), true);

		this->AddText(TEXT("&URL"));
		this->AddEditField(data.httpTemplate.url, 100, TEXT("Edit_URL"), true);

		this->AddSeparator();

		this->AddText(TEXT("&Verb"));
		int verbMessageIds[] = { MVerbGET, MVerbPOST, MVerbHEAD };
		int selectedVerb = (int)data.httpTemplate.verb;
		this->AddRadioButtons(&selectedVerb, std::size(verbMessageIds), verbMessageIds);

		this->AddSeparator();

		this->AddText(TEXT("Ar&guments"));

		std::list<string> argumentsStr;
		std::vector<const wchar_t*> argumentsStrPointers;
		argumentsStrPointers.reserve(data.httpTemplate.arguments.size());

		std::unordered_map<HTTPArgumentType, const wchar_t*> typeToStr = {
			{ HTTPArgumentType::Path, TEXT("Path") },
			{ HTTPArgumentType::Query, TEXT("Query") },
		};

		std::unordered_map<HTTPArgumentRetention, const wchar_t*> retentionToStr = {
			{ HTTPArgumentRetention::AskEverytime, TEXT("AskEverytime") },
			{ HTTPArgumentRetention::Remember, TEXT("Remember") },
			{ HTTPArgumentRetention::Clipboard, TEXT("Clipboard") },
		};

		for (const auto& arg : data.httpTemplate.arguments)
		{
			const auto& argumentStr = argumentsStr.emplace_back(
				std::format(TEXT("{} | {} | {} | {}"), arg.name, arg.value, typeToStr[arg.type], retentionToStr[arg.retention])
			);
			argumentsStrPointers.push_back(argumentStr.c_str());
		}
		data.listSelectedArg = -1;
		this->AddListBox(&data.listSelectedArg, 100, (int)std::min(argumentsStrPointers.size() + 1, (size_t)5), argumentsStrPointers.data(), argumentsStrPointers.size(), DIF_WORDWRAP);

		int argsButtonMessageIds[] = { MAdd, MEditSelected, MRemoveSelected, MRemoveAll };
		this->AddButtons(std::size(argsButtonMessageIds), argsButtonMessageIds, -1);
		data.addArgId = this->GetLastID() - 3;
		data.editSelectedArgId = this->GetLastID() - 2;
		data.removeSelectedArgId = this->GetLastID() - 1;
		data.removeAllArgsId = this->GetLastID();

		this->AddSeparator();

		this->AddText(TEXT("&Headers"));

		std::list<string> headersStr;
		std::vector<const wchar_t*> headersStrPointers;
		headersStrPointers.reserve(data.httpTemplate.requestHeaders.size());

		for (const auto& [name, value] : data.httpTemplate.requestHeaders)
		{
			const auto& headerStr = argumentsStr.emplace_back(
				std::format(TEXT("{} | {}"), name, value)
			);
			headersStrPointers.push_back(headerStr.c_str());
		}

		this->AddListBox(&data.listSelectedHeader, 100, (int)std::min(headersStrPointers.size() + 1, (size_t)5), headersStrPointers.data(), headersStrPointers.size(), DIF_WORDWRAP);

		int headersButtonMessageIds[] = { MAddS, MEditSelectedS, MRemoveSelectedS, MRemoveAllS };
		this->AddButtons(std::size(headersButtonMessageIds), headersButtonMessageIds, -1);
		data.addHeaderId = this->GetLastID() - 3;
		data.editSelectedHeaderId = this->GetLastID() - 2;
		data.removeSelectedHeaderId = this->GetLastID() - 1;
		data.removeAllHeadersId = this->GetLastID();

		this->AddOKCancel(MOk, MCancel);

		auto result = PluginDialogBuilder::ShowDialogEx();
		data.httpTemplate.verb = (HTTPVerb)selectedVerb;
		return result;
	}


	Argument::Argument()
		: PluginDialogBuilder(PsInfo, MainGuid, ConfigDialogGuid, MHTTPArgument, TEXT("Argument")) {}

	intptr_t Argument::ShowDialogEx(
		IN OUT HTTPArgument& argument
	)
	{
		this->AddText(TEXT("&Name"));
		this->AddEditField(argument.name, 100, TEXT("HTTP_Argument_Name"), true);

		this->AddSeparator();

		this->AddText(TEXT("&Value"));
		this->AddEditField(argument.value, 100, TEXT("HTTP_Argument_Value"), true);

		this->AddSeparator();

		this->AddText(TEXT("Type"));
		int argType = (int)argument.type;
		int argTypes[] = { MArgQuery, MArgPath };
		this->AddRadioButtons(&argType, std::size(argTypes), argTypes);

		this->AddSeparator();

		this->AddText(TEXT("Retention"));
		int retention = (int)argument.retention;
		int retentions[] = { MRetentionAsk, MRetentionRemember, MRetentionClipboard };
		this->AddRadioButtons(&retention, std::size(retentions), retentions);

		this->AddOKCancel(MOk, MCancel);

		auto result = PluginDialogBuilder::ShowDialogEx();
		argument.type = (HTTPArgumentType)argType;
		argument.retention = (HTTPArgumentRetention)retention;
		return result;
	}


	RequestHeader::RequestHeader()
		: PluginDialogBuilder(PsInfo, MainGuid, ConfigDialogGuid, MHTTPHeader, TEXT("RequestHeaders")) {}

	intptr_t RequestHeader::ShowDialogEx(
		IN OUT Header& requestHeader
	)
	{
		auto& [headerName, headerValue] = requestHeader;
		this->AddText(TEXT("&Name"));
		this->AddEditField(headerName, 100, TEXT("HTTP_Header_Name"), true);
	
		this->AddSeparator();

		this->AddText(TEXT("&Value"));
		this->AddEditField(headerValue, 100, TEXT("HTTP_Header_Value"), true);

		this->AddOKCancel(MOk, MCancel);

		auto res = PluginDialogBuilder::ShowDialogEx();

		std::transform(headerName.begin(), headerName.end(), headerName.begin(),
			[](unsigned char c) { return std::tolower(c); });
		std::transform(headerValue.begin(), headerValue.end(), headerValue.begin(),
			[](unsigned char c) { return std::tolower(c); });

		return res;
	}


	OpenSelection::OpenSelection()
		: PluginDialogBuilder(PsInfo, MainGuid, ConfigDialogGuid, MOpenSelection, TEXT("OpenSelection"))
	{
	}

	intptr_t OpenSelection::ShowDialogEx(
		IN OUT OpenSelectionDialogData& data
	)
	{
		this->AddText(TEXT("&Selection"));
		this->AddEditField(data.selectedText, 100, TEXT("Selected_Text"), false);

		this->AddSeparator(TEXT("Templates"));

		size_t index = 1;
		for (const auto& [filename, selected] : std::views::zip(data.httpTemplateFilenames, data.selectedIndices))
		{
			size_t slashPos = filename.rfind(L"\\");
			if (slashPos != string::npos)
				filename = filename.substr(slashPos + 1);

			if (index > 0)
			{
				filename = std::format(TEXT("&{}. {}"), index, filename);
				index = (index + 1) % 10;
			}

			this->AddCheckbox(filename.c_str() + filename.rfind(L"\\") + 1, &selected);
		}

		this->AddOKCancel(MOk, MCancel);

		auto res = PluginDialogBuilder::ShowDialogEx();
		return res;
	}
}

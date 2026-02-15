#include "HTTP.hpp"

HTTPTemplateDialog::HTTPTemplateDialog()
	: PluginDialogBuilder(PsInfo, MainGuid, ConfigDialogGuid, MTitle, TEXT("Template"))
{}


intptr_t HTTPTemplateDialog::ShowDialogEx(HTTPTemplateDialogData& data)
{
	this->AddText(TEXT("&Name"));
	this->AddEditField(data.filename, 100, TEXT("Template_Name"), true);

	this->AddText(TEXT("&URL"));
	this->AddEditField(data.httpTemplate.url, 100, TEXT("Edit_URL"), true);

	this->AddSeparator();

	this->AddText(TEXT("&Verb"));
	int verbMessageIds[] = { MVerbGET, MVerbPOST };
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
	};

	for (const auto& arg : data.httpTemplate.arguments)
	{
		const auto& argumentStr = argumentsStr.emplace_back(
			std::format(TEXT("{} | {} | {} | {}"), arg.name, arg.value, typeToStr[arg.type], retentionToStr[arg.retention])
		);
		argumentsStrPointers.push_back(argumentStr.c_str());
	}
	data.listSelectedArgument = -1;
	this->AddListBox(&data.listSelectedArgument, 100, (int)std::min(argumentsStr.size() + 1, (size_t)5), argumentsStrPointers.data(), argumentsStr.size(), DIF_NONE);

	int buttonMessageIds[] = { MAdd, MEditSelected, MRemoveSelected, MRemoveAll };
	this->AddButtons(std::size(buttonMessageIds), buttonMessageIds, -1);
	data.addArgumentId = this->GetLastID() - 3;
	data.editSelectedId = this->GetLastID() - 2;
	data.removeSelectedId = this->GetLastID() - 1;
	data.removeAllArgumentsId = this->GetLastID();

	this->AddOKCancel(MOk, MCancel);

	auto result = PluginDialogBuilder::ShowDialogEx();
	data.httpTemplate.verb = (HTTPVerb)selectedVerb;
	return result;
}


HTTPArgumentDialog::HTTPArgumentDialog()
	: PluginDialogBuilder(PsInfo, MainGuid, ConfigDialogGuid, MHTTPArgument, TEXT("Argument"))
{
}


intptr_t HTTPArgumentDialog::ShowDialogEx(
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
	int retentions[] = { MRetentionAsk, MRetentionRemember };
	this->AddRadioButtons(&retention, std::size(retentions), retentions);

	this->AddOKCancel(MOk, MCancel);

	auto result = PluginDialogBuilder::ShowDialogEx();
	argument.type = (HTTPArgumentType)argType;
	argument.retention = (HTTPArgumentRetention)retention;
	return result;
}

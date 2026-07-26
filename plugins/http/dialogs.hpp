#include "headers.hpp"

namespace HTTPDialogs
{
	constexpr intptr_t OK_ID = 0;
	constexpr intptr_t CANCEL_ID = 1;

	struct TemplateDialogData
	{
		IN OUT HTTPTemplate httpTemplate;
		IN OUT string filename;
		IN OUT int listSelectedArg = 0;
		IN OUT int listSelectedHeader = 0;

		OUT int addArgId;
		OUT int editSelectedArgId;
		OUT int removeSelectedArgId;
		OUT int removeAllArgsId;

		OUT int addHeaderId;
		OUT int editSelectedHeaderId;
		OUT int removeSelectedHeaderId;
		OUT int removeAllHeadersId;
	};

	struct OpenSelectionDialogData
	{
		string selectedText;
		std::deque<string> httpTemplateFilenames;
		std::deque<string> httpTemplateDisplayFilenames;
		std::deque<bool> selectedIndices;
	};

	class Template : public PluginDialogBuilder
	{
	public:
		Template();

		intptr_t ShowDialogEx(
			IN OUT TemplateDialogData& data
		);
	};

	class Argument : public PluginDialogBuilder
	{
	public:
		Argument();

		intptr_t ShowDialogEx(
			IN OUT HTTPArgument& argument
		);
	};

	class RequestHeader : public PluginDialogBuilder
	{
	public:
		RequestHeader();

		intptr_t ShowDialogEx(
			IN OUT Header& requestHeader
		);
	};

	class OpenSelection : public PluginDialogBuilder
	{
	public:
		OpenSelection();

		intptr_t ShowDialogEx(
			IN OUT OpenSelectionDialogData& data
		);
	};
;}

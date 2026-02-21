#include "headers.hpp"

struct HTTPTemplateDialogData
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

class HTTPTemplateDialog : public PluginDialogBuilder
{
public:
	HTTPTemplateDialog();

	intptr_t ShowDialogEx(HTTPTemplateDialogData& data);
};

class HTTPArgumentDialog : public PluginDialogBuilder
{
public:
	HTTPArgumentDialog();

	intptr_t ShowDialogEx(
		IN OUT HTTPArgument& argument
	);
};

class HTTPRequestHeaderDialog: public PluginDialogBuilder
{
public:
	HTTPRequestHeaderDialog();

	intptr_t ShowDialogEx(
		IN OUT Header& requestHeader
	);
};

#include <stdint.h>

#include <list>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <utility>
#include <stdexcept>

using string = std::wstring;
using string_view = std::wstring_view;

enum struct ContentType: uint8_t
{
	JSON,
	HTML,
	Other,
};

enum struct HTTPArgumentType: uint8_t
{
	Query,
	Path,
};

enum struct HTTPArgumentRetention: uint8_t
{
	AskEverytime,
	Remember,
};

enum struct HTTPVerb: uint8_t
{
	GET,
	POST,
};

template <typename T>
void SerializeBasicType(std::vector<uint8_t>& buffer, const T& x)
{
	const uint8_t* p = reinterpret_cast<const uint8_t*>(&x);
	for (uint8_t i : std::span(p, sizeof(T)))
		buffer.push_back(i);
}

template <typename T>
std::span<uint8_t> DeserializeBasicType(std::span<uint8_t> buffer, T& x)
{
	uint8_t* p = reinterpret_cast<uint8_t*>(&x);
	size_t typeLen = sizeof(T);
	if (buffer.size() < typeLen)
		throw std::runtime_error("End of buffer reached");

	for (uint8_t i : buffer.subspan(0, typeLen))
	{
		*p = i;
		++p;
	}
	return buffer.subspan(typeLen, buffer.size() - typeLen);
}

static void SerializeString(std::vector<uint8_t>& buffer, const string& s)
{
	SerializeBasicType(buffer, s.size());
	for (const auto& c : s)
		SerializeBasicType(buffer, c);
}

static std::span<uint8_t> DeserializeString(std::span<uint8_t> buffer, string& x)
{
	decltype(x.size()) size;
	buffer = DeserializeBasicType(buffer, size);

	x.resize(size);
	for (auto& c : x)
		buffer = DeserializeBasicType(buffer, c);

	return buffer;
}

struct HTTPArgument
{
	string name;
	string value;
	HTTPArgumentType type;
	HTTPArgumentRetention retention;

	void Serialize(std::vector<uint8_t>& buffer) const
	{
		SerializeString(buffer, name);
		SerializeString(buffer, value);
		SerializeBasicType(buffer, type);
		SerializeBasicType(buffer, retention);
	}

	std::span<uint8_t> Deserialize(std::span<uint8_t> buffer)
	{
		buffer = DeserializeString(buffer, name);
		buffer = DeserializeString(buffer, value);
		buffer = DeserializeBasicType(buffer, type);
		buffer = DeserializeBasicType(buffer, retention);
		return buffer;
	}

	bool operator==(const HTTPArgument&) const = default;
};

struct HTTPTemplate
{
	HTTPVerb verb;
	string url;
	std::vector<HTTPArgument> arguments;

	void Serialize(std::vector<uint8_t>& buffer) const
	{
		SerializeBasicType(buffer, verb);
		SerializeString(buffer, url);
		SerializeBasicType(buffer, arguments.size());
		for (const auto& arg : arguments)
			arg.Serialize(buffer);
	}

	std::span<uint8_t> Deserialize(std::span<uint8_t> buffer)
	{
		buffer = DeserializeBasicType(buffer, verb);
		buffer = DeserializeString(buffer, url);
		decltype(arguments.size()) size;
		buffer = DeserializeBasicType(buffer, size);
		arguments.resize(size);
		for (auto& arg : arguments)
			buffer = arg.Deserialize(buffer);
		return buffer;
	}

	bool operator==(const HTTPTemplate&) const = default;
};

static bool test_StringSerializer()
{
	string x = TEXT("FooBar");
	std::vector<uint8_t> buffer;
	SerializeString(buffer, x);
	string y;
	DeserializeString(buffer, y);
	return x == y;
}

static bool test_HTTPTemplateSerializer()
{
	std::vector<HTTPArgument> arguments;

	arguments.push_back(HTTPArgument{
		.name = TEXT("file"),
		.value = TEXT("sample.json"),
		.type = HTTPArgumentType::Query,
		.retention = HTTPArgumentRetention::Remember,
		});

	arguments.push_back(HTTPArgument{
		.name = TEXT("local"),
		.value = TEXT("true"),
		.type = HTTPArgumentType::Query,
		.retention = HTTPArgumentRetention::Remember,
		});

	HTTPTemplate t{
		.verb = HTTPVerb::GET,
		.url = TEXT("http://localhost:8000/sample.json"),
		.arguments = arguments,
	};

	std::vector<uint8_t> buffer;
	t.Serialize(buffer);
	HTTPTemplate other;
	other.Deserialize(buffer);
	return t == other;
}

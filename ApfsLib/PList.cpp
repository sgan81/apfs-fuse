#include <string>

#include "PList.h"

PLObject::PLObject()
{
}

PLObject::~PLObject()
{
}

const PLInteger * PLObject::toInt() const
{
	return dynamic_cast<const PLInteger *>(this);
}

const PLString * PLObject::toString() const
{
	return dynamic_cast<const PLString *>(this);
}

const PLData * PLObject::toData() const
{
	return dynamic_cast<const PLData *>(this);
}

const PLArray * PLObject::toArray() const
{
	return dynamic_cast<const PLArray *>(this);
}

const PLDict * PLObject::toDict() const
{
	return dynamic_cast<const PLDict *>(this);
}

PLInteger::PLInteger()
{
}

PLInteger::~PLInteger()
{
}

PLType PLInteger::type() const
{
	return PLType_Integer;
}

PLString::PLString()
{
}

PLString::~PLString()
{
}

PLType PLString::type() const
{
	return PLType_String;
}

PLData::PLData()
{
}

PLData::~PLData()
{
}

PLType PLData::type() const
{
	return PLType_Data;
}

PLArray::PLArray()
{
}

PLArray::~PLArray()
{
	for (std::vector<PLObject *>::iterator it = m_array.begin(); it != m_array.end(); it++)
		delete *it;
	m_array.clear();
}

PLType PLArray::type() const
{
	return PLType_Array;
}

PLObject * PLArray::get(size_t idx) const
{
	if (idx < m_array.size())
		return m_array[idx];
	else
		return nullptr;
}

PLDict::PLDict()
{
}

PLDict::~PLDict()
{
	for (std::map<std::string, PLObject*>::iterator it = m_dict.begin(); it != m_dict.end(); it++)
		delete it->second;
	m_dict.clear();
}

PLType PLDict::type() const
{
	return PLType_Dict;
}

PLObject * PLDict::get(const char * name) const
{
	std::map<std::string, PLObject *>::const_iterator it = m_dict.find(name);

	if (it != m_dict.cend())
		return it->second;
	else
		return nullptr;
}

PListXmlParser::PListXmlParser(const char * data, size_t size)
	:
	m_data(data),
	m_size(size)
{
	m_idx = 0;
}

PListXmlParser::~PListXmlParser()
{

}

PLObject * PListXmlParser::Parse()
{
	std::string name;
	std::string content;
	TagType type;
	PLObject *root = nullptr;

	while (FindTag(name, type))
	{
		if (type == TagType::Start && name == "plist")
		{
			try
			{
				root = ParseObject();
			}
			catch (PLException &ex)
			{
				delete root;
				root = nullptr;

				fprintf(stderr, "XML PList parse error: %s\n", ex.what());
			}

			break;
		}
	}

	return root;
}

PLArray * PListXmlParser::ParseArray()
{
	PLArray *arr = new PLArray();
	PLObject *obj;

	for (;;)
	{
		obj = ParseObject();

		if (obj)
			arr->m_array.push_back(obj);
		else
			break;
	}

	return arr;
}

PLDict * PListXmlParser::ParseDict()
{
	PLDict *dict = new PLDict();
	PLObject *obj;

	TagType tagtype;
	std::string tagname;
	std::string key;

	for (;;)
	{
		FindTag(tagname, tagtype);

		if (tagname == "dict" && tagtype == TagType::End)
			break;

		if (tagname != "key" || tagtype != TagType::Start)
			throw PLException("Invalid tag in dict");

		GetContent(key);

		if (key.empty())
			throw PLException("Empty key in dict");

		FindTag(tagname, tagtype);

		if (tagname != "key" || tagtype != TagType::End)
			throw PLException("Invalid tag type, expected </key>");

		obj = ParseObject();

		if (obj)
			dict->m_dict[key] = obj;
	}

	return dict;
}

PLObject * PListXmlParser::ParseObject()
{
	std::string name;
	TagType type;
	std::string content_str;
	const char *content_start;
	size_t content_size;
	PLObject *robj = nullptr;

	FindTag(name, type);

	if (type == TagType::Start)
	{
		if (name == "integer")
		{
			GetContent(content_str);
			FindTag(name, type);
			if (name != "integer" || type != TagType::End)
				throw PLException("Invalid end tag, expected </integer>.");

			PLInteger *obj = new PLInteger();
			obj->m_value = strtoll(content_str.c_str(), nullptr, 0);
			robj = obj;
		}
		else if (name == "string")
		{
			GetContent(content_str);
			FindTag(name, type);
			if (name != "string" || type != TagType::End)
				throw PLException("Invalid end tag, expected </string>.");

			PLString *obj = new PLString();
			obj->m_string = content_str;
			robj = obj;
		}
		else if (name == "data")
		{
			content_start = m_data + m_idx;
			content_size = GetContentSize();

			FindTag(name, type);
			if (name != "data" || type != TagType::End)
				throw PLException("Invalid end tag, expected </data>.");

			PLData *obj = new PLData();
			Base64Decode(obj->m_data, content_start, content_size);
			robj = obj;
		}
		else if (name == "array")
		{
			PLArray *obj = ParseArray();
			if (!obj)
				return nullptr;
			robj = obj;
		}
		else if (name == "dict")
		{
			PLDict *obj = ParseDict();
			if (!obj)
				return nullptr;
			robj = obj;
		}
		else
		{
			throw PLException("Unexpected start tag.");
		}
	}
	else if (type == TagType::Empty)
	{
		if (name == "true")
		{
			PLInteger *obj = new PLInteger();
			obj->m_value = 1;
			robj = obj;
		}
		else if (name == "false")
		{
			PLInteger *obj = new PLInteger();
			obj->m_value = 0;
			robj = obj;
		}
		else
		{
			throw PLException("Unexpected empty tag.");
		}
	}
	else if (type == TagType::End)
	{
		return nullptr;
	}

	return robj;
}

void PListXmlParser::Base64Decode(std::vector<uint8_t>& bin, const char * str, size_t size)
{
	int chcnt;
	uint32_t buf;
	size_t ip;
	char ch;
	uint32_t dec;

	bin.clear();
	bin.reserve(size * 4 / 3);

	chcnt = 0;
	buf = 0;

	for (ip = 0; ip < size; ip++)
	{
		ch = str[ip];

		if (ch >= 'A' && ch <= 'Z')
			dec = (unsigned char)ch - 'A'; // safe cast, we know that ch >= 'A'
		else if (ch >= 'a' && ch <= 'z')
			dec = (unsigned char)ch - 'a' + 0x1A; // safe cast, we know that ch >= 'a'
		else if (ch >= '0' && ch <= '9')
			dec = (unsigned char)ch - '0' + 0x34; // safe cast, we know that ch >= '0'
		else if (ch == '+')
			dec = 0x3E;
		else if (ch == '/')
			dec = 0x3F;
		else if (ch != '=')
			continue;
		else
			break;

		buf = (buf << 6) | dec;
		chcnt++;

		if (chcnt == 2)
			bin.push_back((buf >> 4) & 0xFF);
		else if (chcnt == 3)
			bin.push_back((buf >> 2) & 0xFF);
		else if (chcnt == 4)
		{
			bin.push_back((buf >> 0) & 0xFF);
			chcnt = 0;
		}
	}
}

bool PListXmlParser::FindTag(std::string & name, TagType & type)
{
	char ch;
	bool in_name = false;

	name.clear();
	type = TagType::None;

	do {
		ch = GetChar();
	} while (ch != '<' && ch != 0);

	if (ch == 0)
		return false;

	ch = GetChar();

	switch (ch)
	{
	case '?':
		type = TagType::ProcInstr;
		break;
	case '!':
		type = TagType::Doctype;
		break;
	case '/':
		type = TagType::End;
		in_name = true;
		break;
	default:
		type = TagType::Start;
		name.push_back(ch);
		in_name = true;
		break;
	}

	do
	{
		ch = GetChar();

		switch (ch)
		{
		case 0x09:
		case 0x0A:
		case 0x0D:
		case ' ':
		case '>':
			in_name = false;
			break;
		case '/':
			if (m_data[m_idx] == '>')
				type = TagType::Empty;
			in_name = false;
			break;
		}

		if (in_name)
			name.push_back(ch);
	} while (ch != '>' && ch != 0);

	return true;
}

bool PListXmlParser::GetContent(std::string & content)
{
	size_t start = m_idx;

	content.clear();

	while (m_idx < m_size && m_data[m_idx] != '<')
		m_idx++;

	if (m_idx == m_size)
		return false;

	content.assign(m_data + start, m_idx - start);

	return true;
}

size_t PListXmlParser::GetContentSize()
{
	size_t start = m_idx;

	while (m_idx < m_size && m_data[m_idx] != '<')
		m_idx++;

	if (m_idx == m_size)
		return m_idx - start;

	return m_idx - start;
}

PList::PList()
{
	m_root = nullptr;
}

PList::~PList()
{
}

bool PList::parseXML(const char * data, size_t size)
{
	(void)data;
	(void)size;

	return false;
}

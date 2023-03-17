/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2017 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <vector>
#include <map>
#include <memory>

enum PLType
{
	PLType_Integer,
	PLType_String,
	PLType_Data,
	PLType_Array,
	PLType_Dict
};

class PLInteger;
class PLString;
class PLData;
class PLArray;
class PLDict;

class PLException : public std::exception
{
public:
	PLException(const char *reason) { m_reason = reason; }

	const char *what() const noexcept override { return m_reason; }

private:
	const char *m_reason;
};

class PLObject
{
protected:
	PLObject();
public:
	virtual ~PLObject();

	virtual PLType type() const = 0;

	const PLInteger *toInt() const;
	const PLString *toString() const;
	const PLData *toData() const;
	const PLArray *toArray() const;
	const PLDict *toDict() const;
};

class PLInteger : public PLObject
{
	friend class PListXmlParser;
public:
	PLInteger();
	virtual ~PLInteger();

	PLType type() const override;

	int64_t value() const { return m_value; }

private:
	int64_t m_value;
};

class PLString : public PLObject
{
	friend class PListXmlParser;
public:
	PLString();
	virtual ~PLString();

	PLType type() const override;

	const std::string &string() const { return m_string; }

private:
	std::string m_string;
};

class PLData : public PLObject
{
	friend class PListXmlParser;
public:
	PLData();
	virtual ~PLData();

	PLType type() const override;

	const uint8_t *data() const { return m_data.data(); }
	size_t size() const { return m_data.size(); }

private:
	std::vector<uint8_t> m_data;
};

class PLArray : public PLObject
{
	friend class PListXmlParser;
public:
	PLArray();
	virtual ~PLArray();

	PLType type() const override;

	PLObject *get(size_t idx) const;
	size_t size() const { return m_array.size(); }

	const std::vector<PLObject *> &array() const { return m_array; }

private:
	std::vector<PLObject *> m_array;
};

class PLDict : public PLObject
{
	friend class PListXmlParser;
public:
	PLDict();
	virtual ~PLDict();

	PLType type() const override;

	PLObject *get(const char *name) const;

	const std::map<std::string, PLObject *> &dict() const { return m_dict; }

private:
	std::map<std::string, PLObject *> m_dict;
};

class PListXmlParser
{
	enum class TagType
	{
		None,
		Empty,
		Start,
		End,
		ProcInstr,
		Doctype
	};

public:
	PListXmlParser(const char *data, size_t size);
	~PListXmlParser();

	PLObject *Parse();

private:
	PLArray * ParseArray();
	PLDict * ParseDict();
	PLObject * ParseObject();
	void Base64Decode(std::vector<uint8_t> &bin, const char *str, size_t size);

	bool FindTag(std::string &name, TagType &type);
	bool GetContent(std::string &content);
	size_t GetContentSize();

	char GetChar()
	{
		if (m_idx < m_size)
			return m_data[m_idx++];
		else
			return 0;
	}

	const char * const m_data;
	const size_t m_size;
	size_t m_idx;
};

// Not used ... maybe later, if we need bplists ...
class PList
{
public:
	PList();
	~PList();

	bool parseXML(const char *data, size_t size);

private:
	PLObject * m_root;
};

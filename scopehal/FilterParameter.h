/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of FilterParameter
 */

#ifndef FilterParameter_h
#define FilterParameter_h

/**
	@brief An 8B/10B symbol within a pattern, used for trigger matching
 */
class T8B10BSymbol
{
public:

	enum disparity_t
	{
		POSITIVE,
		NEGATIVE,
		ANY
	} disparity;

	enum type_t
	{
		KSYMBOL,
		DSYMBOL,
		DONTCARE
	} ktype;

	T8B10BSymbol()
	: disparity(ANY)
	, ktype(DONTCARE)
	, value(0)
	{}

	T8B10BSymbol(T8B10BSymbol::disparity_t d, T8B10BSymbol::type_t t, uint8_t v)
	: disparity(d)
	, ktype(t)
	, value(v)
	{}

	uint8_t value;
};

/**
	@brief A parameter to a filter

	Parameters are used for scalar inputs, configuration settings, and generally any input a filter takes which is not
	some kind of waveform.
 */
class FilterParameter
{
public:

	/**
		@brief Types of data a parameter can store
	 */
	enum ParameterTypes
	{
		TYPE_FLOAT,			//32-bit floating point number
		TYPE_INT,			//64-bit integer
		TYPE_BOOL,			//boolean value
		TYPE_FILENAME,		//file path
		TYPE_ENUM,			//enumerated constant
		TYPE_STRING,		//arbitrary string
		TYPE_8B10B_PATTERN	//8B/10B pattern
	};

	FilterParameter(ParameterTypes type = FilterParameter::TYPE_FLOAT, Unit unit  = Unit(Unit::UNIT_FS));

	static FilterParameter UnitSelector();

	void ParseString(const std::string& str, bool useDisplayLocale = true);
	std::string ToString(bool useDisplayLocale = true) const;

	/**
		@brief Returns the value of the parameter interpreted as a boolean
	 */
	bool GetBoolVal() const
	{ return (m_intval != 0); }

	/**
		@brief Returns the value of the parameter interpreted as an integer
	 */
	int64_t GetIntVal() const
	{ return m_intval; }

	/**
		@brief Returns the value of the parameter interpreted as a floating point number
	 */
	float GetFloatVal() const
	{ return m_floatval; }

	/**
		@brief Access to the underlying pattern
	 */
	std::vector<T8B10BSymbol> Get8B10BPattern()
	{ return m_8b10bPattern; }

	/**
		@brief Returns the value of the parameter interpreted as a file path
	 */
	std::string GetFileName() const
	{ return m_string; }

	void SetBoolVal(bool b);
	void SetIntVal(int64_t i);
	void SetFloatVal(float f);
	void SetStringVal(const std::string& f);
	void SetFileName(const std::string& f);
	void Set8B10BPattern(const std::vector<T8B10BSymbol>& pattern);

	/**
		@brief Returns the type of the parameter
	 */
	ParameterTypes GetType() const
	{ return m_type; }

	/**
		@brief Returns the units of the parameter
	 */
	Unit GetUnit() const
	{ return m_unit; }

	/**
		@brief Change the units of the parameter
	 */
	void SetUnit(Unit u)
	{ m_unit = u; }

	//File filters for TYPE_FILENAME (otherwise ignored)
	std::string m_fileFilterMask;
	std::string m_fileFilterName;

	//Specifies TYPE_FILENAME is an output
	bool m_fileIsOutput;

	/**
		@brief Adds a (name, value) pair to a TYPE_ENUM parameter.
	 */
	void AddEnumValue(const std::string& name, int value)
	{
		m_forwardEnumMap[name] = value;
		m_reverseEnumMap[value] = name;
		m_enumSignal.emit();
	}

	/**
		@brief Gets a list of valid enumerated parameter names for a TYPE_ENUM parameter.
	 */
	void GetEnumValues(std::vector<std::string>& values)
	{
		for(auto it : m_forwardEnumMap)
			values.push_back(it.first);
	}

	/**
		@brief Clears the list of enumerated values for a TYPE_ENUM parameter
	 */
	void ClearEnumValues()
	{
		m_forwardEnumMap.clear();
		m_reverseEnumMap.clear();
		m_enumSignal.emit();
	}

	void Reinterpret();

	/**
		@brief Signal emitted every time the parameter's value changes
	 */
	sigc::signal<void()> signal_changed()
	{ return m_changeSignal; }

	/**
		@brief Signal emitted every time the list of enumeration values changes
	 */
	sigc::signal<void()> signal_enums_changed()
	{ return m_enumSignal; }

	/**
		@brief Marks this parameter to be hidden from the GUI.

		The most common use case for this is a derived filter class that wraps a base filter class but automatically
		calculates coefficients or other configuration based on user input. The coefficients are no longer direct user
		inputs, so they should be marked hidden to avoid confusing the user.
	 */
	void MarkHidden()
	{ m_hidden = true; }

	/**
		@brief Checks if this parameter should be hidden in the GUI.
	 */
	bool IsHidden()
	{ return m_hidden; }

	/**
		@brief Marks this parameter as read-only in the GUI.

		This is typically used for a filter to output configuration values to the user (bandwidth, resolution, etc)
		calculated from user input, while not allowing the user to override them.
	 */
	void MarkReadOnly()
	{ m_readOnly = true; }

	/**
		@brief Checks if this parameter should be read-only in the GUI.
	 */
	bool IsReadOnly()
	{ return m_readOnly; }

protected:
	ParameterTypes				m_type;

	sigc::signal<void()>		m_changeSignal;
	sigc::signal<void()>		m_enumSignal;

	Unit						m_unit;

	std::map<std::string, int>	m_forwardEnumMap;
	std::map<int, std::string>	m_reverseEnumMap;

	std::vector<T8B10BSymbol>	m_8b10bPattern;

	int64_t						m_intval;
	float						m_floatval;
	std::string					m_string;

	bool						m_hidden;
	bool						m_readOnly;
};

#endif

/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of InputConstraint
	@ingroup core
 */
#ifndef InputConstraint_h
#define InputConstraint_h

#include <type_traits>

#ifdef __GNUC__
#include <cxxabi.h>
#endif

/**
	@brief Base class for constraints on inputs

	@ingroup core
 */
class InputConstraint
{
public:
	InputConstraint(FlowGraphNode* sink);
	virtual ~InputConstraint();

	///@brief Checks if the given input satisfies the constraint
	virtual bool Check(StreamDescriptor source) =0;

	///@brief Convert this constraint to a string
	virtual std::string ToString() =0;

protected:
	FlowGraphNode* m_sink;

	std::string StreamTypeToString(Stream::StreamType type);
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief A set of constraints, all of which must be satisfied for the group to match

	@ingroup core
 */
class InputConstraintAND : public InputConstraint
{
public:
	InputConstraintAND(FlowGraphNode* sink, std::initializer_list<std::shared_ptr<InputConstraint> > constraints)
	: InputConstraint(sink)
	, m_constraints(constraints)
	{}

	//Match if all children are satisfied
	virtual bool Check(StreamDescriptor source) override
	{
		for(auto p : m_constraints)
		{
			if(!p->Check(source))
				return false;
		}
		return true;
	}

	virtual std::string ToString() override
	{
		std::string ret = "";
		for(auto p : m_constraints)
		{
			if(!ret.empty())
				ret += "\n";
			ret += std::string("• ") + p->ToString();
		}
		return ret;
	}

protected:
	std::vector< std::shared_ptr<InputConstraint> > m_constraints;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief Match if the input is of the correct stream type

	@ingroup core
 */
class InputConstraintStreamType : public InputConstraint
{
public:
	InputConstraintStreamType(FlowGraphNode* sink, Stream::StreamType stype)
		: InputConstraint(sink)
		, m_type(stype)
	{}

	virtual bool Check(StreamDescriptor source) override
	{ return (m_type == source.GetType() ); }

	virtual std::string ToString() override
	{ return std::string("Stream type is ") + StreamTypeToString(m_type); }

protected:
	Stream::StreamType m_type;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief Match if the input is of the correct stream type, and sparse

	@ingroup core
 */
class InputConstraintSparseStreamType : public InputConstraintStreamType
{
public:
	InputConstraintSparseStreamType(FlowGraphNode* sink, Stream::StreamType stype)
		: InputConstraintStreamType(sink, stype)
	{}

	virtual bool Check(StreamDescriptor source) override
	{
		//Input must be non-null and sparse
		auto pdata = dynamic_cast<SparseWaveformBase*>(source.GetData());
		if(!pdata)
			return false;

		return (m_type == source.GetType() );
	}

	virtual std::string ToString() override
	{ return std::string("Stream type is sparse ") + StreamTypeToString(m_type); }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief Match if the input is of the correct class type

	@ingroup core
 */
template<class T>
class InputConstraintWaveformType : public InputConstraint
{
public:
	InputConstraintWaveformType(FlowGraphNode* sink)
		: InputConstraint(sink)
	{}

	virtual bool Check(StreamDescriptor source) override
	{
		//Waveform type must be a waveform
		static_assert(std::is_base_of<WaveformBase, T>::value == true);

		//Input must be non-null
		auto pdata = source.GetData();
		if(!pdata)
			return false;

		//Make sure it matches what we expect
		return ( typeid(*pdata) == typeid(T) );
	}

	virtual std::string ToString() override
	{
		std::string stype;

		//separate path here needed since GCC returns mangled name
		#ifdef __GNUC__
			int status;
			auto tmp = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);
			stype = std::string(tmp);
			free(tmp);
		#else
			stype = typeid(T).name();
		#endif

		return std::string("Stream type is ") + stype;
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief Match if the input filter is of the correct class type

	@ingroup core
 */
template<class T>
class InputConstraintBlockType : public InputConstraint
{
public:
	InputConstraintBlockType(FlowGraphNode* sink)
		: InputConstraint(sink)
	{}

	virtual bool Check(StreamDescriptor source) override
	{
		//Node type must be a graph node
		static_assert(std::is_base_of<FlowGraphNode, T>::value == true);

		//Input must be non-null
		auto chan = source.m_channel;
		if(!chan)
			return false;

		//Make sure it matches what we expect
		return ( typeid(*chan) == typeid(T) );
	}

	virtual std::string ToString() override
	{
		std::string stype;

		//separate path here needed since GCC returns mangled name
		#ifdef __GNUC__
			int status;
			auto tmp = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);
			stype = std::string(tmp);
			free(tmp);
		#else
			stype = typeid(T).name();
		#endif

		return std::string("Source block type is ") + stype;
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief Match if the input is one of a list of stream types

	@ingroup core
 */
class InputConstraintStreamTypes : public InputConstraint
{
public:
	InputConstraintStreamTypes(FlowGraphNode* sink, std::initializer_list<Stream::StreamType> stypes)
		: InputConstraint(sink)
		, m_types(stypes)
	{}

	virtual bool Check(StreamDescriptor source) override
	{
		for(auto t : m_types)
		{
			if(t == source.GetType())
				return true;
		}
		return false;
	}

	virtual std::string ToString() override;

protected:
	std::vector<Stream::StreamType> m_types;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief Match if the input's X axis unit is a specific value

	@ingroup core
 */
class InputConstraintXUnit : public InputConstraint
{
public:
	InputConstraintXUnit(FlowGraphNode* sink, Unit unit)
		: InputConstraint(sink)
		, m_unit(unit)
	{}

	virtual bool Check(StreamDescriptor source) override
	{ return (m_unit == source.GetXAxisUnits() ); }

	virtual std::string ToString() override
	{ return std::string("X axis unit is ") + m_unit.ToStringLong();  }

protected:
	Unit m_unit;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief Match if the input's Y axis unit is a specific value

	@ingroup core
 */
class InputConstraintYUnit : public InputConstraint
{
public:
	InputConstraintYUnit(FlowGraphNode* sink, Unit unit)
		: InputConstraint(sink)
		, m_unit(unit)
	{}

	virtual bool Check(StreamDescriptor source) override
	{ return (m_unit == source.GetYAxisUnits() ); }

	virtual std::string ToString() override
	{ return std::string("Y axis unit is ") + m_unit.ToStringLong();  }

protected:
	Unit m_unit;
};

#endif

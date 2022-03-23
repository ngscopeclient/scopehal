/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of ExportWizard
 */

#ifndef ExportWizard_h
#define ExportWizard_h

/**
	@brief Abstract base class for an export wizard
 */
class ExportWizard : public Gtk::Assistant
{
public:
	ExportWizard(const std::vector<OscilloscopeChannel*>& channels);
	virtual ~ExportWizard();

protected:
	std::vector<OscilloscopeChannel*> m_channels;

	virtual void on_cancel();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Dynamic creation and enumeration

public:
	typedef ExportWizard* (*CreateProcType)(const std::vector<OscilloscopeChannel*>&);
	static void DoAddExportWizardClass(const std::string& name, CreateProcType proc);

	static void EnumExportWizards(std::vector<std::string>& names);
	static ExportWizard* CreateExportWizard(const std::string& name, const std::vector<OscilloscopeChannel*>& channels);

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;
};

#define EXPORT_WIZARD_INITPROC(T) \
	static ExportWizard* CreateInstance(const std::vector<OscilloscopeChannel*>& channels) \
	{ \
		return new T(channels); \
	} \
	virtual std::string GetExportWizardName() \
	{ return GetExportName(); }

#define AddExportWizardClass(T) ExportWizard::DoAddExportWizardClass(T::GetExportName(), T::CreateInstance)

#endif

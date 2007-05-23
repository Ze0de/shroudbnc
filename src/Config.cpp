/*******************************************************************************
 * shroudBNC - an object-oriented framework for IRC                            *
 * Copyright (C) 2005-2007 Gunnar Beutner                                      *
 *                                                                             *
 * This program is free software; you can redistribute it and/or               *
 * modify it under the terms of the GNU General Public License                 *
 * as published by the Free Software Foundation; either version 2              *
 * of the License, or (at your option) any later version.                      *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
 * GNU General Public License for more details.                                *
 *                                                                             *
 * You should have received a copy of the GNU General Public License           *
 * along with this program; if not, write to the Free Software                 *
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. *
 *******************************************************************************/

#include "StdAfx.h"

/**
 * CConfigFile
 *
 * Constructs a new configuration object and loads the given filename
 * if specified. If you specify NULL as the filename, a volatile
 * configuration object is constructed. Changes made to such an object
 * are not stored to disk.
 *
 * @param Filename the filename of the configuration file, can be NULL
 */
CConfigFile::CConfigFile(const char *Filename, CUser *Owner) {
	SetOwner(Owner);

	m_WriteLock = false;

	m_Settings.RegisterValueDestructor(FreeUString);

	if (Filename != NULL) {
		m_Filename = ustrdup(Filename);

		CHECK_ALLOC_RESULT(m_Filename, strdup) {
			g_Bouncer->Fatal();
		} CHECK_ALLOC_RESULT_END;
	} else {
		m_Filename = NULL;
	}

	Reload();
}

/**
 * ParseConfig
 *
 * Parses a configuration file. Valid lines of the configuration file
 * have this syntax:
 *
 * setting=value
 */
bool CConfigFile::ParseConfig(void) {
	const size_t LineLength = 131072;
	char *Line;
	char *dupEq;
	FILE *ConfigFile;

	if (m_Filename == NULL) {
		return false;
	}

	Line = (char *)malloc(LineLength);

	CHECK_ALLOC_RESULT(Line, malloc) {
		return false;
	} CHECK_ALLOC_RESULT_END;

	ConfigFile = fopen(m_Filename, "r");

	if (ConfigFile == NULL) {
		free(Line);

		return false;
	}

	m_WriteLock = true;

	while (feof(ConfigFile) == 0) {
		fgets(Line, LineLength, ConfigFile);

		if (Line == NULL) {
			break;
		}

		if (strlen(Line) == 0) {
			continue;
		}

		if (Line[strlen(Line) - 1] == '\n') {
			Line[strlen(Line) - 1] = '\0';
		}

		if (Line[strlen(Line) - 1] == '\r') {
			Line[strlen(Line) - 1] = '\0';
		}

		char *Eq = strchr(Line, '=');

		if (Eq != NULL) {
			*Eq = '\0';

			dupEq = ustrdup(++Eq);

			CHECK_ALLOC_RESULT(dupEq, strdup) {
				if (g_Bouncer != NULL) {
					g_Bouncer->Fatal();
				} else {
					exit(0);
				}
			} CHECK_ALLOC_RESULT_END;

			if (m_Settings.Add(Line, dupEq) == false) {
				LOGERROR("CHashtable::Add failed. Config could not be parsed"
					" (%s, %s).", Line, Eq);

				g_Bouncer->Fatal();
			}
		}
	}

	fclose(ConfigFile);

	m_WriteLock = false;

	free(Line);

	return true;
}

/**
 * ~CConfig
 *
 * Destructs the configuration object.
 */
CConfigFile::~CConfigFile() {
	ufree(m_Filename);
}

/**
 * ReadString
 *
 * Reads a configuration setting as a string. If the specified setting does
 * not exist, NULL is returned.
 *
 * @param Setting the configuration setting
 */
RESULT<const char *> CConfigFile::ReadString(const char *Setting) const {
	const char *Value = m_Settings.Get(Setting);

	if (Value != NULL && Value[0] != '\0') {
		RETURN(const char *, Value);
	} else {
		THROW(const char *, Generic_Unknown, "There is no such setting.");
	}
}

/**
 * ReadInteger
 *
 * Reads a configuration setting as an integer. If the specified setting does
 * not exist, 0 is returned.
 *
 * @param Setting the configuration setting
 */
RESULT<int> CConfigFile::ReadInteger(const char *Setting) const {
	const char *Value = m_Settings.Get(Setting);

	if (Value != NULL) {
		RETURN(int, atoi(Value));
	} else {
		THROW(int, Generic_Unknown, "There is no such setting.");
	}
}

/**
 * WriteString
 *
 * Set a configuration setting.
 *
 * @param Setting the configuration setting
 * @param Value the new value for the setting, can be NULL to indicate that
 *              the configuration setting is to be removed
 */
RESULT<bool> CConfigFile::WriteString(const char *Setting, const char *Value) {
	RESULT<bool> ReturnValue;
	const char *OldValue;

	OldValue = ReadString(Setting);

	if ((Value == NULL && OldValue == NULL) || (Value != NULL && OldValue != NULL && strcmp(Value, OldValue) == 0)) {
		RETURN(bool, true);
	}

	if (Value != NULL) {
		ReturnValue = m_Settings.Add(Setting, ustrdup(Value));
	} else {
		ReturnValue = m_Settings.Remove(Setting);
	}

	THROWIFERROR(bool, ReturnValue);

	if (!m_WriteLock) {
		return Persist();
	} else {
		RETURN(bool, true);
	}
}

/**
 * WriteInteger
 *
 * Sets a configuration setting.
 *
 * @param Setting the configuration setting
 * @param Value the new value for the setting
 */
RESULT<bool> CConfigFile::WriteInteger(const char *Setting, const int Value) {
	char *ValueString;
	RESULT<bool> ReturnValue;

	if (Value == 0 && ReadString(Setting) == NULL) {
		RETURN(bool, true);
	}

	asprintf(&ValueString, "%d", Value);

	CHECK_ALLOC_RESULT(ValueString, asprintf) {
		THROW(bool, Generic_OutOfMemory, "asprintf() failed.");
	} CHECK_ALLOC_RESULT_END;

	ReturnValue = WriteString(Setting, ValueString);

	free(ValueString);

	return ReturnValue;
}

/**
 * Persist
 *
 * Saves changes which have been made to the configuration object to disk
 * unless the configuration object is volatile.
 */
RESULT<bool> CConfigFile::Persist(void) const {
	if (m_Filename == NULL) {
		RETURN(bool, false);
	}

	FILE *ConfigFile = fopen(m_Filename, "w");

	CHECK_ALLOC_RESULT(ConfigFile, fopen) {
		THROW(bool, Generic_Unknown, "Could not open config file.");
	} CHECK_ALLOC_RESULT_END;

	SetPermissions(m_Filename, S_IRUSR | S_IWUSR);

	int i = 0;
	while (hash_t<char *> *SettingHash = m_Settings.Iterate(i++)) {
		if (SettingHash->Name != NULL && SettingHash->Value != NULL) {
			fprintf(ConfigFile, "%s=%s\n", SettingHash->Name, SettingHash->Value);
		}
	}

	fclose(ConfigFile);

	RETURN(bool, true);
}

/**
 * GetFilename
 *
 * Returns the filename of the configuration object. The return value will
 * be NULL if the configuration object is volatile.
 */
const char *CConfigFile::GetFilename(void) const {
	return m_Filename;
}

/**
 * Iterate
 *
 * Iterates through the configuration object's settings.
 *
 * @param Index specifies the index of the setting which is to be returned
 */
hash_t<char *> *CConfigFile::Iterate(int Index) const {
	return m_Settings.Iterate(Index);
}

/**
 * Reload
 *
 * Reloads all settings from disk.
 */
void CConfigFile::Reload(void) {
	m_Settings.Clear();

	if (m_Filename != NULL) {
		ParseConfig();
	}
}

/**
 * GetLength
 *
 * Returns the number of items in the config.
 */
unsigned int CConfigFile::GetLength(void) const {
	return m_Settings.GetLength();
}


/**
 * GetInnerHashtable
 *
 * Returns the hashtable which is used for caching the settings.
 */
CHashtable<char *, false, 16> *CConfigFile::GetInnerHashtable(void) {
	return &m_Settings;
}

/**
 * CanUseCache
 *
 * Checks whether a cached version of this Setting can be used.
 */
bool CConfigFile::CanUseCache(void) {
	return true;
}

/**
 * Destroy
 *
 * Destroys the config object.
 */
void CConfigFile::Destroy(void) {
	delete this;
}

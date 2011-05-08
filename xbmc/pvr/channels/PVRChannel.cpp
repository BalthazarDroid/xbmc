/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "FileItem.h"
#include "guilib/LocalizeStrings.h"
#include "utils/log.h"
#include "TextureCache.h"
#include "Util.h"
#include "filesystem/File.h"
#include "music/tags/MusicInfoTag.h"
#include "settings/GUISettings.h"
#include "utils/URIUtils.h"
#include "threads/SingleLock.h"

#include "PVRChannelGroupsContainer.h"
#include "pvr/epg/PVREpgContainer.h"
#include "pvr/timers/PVRTimers.h"
#include "pvr/PVRDatabase.h"
#include "pvr/PVRManager.h"

using namespace XFILE;
using namespace MUSIC_INFO;
using namespace PVR;

bool CPVRChannel::operator==(const CPVRChannel &right) const
{
  if (this == &right) return true;

  return (m_iChannelId              == right.m_iChannelId &&
          m_bIsRadio                == right.m_bIsRadio &&
          m_strIconPath             == right.m_strIconPath &&
          m_strChannelName          == right.m_strChannelName &&
          m_bIsVirtual              == right.m_bIsVirtual &&
          m_iEpgId                  == right.m_iEpgId &&

          m_iUniqueId               == right.m_iUniqueId &&
          m_iClientId               == right.m_iClientId &&
          m_iClientChannelNumber    == right.m_iClientChannelNumber &&
          m_strClientChannelName    == right.m_strClientChannelName &&
          m_strStreamURL            == right.m_strStreamURL &&
          m_iClientEncryptionSystem == right.m_iClientEncryptionSystem);
}

bool CPVRChannel::operator!=(const CPVRChannel &right) const
{
  return !(*this == right);
}

CPVRChannel::CPVRChannel(bool bRadio /* = false */)
{
  m_iChannelId              = -1;
  m_bIsRadio                = bRadio;
  m_bIsHidden               = false;
  m_strIconPath             = "";
  m_strChannelName          = "";
  m_bIsVirtual              = false;
  m_iLastWatched            = 0;
  m_bChanged                = false;
  m_iCachedChannelNumber    = 0;

  m_iEpgId                  = -1;
  m_EPG                     = NULL;
  m_bEPGEnabled             = true;
  m_strEPGScraper           = "client";

  m_iUniqueId               = -1;
  m_iClientId               = -1;
  m_iClientChannelNumber    = -1;
  m_strClientChannelName    = "";
  m_strInputFormat          = "";
  m_strStreamURL            = "";
  m_strFileNameAndPath      = "";
  m_iClientEncryptionSystem = -1;
  m_bIsCachingIcon          = false;
}

CPVRChannel::CPVRChannel(const PVR_CHANNEL &channel, unsigned int iClientId)
{
  m_iChannelId              = -1;
  m_bIsRadio                = channel.bIsRadio;
  m_bIsHidden               = channel.bIsHidden;
  m_strIconPath             = channel.strIconPath;
  m_strChannelName          = channel.strChannelName;
  m_iUniqueId               = channel.iUniqueId;
  m_iClientChannelNumber    = channel.iChannelNumber;
  m_strClientChannelName    = channel.strChannelName;
  m_strInputFormat          = channel.strInputFormat;
  m_strStreamURL            = channel.strStreamURL;
  m_iClientEncryptionSystem = channel.iEncryptionSystem;
  m_iCachedChannelNumber    = 0;
  m_iClientId               = iClientId;
  m_strFileNameAndPath      = "";
  m_bIsVirtual              = false;
  m_iLastWatched            = 0;
  m_bEPGEnabled             = true;
  m_strEPGScraper           = "client";
  m_iEpgId                  = -1;
  m_EPG                     = NULL;
  m_bChanged                = false;
  m_bIsCachingIcon          = false;

  if (m_strChannelName.IsEmpty())
    m_strChannelName.Format("%s %d", g_localizeStrings.Get(19029), m_iUniqueId);

  UpdateEncryptionName();
}

CPVRChannel::CPVRChannel(const CPVRChannel &channel)
{
  *this = channel;
}

CPVRChannel &CPVRChannel::operator=(const CPVRChannel &channel)
{
  m_iChannelId              = channel.m_iChannelId;
  m_bIsRadio                = channel.m_bIsRadio;
  m_bIsHidden               = channel.m_bIsHidden;
  m_strIconPath             = channel.m_strIconPath;
  m_strChannelName          = channel.m_strChannelName;
  m_bIsVirtual              = channel.m_bIsVirtual;
  m_iLastWatched            = channel.m_iLastWatched;
  m_bEPGEnabled             = channel.m_bEPGEnabled;
  m_strEPGScraper           = channel.m_strEPGScraper;
  m_iUniqueId               = channel.m_iUniqueId;
  m_iClientId               = channel.m_iClientId;
  m_iClientChannelNumber    = channel.m_iClientChannelNumber;
  m_strClientChannelName    = channel.m_strClientChannelName;
  m_strInputFormat          = channel.m_strInputFormat;
  m_strStreamURL            = channel.m_strStreamURL;
  m_strFileNameAndPath      = channel.m_strFileNameAndPath;
  m_iClientEncryptionSystem = channel.m_iClientEncryptionSystem;
  m_iCachedChannelNumber    = channel.m_iCachedChannelNumber;
  m_iEpgId                  = channel.m_iEpgId;
  m_EPG                     = channel.m_EPG;
  m_bChanged                = channel.m_bChanged;

  UpdateEncryptionName();

  return *this;
}

bool CPVRChannel::CheckCachedIcon(void)
{
  CSingleLock lock(m_critSection);

  if (m_bIsCachingIcon)
    return false;

  if (URIUtils::IsInternetStream(m_strIconPath, true))
  {
    m_bIsCachingIcon = true;
    CJobManager::GetInstance().AddJob(new CPVRChannelIconCacheJob(this), this);
  }

  return true;
}

bool CPVRChannel::CacheIcon(void)
{
  bool bReturn(false);

  CSingleLock lock(m_critSection);
  if (!m_bIsCachingIcon)
    return bReturn;

  CStdString strIconPath(m_strIconPath);
  lock.Leave();

  strIconPath = CTextureCache::Get().CheckAndCacheImage(strIconPath);

  lock.Enter();
  if (!m_strIconPath.Equals(strIconPath))
  {
    m_strIconPath = strIconPath;
    m_bChanged = true;
    SetChanged();
    bReturn = true;
  }

  return bReturn;
}

/********** XBMC related channel methods **********/

bool CPVRChannel::Delete(void)
{
  bool bReturn = false;
  CPVRDatabase *database = OpenPVRDatabase();
  if (!database)
    return bReturn;

  CSingleLock lock(m_critSection);

  /* delete the EPG table */
  if (m_EPG)
  {
    g_PVREpg->DeleteEpg(*m_EPG, true);
    m_EPG = NULL;
  }

  bReturn = database->Delete(*this);

  database->Close();

  return bReturn;
}

bool CPVRChannel::UpdateFromClient(const CPVRChannel &channel)
{
  SetClientID(channel.ClientID());
  SetClientChannelNumber(channel.ClientChannelNumber());
  SetInputFormat(channel.InputFormat());
  SetStreamURL(channel.StreamURL());
  SetEncryptionSystem(channel.EncryptionSystem());
  SetClientChannelName(channel.ClientChannelName());

  CSingleLock lock(m_critSection);
  if (m_strChannelName.IsEmpty())
    SetChannelName(channel.ClientChannelName());
  if (m_strIconPath.IsEmpty())
    SetIconPath(channel.IconPath());

  return m_bChanged;
}

bool CPVRChannel::Persist(bool bQueueWrite /* = false */)
{
  bool bReturn(true);
  CSingleLock lock(m_critSection);
  if (!m_bChanged && m_iChannelId > 0)
    return bReturn;

  if (CPVRDatabase *database = OpenPVRDatabase())
  {
    if (!bQueueWrite)
    {
      m_iChannelId = database->Persist(*this, false);
      m_bChanged = false;
      bReturn = m_iChannelId > 0;
    }
    else
    {
      bReturn = database->Persist(*this, true) > 0;
    }
    database->Close();
  }
  else
  {
    bReturn = false;
  }

  return bReturn;
}

bool CPVRChannel::SetChannelID(int iChannelId, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_iChannelId != iChannelId)
  {
    /* update the id */
    m_iChannelId = iChannelId;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

int CPVRChannel::ChannelNumber(void) const
{
  CSingleLock lock(m_critSection);
  return m_iCachedChannelNumber;
}

bool CPVRChannel::SetHidden(bool bIsHidden, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_bIsHidden != bIsHidden)
  {
    /* update the hidden flag */
    m_bIsHidden = bIsHidden;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::IsRecording(void) const
{
  return g_PVRTimers->IsRecordingOnChannel(*this);
}

bool CPVRChannel::SetIconPath(const CStdString &strIconPath, bool bSaveInDb /* = false */)
{
  bool bReturn(true); // different from the behaviour of the rest of this class
  CSingleLock lock(m_critSection);

  /* check if the path is valid */
  if (!CFile::Exists(strIconPath))
    return false;

  if (m_strIconPath != strIconPath)
  {
    /* update the path */
    m_strIconPath.Format("%s", strIconPath);
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetChannelName(const CStdString &strChannelName, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CStdString strName(strChannelName);

  if (strName.IsEmpty())
  {
    strName.Format(g_localizeStrings.Get(19085), ClientChannelNumber());
  }

  CSingleLock lock(m_critSection);
  if (m_strChannelName != strName)
  {
    /* update the channel name */
    m_strChannelName = strName;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetVirtual(bool bIsVirtual, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_bIsVirtual != bIsVirtual)
  {
    /* update the virtual flag */
    m_bIsVirtual = bIsVirtual;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetLastWatched(time_t iLastWatched, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_iLastWatched != iLastWatched)
  {
    /* update last watched  */
    m_iLastWatched = iLastWatched;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::IsEmpty() const
{
  CSingleLock lock(m_critSection);
  return (m_strFileNameAndPath.IsEmpty() ||
          m_strStreamURL.IsEmpty());
}

/********** Client related channel methods **********/

bool CPVRChannel::SetUniqueID(int iUniqueId, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_iUniqueId != iUniqueId)
  {
    /* update the unique ID */
    m_iUniqueId = iUniqueId;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetClientID(int iClientId, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_iClientId != iClientId)
  {
    /* update the client ID */
    m_iClientId = iClientId;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetClientChannelNumber(int iClientChannelNumber, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_iClientChannelNumber != iClientChannelNumber && iClientChannelNumber > 0)
  {
    /* update the client channel number */
    m_iClientChannelNumber = iClientChannelNumber;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetClientChannelName(const CStdString &strClientChannelName)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_strClientChannelName != strClientChannelName)
  {
    /* update the client channel name */
    m_strClientChannelName.Format("%s", strClientChannelName);
    SetChanged();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetInputFormat(const CStdString &strInputFormat, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_strInputFormat != strInputFormat)
  {
    /* update the input format */
    m_strInputFormat.Format("%s", strInputFormat);
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetStreamURL(const CStdString &strStreamURL, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_strStreamURL != strStreamURL)
  {
    /* update the stream url */
    m_strStreamURL.Format("%s", strStreamURL);
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

void CPVRChannel::UpdatePath(unsigned int iNewChannelNumber)
{
  CStdString strFileNameAndPath;
  CSingleLock lock(m_critSection);

  strFileNameAndPath.Format("pvr://channels/%s/%s/%i.pvr", (m_bIsRadio ? "radio" : "tv"), g_PVRChannelGroups->GetGroupAll(m_bIsRadio)->GroupName().c_str(), iNewChannelNumber);
  if (m_strFileNameAndPath != strFileNameAndPath)
  {
    m_strFileNameAndPath = strFileNameAndPath;
    SetChanged();
  }
}

bool CPVRChannel::SetEncryptionSystem(int iClientEncryptionSystem, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_iClientEncryptionSystem != iClientEncryptionSystem)
  {
    /* update the client encryption system */
    m_iClientEncryptionSystem = iClientEncryptionSystem;
    UpdateEncryptionName();
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    bReturn = true;
  }

  return bReturn;
}

void CPVRChannel::UpdateEncryptionName(void)
{
  // http://www.dvb.org/index.php?id=174
  // http://en.wikipedia.org/wiki/Conditional_access_system
  CStdString strName;
  CSingleLock lock(m_critSection);

  if (     m_iClientEncryptionSystem == 0x0000)
    strName = g_localizeStrings.Get(19013); /* Free To Air */
  else if (m_iClientEncryptionSystem <  0x0000)
    strName = g_localizeStrings.Get(13205); /* Unknown */
  else if (m_iClientEncryptionSystem >= 0x0001 &&
           m_iClientEncryptionSystem <= 0x009F)
    strName.Format("%s (%X)", g_localizeStrings.Get(19014).c_str(), m_iClientEncryptionSystem); /* Fixed */
  else if (m_iClientEncryptionSystem >= 0x00A0 &&
           m_iClientEncryptionSystem <= 0x00A1)
    strName.Format("%s (%X)", g_localizeStrings.Get(338).c_str(), m_iClientEncryptionSystem); /* Analog */
  else if (m_iClientEncryptionSystem >= 0x00A2 &&
           m_iClientEncryptionSystem <= 0x00FF)
    strName.Format("%s (%X)", g_localizeStrings.Get(19014).c_str(), m_iClientEncryptionSystem); /* Fixed */
  else if (m_iClientEncryptionSystem >= 0x0100 &&
           m_iClientEncryptionSystem <= 0x01FF)
    strName.Format("%s (%X)", "SECA Mediaguard", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x0464)
    strName.Format("%s (%X)", "EuroDec", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x0500 &&
           m_iClientEncryptionSystem <= 0x05FF)
    strName.Format("%s (%X)", "Viaccess", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x0600 &&
           m_iClientEncryptionSystem <= 0x06FF)
    strName.Format("%s (%X)", "Irdeto", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x0900 &&
           m_iClientEncryptionSystem <= 0x09FF)
    strName.Format("%s (%X)", "NDS Videoguard", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x0B00 &&
           m_iClientEncryptionSystem <= 0x0BFF)
    strName.Format("%s (%X)", "Conax", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x0D00 &&
           m_iClientEncryptionSystem <= 0x0DFF)
    strName.Format("%s (%X)", "CryptoWorks", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x0E00 &&
           m_iClientEncryptionSystem <= 0x0EFF)
    strName.Format("%s (%X)", "PowerVu", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x1000)
    strName.Format("%s (%X)", "RAS", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x1200 &&
           m_iClientEncryptionSystem <= 0x12FF)
    strName.Format("%s (%X)", "NagraVision", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x1700 &&
           m_iClientEncryptionSystem <= 0x17FF)
    strName.Format("%s (%X)", "BetaCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x1800 &&
           m_iClientEncryptionSystem <= 0x18FF)
    strName.Format("%s (%X)", "NagraVision", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x22F0)
    strName.Format("%s (%X)", "Codicrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x2600)
    strName.Format("%s (%X)", "BISS", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4347)
    strName.Format("%s (%X)", "CryptOn", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4800)
    strName.Format("%s (%X)", "Accessgate", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4900)
    strName.Format("%s (%X)", "China Crypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4A10)
    strName.Format("%s (%X)", "EasyCas", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4A20)
    strName.Format("%s (%X)", "AlphaCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4A70)
    strName.Format("%s (%X)", "DreamCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4A60)
    strName.Format("%s (%X)", "SkyCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4A61)
    strName.Format("%s (%X)", "Neotioncrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4A62)
    strName.Format("%s (%X)", "SkyCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4A63)
    strName.Format("%s (%X)", "Neotion SHL", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x4A64 &&
           m_iClientEncryptionSystem <= 0x4A6F)
    strName.Format("%s (%X)", "SkyCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4A80)
    strName.Format("%s (%X)", "ThalesCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4AA1)
    strName.Format("%s (%X)", "KeyFly", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4ABF)
    strName.Format("%s (%X)", "DG-Crypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem >= 0x4AD0 &&
           m_iClientEncryptionSystem <= 0x4AD1)
    strName.Format("%s (%X)", "X-Crypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4AD4)
    strName.Format("%s (%X)", "OmniCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x4AE0)
    strName.Format("%s (%X)", "RossCrypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x5500)
    strName.Format("%s (%X)", "Z-Crypt", m_iClientEncryptionSystem);
  else if (m_iClientEncryptionSystem == 0x5501)
    strName.Format("%s (%X)", "Griffin", m_iClientEncryptionSystem);
  else
    strName.Format("%s (%X)", g_localizeStrings.Get(19499).c_str(), m_iClientEncryptionSystem); /* Unknown */

  m_strClientEncryptionName = strName;
}

/********** EPG methods **********/

CPVREpg *CPVRChannel::GetEPG(void)
{
  CSingleLock lock(m_critSection);
  if (m_EPG == NULL)
  {
    if (m_iEpgId > 0 && !g_guiSettings.GetBool("epg.ignoredbforclient"))
      m_EPG = (CPVREpg *) g_PVREpg->GetById(m_iEpgId);

    if (m_EPG == NULL)
    {
      /* will be cleaned up by CPVREpgContainer on exit */
      m_EPG = new CPVREpg(this, false);
      if (!g_guiSettings.GetBool("epg.ignoredbforclient"))
        m_EPG->Persist();
      g_PVREpg->push_back(m_EPG);
    }

    if (m_EPG && m_iEpgId != m_EPG->EpgID())
    {
      m_iEpgId = m_EPG->EpgID();
      m_bChanged = true;
    }
  }

  return m_EPG;
}

int CPVRChannel::GetEPG(CFileItemList *results)
{
  CPVREpg *epg = GetEPG();
  if (!epg)
  {
    CLog::Log(LOGDEBUG, "PVR - %s - cannot get EPG for channel '%s'",
        __FUNCTION__, m_strChannelName.c_str());
    return -1;
  }

  return epg->Get(results);
}

bool CPVRChannel::ClearEPG()
{
  CSingleLock lock(m_critSection);
  if (m_EPG)
    GetEPG()->Clear();

  return true;
}

CPVREpgInfoTag* CPVRChannel::GetEPGNow(void) const
{
  CPVREpgInfoTag *tag(NULL);
  CSingleLock lock(m_critSection);

  if (!m_bIsHidden && m_bEPGEnabled && m_EPG)
    tag = (CPVREpgInfoTag *) m_EPG->InfoTagNow();

  return tag;
}

CPVREpgInfoTag* CPVRChannel::GetEPGNext(void) const
{
  CPVREpgInfoTag *tag(NULL);
  CSingleLock lock(m_critSection);

  if (!m_bIsHidden && m_bEPGEnabled && m_EPG)
    tag = (CPVREpgInfoTag *) m_EPG->InfoTagNext();

  return tag;
}

bool CPVRChannel::SetEPGEnabled(bool bEPGEnabled /* = true */, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_bEPGEnabled != bEPGEnabled)
  {
    /* update the EPG flag */
    m_bEPGEnabled = bEPGEnabled;
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    /* clear the previous EPG entries if needed */
    if (!m_bEPGEnabled && m_EPG)
      m_EPG->Clear();

    bReturn = true;
  }

  return bReturn;
}

bool CPVRChannel::SetEPGScraper(const CStdString &strScraper, bool bSaveInDb /* = false */)
{
  bool bReturn(false);
  CSingleLock lock(m_critSection);

  if (m_strEPGScraper != strScraper)
  {
    bool bCleanEPG = !m_strEPGScraper.IsEmpty() || strScraper.IsEmpty();

    /* update the scraper name */
    m_strEPGScraper.Format("%s", strScraper);
    SetChanged();
    m_bChanged = true;

    /* persist the changes */
    if (bSaveInDb)
      Persist();

    /* clear the previous EPG entries if needed */
    if (bCleanEPG && m_bEPGEnabled && m_EPG)
      m_EPG->Clear();

    bReturn = true;
  }

  return bReturn;
}

void CPVRChannel::SetCachedChannelNumber(unsigned int iChannelNumber)
{
  CSingleLock lock(m_critSection);
  m_iCachedChannelNumber = iChannelNumber;
}

void CPVRChannel::OnJobComplete(unsigned int jobID, bool success, CJob* job)
{
  if (!strcmp(job->GetType(), "pvr-channel-icon-update"))
  {
    CSingleLock lock(m_critSection);
    m_bIsCachingIcon = false;
  }
}

bool CPVRChannelIconCacheJob::DoWork(void)
{
  if (m_channel->CacheIcon())
  {
    m_channel->Persist(false);
    return true;
  }

  return false;
}
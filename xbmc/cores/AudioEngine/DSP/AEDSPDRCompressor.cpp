/*
 *      Copyright (C) 2010-2012 Team XBMC
 *      http://xbmc.org
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
 *
 *  Added to XBMC AudioEngine by DDDamian June 2012
 */

#include "AEDSPDRCompressor.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUIsettings.h"


///////////////////////////////////////////////////////////////////////////


CAEDSPDRCompressor::CAEDSPDRCompressor() :
  m_returnBuffer (NULL),
  m_returnSamples(0   )
{
}


CAEDSPDRCompressor::~CAEDSPDRCompressor()
{
  DeInitialize();
}

void CAEDSPDRCompressor::DeInitialize()
{
}

///////////////////////////////////////////////////////////////////////////


bool CAEDSPDRCompressor::Initialize(const CAEChannelInfo& channels, const unsigned int sampleRate)
{
  m_sampleRate   = sampleRate;
  m_channels     = channels;
  m_channelCount = channels.Count();

  if (!m_channelCount)
    return false;

  /* Setup parameters */
  SetParameterDefaults();

  /* Get our FL & FR channel offsets */
  /* FR = FL if mono                 */
  m_channelFL = m_channelFR = 0;

  for (unsigned int c = 0; c < m_channelCount; c++)
  {
    if (m_channels[c] == AE_CH_FL)
      m_channelFL = c;
    if (m_channels[c] == AE_CH_FR)
      m_channelFR = c;
  }

  if (m_channelCount == 1)
    m_channelFR = m_channelFL;  /* must be mono */

  return true;
}

void CAEDSPDRCompressor::OnSettingChange(std::string setting)
{
  Initialize(m_channels, m_sampleRate);
}

void CAEDSPDRCompressor::SetParameterDefaults()
{
  /* Calculate factor ramp rate */
  m_rampCoeff = SinglePoleCoeff((DRC_TYPE)m_sampleRate, (DRC_TYPE)0.05);

  /* Clear transient values */
  m_cvRelease = 0.0;
  m_cvAttack  = 0.0;
  m_cvSmooth  = 0.0;
  m_crestPeak = MINVAL;
  m_crestRms  = MINVAL;

  /* Setup default values */
  m_logThreshold[kCurrent] = 0.0;
  m_logThreshold[kTarget]  = 0.0;
  m_logGain[kCurrent]      = 0.0;
  m_logGain[kTarget]       = 0.0;
  m_logThreshold[kTarget]  = -6.0;
  m_logKneeWidth           = 15.0;
  m_slope        = -1.0;
  m_attackTime   = 0.030;
  m_releaseTime  = 0.120;

  /* Default to full automation */
  m_bAutoKnee      = true;
  m_bAutoGain      = true;
  m_bAutoAttack    = true;
  m_bAutoRelease   = true;
  m_bNoClipping    = true;

  /* Get advancedsettings.xml parameters if set */
  m_bAutoGain    = g_advancedSettings.dspDRCAutoGain;
  m_bAutoAttack  = g_advancedSettings.dspDRCAutoAttack;
  m_bAutoRelease = g_advancedSettings.dspDRCAutoRelease;
  m_bAutoKnee    = g_advancedSettings.dspDRCAutoKnee;
  m_attackTime   = g_advancedSettings.dspDRCAttackTime;
  m_releaseTime  = g_advancedSettings.dspDRCReleaseTime;
  m_slope        = g_advancedSettings.dspDRCSlope - 1.0;
  m_logGain[kTarget]      = g_advancedSettings.dspDRCGain;
  m_logThreshold[kTarget] = g_advancedSettings.dspDRCThreshold;
  m_logKneeWidth          = g_advancedSettings.dspDRCKneeWidth;

  /* Intelligent auto-adjust setup */
  m_autoKneeMult       = 2.0;
  m_autoMaxAttackTime  = 0.08;
  m_autoMaxReleaseTime = 0.20;
  m_metaCrestTime      = 0.2;
  m_metaAdaptTime      = 1.5;

  /* Calculate filter poles */
  m_attackCoeff    = SinglePoleCoeff(m_sampleRate, m_attackTime);
  m_releaseCoeff   = SinglePoleCoeff(m_sampleRate, m_releaseTime);
  m_metaCrestCoeff = SinglePoleCoeff(m_sampleRate, m_metaCrestTime);
  m_metaAdaptCoeff = SinglePoleCoeff(m_sampleRate, m_metaAdaptTime);
}

unsigned int CAEDSPDRCompressor::Process(float *data, unsigned int samples)
{
  /* Store return values */
  m_returnBuffer  = data;
  m_returnSamples = samples;

  float *pCurrentFrame = data;
  unsigned int frames = samples / m_channelCount; 

  for (unsigned int offset = 0; offset < frames; offset++)
  {
    /* Process sample buffer */
    pCurrentFrame = data + (offset * m_channelCount);

    float inL = pCurrentFrame[m_channelFL];
    float inR = pCurrentFrame[m_channelFR];

    float absL = ABS(inL);
    float absR = ABS(inR);
    float maxLR = MAX(absL, absR);

    /* Do the heavy lifting seperately */
    float mult = (float) ProcessSidechain(maxLR);

    /* Process one frame */
    for (unsigned int channel = 0; channel < m_channelCount; channel++)
    {
      float out = pCurrentFrame[channel] * mult;
      pCurrentFrame[channel] = out;
    }

    /* Ramp parameters */
    m_logThreshold[kCurrent] = MIX(m_logThreshold[kTarget], m_logThreshold[kCurrent], m_rampCoeff);
    m_logGain[kCurrent]      = MIX(m_logGain[kTarget], m_logGain[kCurrent], m_rampCoeff);
  }

  /* Kill denormals */
  m_cvAttack               = KillDenormal(m_cvAttack);
  m_cvRelease              = KillDenormal(m_cvRelease);
  m_cvSmooth               = KillDenormal(m_cvSmooth);
  m_crestRms               = KillDenormal(m_crestRms);
  m_crestPeak              = KillDenormal(m_crestPeak);
  m_logThreshold[kCurrent] = KillDenormal(m_logThreshold[kCurrent]);
  m_logGain[kCurrent]      = KillDenormal(m_logGain[kCurrent]);

  return m_returnSamples;
}

double CAEDSPDRCompressor::GetDelay()
{
  return 0.0;
}

float *CAEDSPDRCompressor::GetOutput(unsigned int& samples)
{
  /* Always return full buffer */
  samples = m_returnSamples;
  return m_returnBuffer;
}

void CAEDSPDRCompressor::GetOutputFormat(CAEChannelInfo& channels, unsigned int& sampleRate)
{
  /* Output format == Input format */
  sampleRate = m_sampleRate;
  channels   = m_channels;
  return;
}

DRC_TYPE CAEDSPDRCompressor::ProcessSidechain(DRC_TYPE inAbs)
{
  /* Calculate crest factors */
  DRC_TYPE inSquare = MAX(SQUARE(inAbs), MINVAL);
  m_crestRms = MIX(inSquare, m_crestRms, m_metaCrestCoeff);
  m_crestPeak = MAX(MIX(inSquare, m_crestPeak, m_metaCrestCoeff), inSquare);
  DRC_TYPE crestSquare = m_crestPeak / m_crestRms;

  /* Calculate attack and release coefficients */
  DRC_TYPE autoAttackCoeff = m_attackCoeff;
  DRC_TYPE autoAttackTime = m_attackTime;
  if (m_bAutoAttack)
  {
    autoAttackTime = 2.0 * m_autoMaxAttackTime / crestSquare;
    autoAttackCoeff = SinglePoleCoeff(m_sampleRate, autoAttackTime);
  }

  DRC_TYPE autoReleaseCoeff = m_releaseCoeff;
  if (m_bAutoRelease)
  {
    DRC_TYPE autoReleaseTime = 2.0 * m_autoMaxReleaseTime / crestSquare;
    autoReleaseCoeff = SinglePoleCoeff(m_sampleRate, autoReleaseTime - autoAttackTime);
  }

  /* Log conversion and overshoot calculation */
  DRC_TYPE logIn = log(MAX(inAbs, MINVAL));
  DRC_TYPE logOvershoot = logIn - m_logThreshold[kCurrent];

  /* Set ratio/slope */
  DRC_TYPE autoSlope = m_slope;
  if (m_bAutoKnee)
    autoSlope = -1.0;

  /* Set estimate for average CV */
  DRC_TYPE cvEstimate = m_logThreshold[kCurrent] * -autoSlope * 0.5;

  /* Set soft/hard knee width */
  DRC_TYPE autoLogWidth = m_logKneeWidth;
  if (m_bAutoKnee)
    autoLogWidth = MAX(-(m_cvSmooth + cvEstimate) * m_autoKneeMult, 0.0);

  /* Soft knee rectification */
  DRC_TYPE cv = 0.0;
  if (logOvershoot >= autoLogWidth * 0.5)
  {
    cv = logOvershoot;
  }
  else if (logOvershoot > -autoLogWidth * 0.5 && logOvershoot < autoLogWidth * 0.5)
  {
    cv = 1.0 / (2.0 * autoLogWidth) * SQUARE(logOvershoot + autoLogWidth * 0.5);
  }

  /* Multiply by negative slope for positive CV */
  cv *= -autoSlope;

  /* Release and Attack calcs */
  m_cvRelease = MAX(cv, MIX(cv, m_cvRelease, autoReleaseCoeff));
  m_cvAttack = MIX(m_cvRelease, m_cvAttack, autoAttackCoeff);

  /* Invert CV again */
  cv = -m_cvAttack;

  /* Smooth CV */
  m_cvSmooth = MIX(cv - cvEstimate, m_cvSmooth, m_metaAdaptCoeff);

  /* Make-up gain */
  if (m_bAutoGain)
  {
    /* Check for clipping and limit */
    if (m_bNoClipping && logIn + cv - (m_cvSmooth + cvEstimate) > MAXCLIPLOG)
    {
      m_cvSmooth = logIn + cv - cvEstimate - MAXCLIPLOG;
    }

    /* Apply automatic gain */
    cv -= (m_cvSmooth + cvEstimate) * 0.8;
  }
  else
  {
    /* Apply static gain from user */
    cv += m_logGain[kCurrent];
  }

  /* Finalize compression factor */
  return exp(cv);
}

/* Calculate one pole filter coefficient */
inline DRC_TYPE CAEDSPDRCompressor::SinglePoleCoeff(DRC_TYPE m_sampleRate, DRC_TYPE tau)
{
  if (tau > 0.0)
  {
    return 1.0 - exp(-1.0 / (tau * (DRC_TYPE)m_sampleRate));
  }
  return 1.0;
}

/* Kill denormal numbers */
inline DRC_TYPE CAEDSPDRCompressor::KillDenormal(DRC_TYPE value)
{
  static const DRC_TYPE denormal = 1E-18;
  value += denormal;
  value -= denormal;
  return value;
}

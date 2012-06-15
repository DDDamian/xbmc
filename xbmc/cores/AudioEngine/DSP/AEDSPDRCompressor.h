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

#include <math.h>
#include "Interfaces/AEDSP.h"

/* Macros used in calcs */
#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))
#define CLAMP(x, min, max) ((x)>(max)?(max):(((x)<(min))?(min):(x)))
#define ABS(x) ((x)<0?(-(x)):(x))
#define SQUARE(x) ((x)*(x))
#define MIX(x0, y1, coeff) (((x0)-(y1))*(coeff)+(y1))

/* dont use double precision on embedded devices */
#if defined(__arm__) || defined(TARGET_DARWIN_IOS)
  #define DRC_TYPE float
#else
  #define DRC_DOUBLE
  #define DRC_TYPE double
#endif

/* Constants */
static const DRC_TYPE MINVAL     =  1.0e-6;
static const DRC_TYPE MAXCLIPLOG = -0.25;
static const int  kCurrent = 0;
static const int  kTarget  = 1;

///////////////////////////////////////////////////////////////////////////

/**
 * CAEDSPDRCompressor - An implmentation of an intelligent self-adjusting
 *                      Dynamic Range Compression algorithm
 */
class CAEDSPDRCompressor : public IAEDSP
{
public:
  CAEDSPDRCompressor();
  virtual ~CAEDSPDRCompressor();

  /** Must call Initialize first to set DSP parameters */
  virtual bool Initialize(const CAEChannelInfo& channels, const unsigned int sampleRate);

  virtual void DeInitialize();

  virtual void GetOutputFormat(CAEChannelInfo& channels, unsigned int& sampleRate);

  virtual unsigned int Process(float *data, unsigned int samples);

  virtual float *GetOutput(unsigned int& samples);

  virtual double GetDelay();

  virtual void   OnSettingChange(std::string setting);

//////////////////////////////////////////////////////////////////////////////////////////

private:
  DRC_TYPE ProcessSidechain(DRC_TYPE absIn);
  inline DRC_TYPE SinglePoleCoeff(DRC_TYPE m_sampleRate, DRC_TYPE tau);
  inline DRC_TYPE KillDenormal(DRC_TYPE value);

  void   SetParameterDefaults();

  DRC_TYPE m_rampCoeff;

  /* Use auto adjust */
  bool   m_bAutoKnee;
  bool   m_bAutoGain;
  bool   m_bAutoAttack;
  bool   m_bAutoRelease;
  bool   m_bNoClipping;

  DRC_TYPE m_cvRelease;
  DRC_TYPE m_cvAttack;
  DRC_TYPE m_cvSmooth;

  DRC_TYPE m_logThreshold[2];
  DRC_TYPE m_logGain[2];
  DRC_TYPE m_logKneeWidth;
  DRC_TYPE m_slope;

  DRC_TYPE m_crestPeak;
  DRC_TYPE m_crestRms;

  DRC_TYPE m_attackTime;
  DRC_TYPE m_attackCoeff;

  DRC_TYPE m_releaseTime;
  DRC_TYPE m_releaseCoeff;

  DRC_TYPE m_autoKneeMult;
  DRC_TYPE m_autoMaxAttackTime;
  DRC_TYPE m_autoMaxReleaseTime;
  DRC_TYPE m_metaCrestTime;
  DRC_TYPE m_metaCrestCoeff;
  DRC_TYPE m_metaAdaptTime;
  DRC_TYPE m_metaAdaptCoeff;

  unsigned int   m_sampleRate;
  unsigned int   m_channelCount;
  unsigned int   m_channelFL;
  unsigned int   m_channelFR;
  CAEChannelInfo m_channels;

  unsigned int m_returnSamples;
  float*       m_returnBuffer;
};

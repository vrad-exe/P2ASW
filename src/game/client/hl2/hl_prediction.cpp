//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: No need it in ASW [str]
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "prediction.h"
#include "hl_movedata.h"
#if HL2_CLIENT_DLL
#include "c_basehlplayer.h"
#elif PORTAL2
#include "c_portal_player.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static CHLMoveData g_HLMoveData;
CMoveData *g_pMoveData = &g_HLMoveData;

// Expose interface to engine
static CPrediction g_Prediction;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CPrediction, IPrediction, VCLIENT_PREDICTION_INTERFACE_VERSION, g_Prediction );

CPrediction *prediction = &g_Prediction;


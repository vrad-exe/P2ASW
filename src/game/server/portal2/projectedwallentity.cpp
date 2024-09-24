#include "cbase.h"
#include "projectedwallentity.h"
#include "paint/paint_database.h"
#include "vcollide_parse.h"
#include "portal_player.h"
#include "physicsshadowclone.h"
#include "mathlib/polyhedron.h"

#ifndef NO_PROJECTED_WALL

// TODO: Not sure where these should go
#define PROJECTED_WALL_WIDTH 64.0f
#define PROJECTED_WALL_HEIGHT 0.015625 // 1/64 - thickness of the bridge

int CProjectedWallEntity::s_HardLightBridgeSurfaceProps = -1;

extern ConVar sv_thinnerprojectedwalls;

IMPLEMENT_AUTO_LIST( IProjectedWallEntityAutoList )

BEGIN_DATADESC( CProjectedWallEntity )

	DEFINE_FIELD( m_vWorldSpace_WallMins, FIELD_VECTOR ),
	DEFINE_FIELD( m_vWorldSpace_WallMaxs, FIELD_VECTOR ),

	DEFINE_FIELD( m_hColorPortal, FIELD_EHANDLE ),
	
	DEFINE_FIELD( m_flLength, FIELD_FLOAT ),
	DEFINE_FIELD( m_flHeight, FIELD_FLOAT ),
	DEFINE_FIELD( m_flWidth, FIELD_FLOAT ),
	DEFINE_FIELD( m_flSegmentLength, FIELD_FLOAT ),
	DEFINE_FIELD( m_flParticleUpdateTime, FIELD_FLOAT ),
	
	DEFINE_FIELD( m_bIsHorizontal, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_nNumSegments, FIELD_INTEGER ),

END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CProjectedWallEntity, DT_ProjectedWallEntity )
	SendPropFloat( SENDINFO( m_flLength ) ),
	SendPropFloat( SENDINFO( m_flHeight ) ),
	SendPropFloat( SENDINFO( m_flWidth ) ),
	SendPropFloat( SENDINFO( m_flSegmentLength ) ),
	SendPropFloat( SENDINFO( m_flParticleUpdateTime ) ),
	
	SendPropBool( SENDINFO( m_bIsHorizontal ) ),
	SendPropInt( SENDINFO( m_nNumSegments ) ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( projected_wall_entity, CProjectedWallEntity )

CProjectedWallEntity::CProjectedWallEntity()
{

}

CProjectedWallEntity::~CProjectedWallEntity()
{
	CleanupWall();
	PaintDatabase.RemovePaintedWall( this );
}

void CProjectedWallEntity::Spawn( void )
{
	BaseClass::Spawn();
	Precache();
	CollisionProp()->SetSolid( SOLID_CUSTOM );
	CollisionProp()->SetSolidFlags( FSOLID_CUSTOMBOXTEST | FSOLID_CUSTOMRAYTEST | FSOLID_FORCE_WORLD_ALIGNED );
	CollisionProp()->SetSurroundingBoundsType( USE_GAME_CODE );
	SetMoveType(MOVETYPE_NONE, MOVECOLLIDE_DEFAULT );
	CreateVPhysics();
	SetTransmitState( FL_EDICT_ALWAYS );
	if (CProjectedWallEntity::s_HardLightBridgeSurfaceProps == -1)
		CProjectedWallEntity::s_HardLightBridgeSurfaceProps = physprops->GetSurfaceIndex("hard_light_bridge");
}

void CProjectedWallEntity::Precache( void )
{
	PrecacheParticleSystem("projected_wall_impact");
}

void CProjectedWallEntity::OnRestore( void )
{
	BaseClass::OnRestore();
	SetTransmitState( FL_EDICT_ALWAYS );
}

void CProjectedWallEntity::UpdateOnRemove( void )
{
	CheckForPlayersOnBridge();
	CheckForSettledReflectorCubes();
	CleanupWall();
	BaseClass::UpdateOnRemove();
}

void CProjectedWallEntity::NotifyPortalEvent( PortalEvent_t nEventType, CPortal_Base2D *pNotifier )
{
	if (nEventType == PORTALEVENT_LINKED)
	{
		pNotifier->RemovePortalEventListener( this );
	}
}

void CProjectedWallEntity::SetHitPortal( CPortal_Base2D *pPortal )
{
	if ( pPortal )
	{
		if( !DidRedirectionPortalMove( pPortal ) )
			goto LABEL_11;
	}
	else if ( !GetHitPortal() )
	{
		goto LABEL_11;
	}
	m_flParticleUpdateTime = gpGlobals->curtime + 0.5;

LABEL_11:
	BaseClass::SetHitPortal( pPortal );
}
void CProjectedWallEntity::SetSourcePortal( CPortal_Base2D *pPortal)
{
	m_flParticleUpdateTime = gpGlobals->curtime + 0.5;
	m_hColorPortal = pPortal;

	BaseClass::SetSourcePortal( pPortal );
}

void CProjectedWallEntity::GetProjectionExtents( Vector &outMins, Vector &outMaxs )
{
	GetExtents( outMins, outMaxs, 0.5 );
}

bool CProjectedWallEntity::ShouldSavePhysics( void )
{
	return false;
}

bool CProjectedWallEntity::CreateVPhysics( void )
{
	ProjectWall();
	return true;
}

void CProjectedWallEntity::ProjectWall( void )
{
	// the decompiler in question:
	float v18; // xmm5_4
	float v19; // xmm4_4
	float v20; // xmm3_4
	solid_t solid; // [esp+64h] [ebp-724h] BYREF
	Vector vLocalMaxs; // [esp+6A4h] [ebp-E4h] BYREF
	float flBackDist; // [esp+6B0h] [ebp-D8h] BYREF
	Vector v38; // [esp+6B4h] [ebp-D4h] BYREF
	float flFrontDist; // [esp+6C0h] [ebp-C8h]
	Vector v40; // [esp+6C4h] [ebp-C4h] BYREF
	float flRightDist; // [esp+6D0h] [ebp-B8h]
	float flLeftDist; // [esp+6E0h] [ebp-A8h]
	Vector vLocalMins; // [esp+704h] [ebp-84h] BYREF
	Vector vMins; // [esp+738h] [ebp-50h] BYREF
	Vector vMaxs; // [esp+744h] [ebp-44h] BYREF
	Vector vUp; // [esp+750h] [ebp-38h] BYREF
	Vector vRight; // [esp+75Ch] [ebp-2Ch] BYREF

	CleanupWall();
	AddEffects( EF_NOINTERP );
	CheckForPlayersOnBridge();

	const Vector vStartPoint = GetStartPoint();
	const Vector vEndPoint = GetEndPoint();

	Vector vecForward;
	Vector vecRight;
	Vector vecUp;
	GetVectors( &vecForward, &vecRight, &vecUp );

	CPhysConvex *pTempConvex;

	// Ignoring this for now mostly - Kelsey
	if (sv_thinnerprojectedwalls.GetInt())
	{
		Vector vBackRight = vStartPoint + (vecRight * 32.0);
		Vector vBackLeft = vStartPoint - (vecRight * 32.0);
		Vector vFrontRight = vEndPoint + (vecRight * 32.0);
		Vector vFrontLeft = vEndPoint - (vecRight * 32.0);

		//flBackDist = vStartPoint.vMins_x - (vecRight.vMins_x * 32.0);

		//flFrontDist = vEndPoint.vMins_y - (vecRight.vMins_y * 32.0);

		//v40.vMins_x = vEndPoint.vMins_z - (32.0 * vecRight.vMins_z);
		//v40.vMins_y = (vecRight.vMins_x * 32.0) + vEndPoint.vMins_x;
		//v40.vMins_z = (vecRight.vMins_y * 32.0) + vEndPoint.vMins_y;

		//flRightDist = (32.0 * vecRight.vMins_z) + vEndPoint.vMins_z;

		Vector *vVerts[4];

		vVerts[0] = &vBackRight;
		vVerts[1] = &vBackLeft;
		vVerts[2] = &vFrontRight;
		vVerts[3] = &vFrontLeft;
		
		pTempConvex = physcollision->ConvexFromVerts( vVerts, 4 );
	}
	else
	{
		Vector vecBackward = vecForward * -1.0f;
		Vector vecDown = vecUp * -1.0f;
		Vector vecLeft = vecRight * -1.0f;

		flFrontDist = (vecForward.x * vEndPoint.x) + (vecForward.y * vEndPoint.y) + (vecForward.z * vEndPoint.z);

		flBackDist = (vecBackward.x * vStartPoint.x) + (vecBackward.y * vStartPoint.y) + (vecBackward.z * vStartPoint.z);

		Vector vecRightDist = vecRight * PROJECTED_WALL_WIDTH / 2;

		v18 = (vecRight.x * 64.0) * 0.5;
		v19 = (vecRight.y * 64.0) * 0.5;
		v20 = (vecRight.z * 64.0) * 0.5;

		flRightDist = ((vStartPoint.x + vecRightDist.x) * vecRight.x) 
					+ ((vStartPoint.y + vecRightDist.y) * vecRight.y) 
					+ ((vStartPoint.z + vecRightDist.z) * vecRight.z);

		flLeftDist = ((vStartPoint.x - vecRightDist.x) * vecLeft.x) 
					+ ((vStartPoint.y - vecRightDist.y) * vecLeft.y) 
					+ ((vStartPoint.z - vecRightDist.z) * vecLeft.z);

		Vector vecUpDist = vecUp * PROJECTED_WALL_HEIGHT / 2;

		float flUpDist = ((vStartPoint.x + vecUpDist.x) * vecUp.x) 
						+ ((vStartPoint.y + vecUpDist.y) * vecUp.y) 
						+ ((vStartPoint.z + vecUpDist.z) * vecUp.z);

		float flDownDist = ((vStartPoint.x - vecUpDist.x) * vecDown.x)
						+ ((vStartPoint.y - vecUpDist.y) * vecDown.y)
						+ ((vStartPoint.z - vecUpDist.z) * vecDown.z);

		float fPlanes[6 * 4];

		// Forward plane
		fPlanes[(0 * 4) + 0] = vecForward.x;
		fPlanes[(0 * 4) + 1] = vecForward.y;
		fPlanes[(0 * 4) + 2] = vecForward.z;
		fPlanes[(0 * 4) + 3] = flFrontDist + m_flLength;

		// Back plane
		fPlanes[(1 * 4) + 0] = -vecForward.x;
		fPlanes[(1 * 4) + 1] = -vecForward.y;
		fPlanes[(1 * 4) + 2] = -vecForward.z;
		fPlanes[(1 * 4) + 3] = flBackDist + m_flLength;

		// Up plane
		fPlanes[(2 * 4) + 0] = vecUp.x;
		fPlanes[(2 * 4) + 1] = vecUp.y;
		fPlanes[(2 * 4) + 2] = vecUp.z;
		fPlanes[(2 * 4) + 3] = flUpDist + m_flHeight;

		// Down plane
		fPlanes[(3 * 4) + 0] = -vecUp.x;
		fPlanes[(3 * 4) + 1] = -vecUp.y;
		fPlanes[(3 * 4) + 2] = -vecUp.z;
		fPlanes[(3 * 4) + 3] = flDownDist + m_flHeight;

		// Right plane
		fPlanes[(4 * 4) + 0] = vecRight.x;
		fPlanes[(4 * 4) + 1] = vecRight.y;
		fPlanes[(4 * 4) + 2] = vecRight.z;
		fPlanes[(4 * 4) + 3] = flRightDist + m_flWidth;

		// Left plane
		fPlanes[(5 * 4) + 0] = -vecRight.x;
		fPlanes[(5 * 4) + 1] = -vecRight.y;
		fPlanes[(5 * 4) + 2] = -vecRight.z;
		fPlanes[(5 * 4) + 3] = flLeftDist + m_flWidth;

		CPolyhedron *pPolyhedron = GeneratePolyhedronFromPlanes( fPlanes, 6, 0.0 );
		if (!pPolyhedron)
		{
			Warning( "CProjectedWallEntity: GeneratePolyhedronFromPlanes failed! Get a save game for me!.\n" );
			return;
		}
		pTempConvex = physcollision->ConvexFromConvexPolyhedron( *pPolyhedron );
		pPolyhedron->Release();
	}

	if (!pTempConvex)
		return;

	m_pWallCollideable = physcollision->ConvertConvexToCollide( &pTempConvex, 1 );
	if (m_pWallCollideable)
	{
		V_strncpy(solid.surfaceprop, "hard_light_bridge", 512);
		solid.params.massCenterOverride = g_PhysDefaultObjectParams.massCenterOverride;
		solid.params.pGameData = this;
		solid.params.mass = g_PhysDefaultObjectParams.mass;
		solid.params.inertia = g_PhysDefaultObjectParams.inertia;
		solid.params.damping = g_PhysDefaultObjectParams.damping;
		solid.params.rotdamping = g_PhysDefaultObjectParams.rotdamping;
		solid.params.rotInertiaLimit = g_PhysDefaultObjectParams.rotInertiaLimit;
		solid.params.pName = g_PhysDefaultObjectParams.pName;
		solid.params.volume = g_PhysDefaultObjectParams.volume;
		solid.params.dragCoefficient = g_PhysDefaultObjectParams.dragCoefficient;
		solid.params.enableCollisions = g_PhysDefaultObjectParams.enableCollisions;
		IPhysicsObject *physModel = PhysModelCreateCustom( this, m_pWallCollideable, vec3_origin, vec3_angle, "hard_light_bridge", true, &solid );
		if (physModel)
		{
			if ( VPhysicsGetObject() )
				VPhysicsDestroyObject();
			VPhysicsSetObject( physModel );
			physModel->RecheckContactPoints();
			if ( physModel->GetCollide() )
			{
				vMaxs = vec3_origin;
				vMins = vec3_origin;
				physcollision->CollideGetAABB(&vMins, &vMaxs, physModel->GetCollide(), vec3_origin, vec3_angle);
				m_vWorldSpace_WallMins = vMins;
				m_vWorldSpace_WallMaxs = vMaxs;

				DevMsg("SET:\nWall Mins: %f %f %f\nWall Maxs: %f %f %f\n", m_vWorldSpace_WallMins.GetX(), m_vWorldSpace_WallMins.GetY(),m_vWorldSpace_WallMins.GetZ(),
						m_vWorldSpace_WallMaxs.GetX(), m_vWorldSpace_WallMaxs.GetY(),m_vWorldSpace_WallMaxs.GetZ());

				// set entity size
				vLocalMins = vMins - vStartPoint;
				vLocalMaxs = vMaxs - vStartPoint;
				SetSize( vLocalMins, vLocalMaxs );

				// Unsure if they actually used this function or not...original code below
				m_flLength = vStartPoint.DistTo(vEndPoint);
				//m_flLength = sqrt(
				//	(((vStartPoint.x - vEndPoint.x) * (vStartPoint.x - vEndPoint.x))
				//	+ ((vStartPoint.y - vEndPoint.y) * (vStartPoint.y - vEndPoint.y)))
				//	+ ((vStartPoint.z - vEndPoint.z) * (vStartPoint.z - vEndPoint.z)));

				// How useless.
				m_flWidth = PROJECTED_WALL_WIDTH;
				m_flHeight = PROJECTED_WALL_HEIGHT;

				CollisionProp()->MarkSurroundingBoundsDirty();
				CollisionProp()->MarkPartitionHandleDirty();
				CollisionProp()->UpdatePartition();
				AngleVectors( GetAbsAngles(), NULL, &vRight, &vUp);
				m_bIsHorizontal = (vUp.z > STEEP_SLOPE || vUp.z < -STEEP_SLOPE) && vRight.z > -STEEP_SLOPE && vRight.z < STEEP_SLOPE;
				DisplaceObstructingEntities();
				m_nNumSegments = ceil( ( m_flLength / m_flSegmentLength ) );
				// FIXME
				//m_PaintPowers.SetCount( ceil( ( m_flLength / m_flSegmentLength ) ) );
				CleansePaint();
			}
		}
	}
}

void CProjectedWallEntity::CheckForPlayersOnBridge( void )
{
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CPortal_Player *pPlayer = (CPortal_Player *)UTIL_PlayerByIndex(i);
		if (pPlayer && pPlayer->GetGroundEntity() == this)
		{
			if ( pPlayer->m_Shared.InCond( PORTAL_COND_TAUNTING ) )
				pPlayer->WasDroppedByOtherPlayerWhileTaunting();
			SetGroundEntity( NULL );
			pPlayer->BridgeRemovedFromUnder();
		}
	}
}

bool CProjectedWallEntity::TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	if ( !m_pWallCollideable )
		return false;

	physcollision->TraceBox( ray, fContentsMask, 0, m_pWallCollideable, vec3_origin, vec3_angle, &tr );
	
	tr.surface.name = "hard_light_bridge";
	tr.surface.flags = 0;
	tr.surface.surfaceProps = CProjectedWallEntity::s_HardLightBridgeSurfaceProps;
	if ( !(tr.fraction >= 1.0) || tr.allsolid || tr.startsolid )
		return true;

	return false;
}

int CProjectedWallEntity::ObjectCaps( void )
{	
	return BaseClass::ObjectCaps() & 1;
}

void CProjectedWallEntity::ComputeWorldSpaceSurroundingBox( Vector *pWorldMins, Vector *pWorldMaxs )
{
	*pWorldMins = m_vWorldSpace_WallMins;
	*pWorldMaxs = m_vWorldSpace_WallMaxs;
	DevMsg("COMPUTED:\nWall Mins: %f %f %f\nWall Maxs: %f %f %f\n", m_vWorldSpace_WallMins.GetX(), m_vWorldSpace_WallMins.GetY(),m_vWorldSpace_WallMins.GetZ(),
														m_vWorldSpace_WallMaxs.GetX(), m_vWorldSpace_WallMaxs.GetY(),m_vWorldSpace_WallMaxs.GetZ());
}

void CProjectedWallEntity::OnPreProjected( void )
{
	 CheckForSettledReflectorCubes();
}

void CProjectedWallEntity::OnProjected( void )
{
	BaseClass::OnProjected();
	ProjectWall();

	m_flParticleUpdateTime = gpGlobals->curtime + 0.5;
}

void CProjectedWallEntity::CleanupWall( void )
{
	if ( VPhysicsGetObject() )
	{
		CPhysicsShadowClone::NotifyDestroy( VPhysicsGetObject(), this );
		VPhysicsDestroyObject();
	}
	if (m_pWallCollideable)
	{
		CPhysicsShadowClone::NotifyDestroy( m_pWallCollideable, this );
#ifndef P2ASW // Doesn't exist in Swarm
		physenv->DestroyCollideOnDeadObjectFlush( m_pWallCollideable );
#else
		physcollision->DestroyCollide( m_pWallCollideable );
#endif
		m_pWallCollideable = NULL;
	}

	m_vWorldSpace_WallMins = m_vWorldSpace_WallMaxs = vec3_origin;
	m_flHeight = m_flWidth = m_flLength = 0.0;
	m_hHitPortal = NULL;
}

float CProjectedWallEntity::GetSegmentLength( void )
{
	return m_flSegmentLength;
}

int CProjectedWallEntity::GetNumSegments( void )
{
	return m_nNumSegments;
}

bool CProjectedWallEntity::IsWallPainted( const Vector &vecPosition )
{
	return CProjectedWallEntity::GetPaintPowerAtPoint( vecPosition ) != NO_POWER;
}

PaintPowerType CProjectedWallEntity::GetPaintPowerAtSegment( int i )
{
	return m_PaintPowers[i];
}

CBaseProjectedEntity *CProjectedWallEntity::CreateNewProjectedEntity( void )
{
	return CProjectedWallEntity::CreateNewInstance();
}

CProjectedWallEntity *CProjectedWallEntity::CreateNewInstance(void)
{
	return (CProjectedWallEntity *)CreateEntityByName( "projected_wall_entity" );
}

void WallPainted( int colorIndex, int nSegment, CBaseEntity *pWall )
{
	CBaseEntity *pEntity = g_TEWallPaintedEvent.m_hEntity;

	CRecipientFilter filter;
	filter.AddAllPlayers();

	if ( pEntity != pWall )
	{
		if ( pWall )
			g_TEWallPaintedEvent.m_hEntity = pWall;
		else
			g_TEWallPaintedEvent.m_hEntity = NULL;
	}

	g_TEWallPaintedEvent.m_colorIndex = colorIndex;
	g_TEWallPaintedEvent.m_nSegment = nSegment;

	g_TEWallPaintedEvent.Create( filter, 0.0 );
}

#ifndef PROJECTED_WALL_EVENT_SERVERONLY
IMPLEMENT_SERVERCLASS_ST( CTEWallPaintedEvent, DT_TEWallPaintedEvent )

	SendPropEHandle( SENDINFO( m_hEntity ) ),
	SendPropInt( SENDINFO( m_colorIndex ) ),
	SendPropInt( SENDINFO( m_nSegment ) ),

END_SEND_TABLE();
#endif

CTEWallPaintedEvent::~CTEWallPaintedEvent()
{
	m_hEntity = NULL;
}


CTEWallPaintedEvent g_TEWallPaintedEvent("WallPaintedEvent");

#endif // NO_PROJECTED_WALL
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/**********************************************************************
FILE NAME: SmHeal.cpp

PURPOSE: 
   Contains popular high level "C" type functions that heal.

GENERAL NOTES: 
    High level functions may assume some input parameters 
    for ease of use. For maximum flexibility, related functions 
    can be found in SMLib, etc
**********************************************************************/

#include "StdAfx.h"

#include "SmApiHeal.h"
#include <SmBrep.h>
#include <SmFace.h>
#include <SmEdge.h>
#include <SmVertex.h>
#include <SmExtent3d.h>
#include <SmMemory.h>

/*******************************************************************//**
PURPOSE --- Heal a brep to fix common topological/geometric defects

NOTES ---   Runs the full healer sequence (SM_HO_ALL).

***********************************************************************/
SmApiStatus SmApiHealBrep
(
    SmBrep*  pBrep                         ///< [in/out]: Pointer to brep                                             <br>
)
{

    if( pBrep == NULL )
        return SM_ERR_INVALID_INPUT;

    SmStatus stat = pBrep->HealBrep( SM_HO_ALL );

    return stat;
}

/*******************************************************************//**
PURPOSE --- Squeeze a tiny face away: delete it and glue its vertices and
    edges together where they lie within dTol3d, so the faces around it meet
    at a vertex (or along an edge) instead.

NOTES ---   For the sliver a fillet leaves where its rails meet: a face only
    a little wider than the zone tolerance between rounds that should meet.
    The face must belong to pBrep and fit in a box whose diagonal is at most
    dTol3d. The vertex left gets a tolerance (and its edges and faces) that
    covers the corners it replaces. pBrep is changed in place; on failure, or
    when the result is not sound (topology, still a closed solid), it is
    restored.

***********************************************************************/
SmApiStatus SmApiSqueezeFace
(
    SmBrep*  pBrep,                        ///< [in/out]: Pointer to brep                                             <br>
    SmFace*  pFace,                        ///< [in]: Face of pBrep to squeeze away                                   <br>
    double   dTol3d                        ///< [in]: Largest gap between its vertices to glue together              <br>
)
{
    if( pBrep == NULL || pFace == NULL || pFace->GetBrep() != pBrep || !(dTol3d > 0.0) )
        return SM_ERR_INVALID_INPUT;

    SmExtent3d sBox;
    SER( pFace->CalculateTightBoundingBox( sBox ) );
    if( sBox.GetMin().DistanceBetween( sBox.GetMax() ) > dTol3d )
        return SM_ERR_INVALID_INPUT;

    const SmContext & crContext = *pBrep->GetContext();
    const SmBoolean bWasSolid = pBrep->IsManifoldSolid();
    SmBrep* pBackup = new (crContext) SmBrep( *pBrep, NULL, NULL );
    SmObjDelete sCleanBackup( pBackup );

    // where its corners are: what the vertex they become must reach
    SmTArray<SmVertex*> sCorners;
    pFace->GetVertices( sCorners );
    SmTArray<SmPoint3d> sCornerPnts;
    for( ULONG ii = 0; ii < sCorners.GetSize(); ii++ )
        sCornerPnts.Add( sCorners[ii]->GetPoint() );

    SmXSectTol3d sTol( dTol3d );
    SmStatus eStat;
    {
        SmTemporaryChangeValue<SmBoolean> sEnableEdit( pBrep->m_bEditingEnabled, TRUE );
        eStat = pBrep->SqueezeFace( pFace, &sTol, NULL, NULL, NULL );
        // the face's vertices are glued in pairs: an edge shorter than the tolerance can be
        // left between them; squeeze it into one vertex half way
        for( ULONG lPass = 0; eStat == SM_SUCCESS && lPass < 16; lPass++ )
        {
            SmTArray<SmEdge*> sEdges;
            pBrep->GetEdges( sEdges );
            SmEdge* pShort = NULL;
            for( ULONG ii = 0; ii < sEdges.GetSize() && pShort == NULL; ii++ )
            {
                SmVertex* pV1 = sEdges[ii]->GetVertex();
                SmVertex* pV2 = sEdges[ii]->GetOtherVertex( pV1 );
                if( pV1 != pV2 && pV1->GetPoint().DistanceBetween( pV2->GetPoint() ) <= dTol3d
                    && sBox.GetMid().DistanceBetween( pV1->GetPoint() ) <= 2.0 * dTol3d )
                    pShort = sEdges[ii];
            }
            if( pShort == NULL )
                break;
            SmVertex* pKeep = pShort->GetVertex();
            SmPoint3d sMid = ( pKeep->GetPoint() + pShort->GetOtherVertex( pKeep )->GetPoint() ) / 2.0;
            eStat = pBrep->SqueezeEdge( pShort, pKeep );
            if( eStat == SM_SUCCESS )
                pKeep->SetPoint( sMid );
        }
        // trim curves made again where squeezing removed them
        if( eStat == SM_SUCCESS )
            eStat = pBrep->HealBrep( SM_HO_ALL );
        // the vertex left reaches the corners it replaces: its tolerance and its edges' and
        // faces' cover the gap (the rounds around it do not quite meet, that is why the face
        // was there)
        if( eStat == SM_SUCCESS )
        {
            SmTArray<SmVertex*> sVerts;
            pBrep->GetVertices( sVerts );
            for( ULONG ii = 0; ii < sVerts.GetSize(); ii++ )
            {
                SmVertex* pV = sVerts[ii];
                if( sBox.GetMid().DistanceBetween( pV->GetPoint() ) > 2.0 * dTol3d )
                    continue;
                double dGap = 0.0;
                for( ULONG jj = 0; jj < sCornerPnts.GetSize(); jj++ )
                    dGap = smos_Max( dGap, pV->GetPoint().DistanceBetween( sCornerPnts[jj] ) );
                SmZoneTol3d sGapTol( 3.0 * dGap );  // (checks take half of it each way)
                pV->SetTolerance( sGapTol );
                SmTArray<SmEdge*> sVEdges;
                pV->GetEdges( sVEdges );
                for( ULONG jj = 0; jj < sVEdges.GetSize(); jj++ )
                    sVEdges[jj]->SetTolerance( sGapTol );
                SmTArray<SmFace*> sVFaces;
                pV->GetFaces( sVFaces );
                for( ULONG jj = 0; jj < sVFaces.GetSize(); jj++ )
                    sVFaces[jj]->SetTolerance( sGapTol );
            }
        }
    }
    // topology sound and still closed; the geometric level 2 checks vertex/face gaps against the
    // Brep's own tolerance, which the gap left between rounds that do not quite meet exceeds
    if( eStat == SM_SUCCESS && ( !pBrep->AssertValid( NULL, SM_LEVEL_1 ) || pBrep->IsManifoldSolid() != bWasSolid ) )
        eStat = SM_ERR;
    if( eStat != SM_SUCCESS )
    {
        SmTemporaryChangeValue<SmBoolean> sEnableEdit( pBrep->m_bEditingEnabled, TRUE );
        if( pBrep->RemoveAllTopology() == SM_SUCCESS )
            pBrep->MergeBrep( *pBackup );
        return eStat;
    }
    return SM_SUCCESS;
}

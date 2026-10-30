// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/**********************************************************************
FILE NAME: SmFillets.cpp

PURPOSE: 
    Contains popular high level "C" type functions for filletting

GENERAL NOTES: 
    High level functions may assume some input parameters 
    for ease of use. For maximum flexibility, related functions 
    can be found in SmPrimitiveCreation
**********************************************************************/

#include "StdAfx.h"

#include "SmApiFillets.h"
#include "SmApiGeneral.h"
#include <SmApiTypes.h>
#include "SmFilletExecutive.h"
#include "SmFilletStandardSolver.h"
#include "SmFeatureExecutive.h"
#include <SmBSplineSurface.h>

#if SM_DEBUG_CODE
static const SmVector3d s_kBlue(0, 0, 1);
#endif

/*******************************************************************//**
PURPOSE ---   

NOTES --- 

***********************************************************************/
/*******************************************************************//**
PURPOSE --- Fillet all the edges of a brep with a linear cross section.  

USAGE NOTES --- 

***********************************************************************/
SmApiStatus SmApiChamferFillet
(
    SmBrep*  pBrep,                   ///< [in ]: Pointer to brep
    double  dDistance                ///< [in ]: Distance of linear fillet
)
{
#if SM_DEBUG_CODE
  SmBoolean bDebugMe = FALSE;
  if(bDebugMe)
  {
    SmApiDraw( pBrep, NULL, 1, 1 );
  }
#endif

  if( !pBrep || !SmFilletExecutive::IsRadiusSupported( dDistance, pBrep->GetTolerance() ) )
    return SM_ERR_INVALID_INPUT;

  const SmContext* pContext = SmApiGetOrCreateContext();

  // Creates circular cross section fillet
  SmFilletSolver * pFS = NULL;
  SmLinearCrossSectionFSG sFSGLinear;

  // Define self-intersection handler for edge fillets
  double dStepBackFactor = 1.0;
  SmMakeSurfaceBlendSIH sSIH( dStepBackFactor );

  SmFilletExecutive sFilExec( *pContext, pBrep );
  sFilExec.SetSelfIntersectionHandler( &sSIH );
  sFilExec.SetDoGlobalMerge( TRUE );

  SmTArray<SmEdge*> sEdges;
  pBrep->GetEdges( sEdges );

  for(ULONG i = 0; i < sEdges.GetSize(); i++)
  {
    SmEdge * pE = sEdges[i];

    SmEdgeuse *pEU = pE->GetBlendEdgeuse();
    if(pEU == NULL)
      continue;

    pFS = new (*pContext) SmConstantDistanceFS( *pContext, pBrep->GetTolerance(), 30.0*SM_PI / 180.0, 2.0*SM_PI / 180.0, dDistance, pEU );

    pFS->SetFilletSurfaceGenerator( &sFSGLinear );

    sFilExec.LoadFilletSolver( pFS );
  }

  SER( sFilExec.CreateFilletCorners() );

  SmTArray<SmFilletCorner*> & rCorners = sFilExec.GetFilletCorners();
  for(ULONG j = 0; j < rCorners.GetSize(); j++)
  {
    rCorners[j]->SetBevel( TRUE );
  }

  SER( sFilExec.DoFilleting() );

#ifdef SM_ASSERT_VALID
  SM_ASSERT_VALID( pBrep );
#endif

#if SM_DEBUG_CODE
  if(bDebugMe)
  {
    SmApiDraw( pBrep, &s_kBlue, 1, 0 );
  }
#endif


  return(SM_SUCCESS);

} // End SmChamferFillet


/*******************************************************************//**
PURPOSE --- Fillet all the edges of a brep with circular cross section.  

USAGE NOTES --- 

***********************************************************************/
SmApiStatus SmApiCircularFillet
(
    SmBrep*  pBrep,                   ///< [in ]: Pointer to brep
    double  dRadius                  ///< [in ]: Radius of circular fillet
)
{
#if SM_DEBUG_CODE
  SmBoolean bDebugMe = FALSE;
  if(bDebugMe)
  {
    SmApiDraw( pBrep, NULL, 1, 1 );
  }
#endif

  if( !pBrep || !SmFilletExecutive::IsRadiusSupported( dRadius, pBrep->GetTolerance() ) )
    return SM_ERR_INVALID_INPUT;

  const SmContext* pContext = SmApiGetOrCreateContext();
  double dTol = pBrep->GetTolerance();

  // Creates circular cross section fillet
  SmCircularCrossSectionFSG sFSGCircular( FALSE, 1.0e-3 );

  // Define self-intersection handler for edge fillets
  double dStepBackFactor = 1.0;
  SmMakeSurfaceBlendSIH sSIH( dStepBackFactor );

  SmFilletExecutive sFilExec( *pContext, pBrep );
  sFilExec.SetSelfIntersectionHandler( &sSIH );
  sFilExec.SetDoGlobalMerge( TRUE );

  SmTArray<SmEdge*> sEdges;
  pBrep->GetEdges( sEdges );

  for(ULONG i = 0; i < sEdges.GetSize(); i++)
  {
    SmEdge * pE = sEdges[i];

    SmEdgeuse *pEU = pE->GetBlendEdgeuse();
    if(pEU == NULL)
      continue;

    SmConstantRadiusFS * pFS = new(*pContext) SmConstantRadiusFS( *pContext, dTol, 30.0*SM_PI / 180.0, 2.0*SM_PI / 180.0, dRadius, pEU );

    pFS->SetFilletSurfaceGenerator( &sFSGCircular );

    sFilExec.LoadFilletSolver( pFS );
  }

  SER( sFilExec.CreateFilletCorners() );

  SER( sFilExec.DoFilleting() );

  // This operation requires stitching for some reason
  // DoFilleting should be examined so that it results in a valid brep
  pBrep->StitchAndOrient();

#ifdef SM_CHECK_VALIDITY
  SM_ASSERT_VALID( pBrep );
#endif

#if SM_DEBUG_CODE
  if(bDebugMe)
  {
    SmApiDraw( pBrep, &s_kBlue, 1, 0 );
  }
#endif


  return(SM_SUCCESS);

} // End SmCircularFillet

// TRUE when pFace is a fillet (its rail edges are tangent to the faces it joins).
static SmBoolean sm_IsFilletFace(SmFace* pFace)
{
    SmBoolean bIsFillet = FALSE;
    ULONG lCrossSection = 0, lSolverType = 0;
    SmSurfParamType eRailDir = SM_SP_U;
    double dRadius = 0, dDist = 0;
    SmTArray<SmEdge*> sRailMin, sRailMax;
    const SmStatus idStat = pFace->IdentifyFillet(SM_ZONE_TOL_3D, 5.0, TRUE, FALSE,  // require tangent rails
                                                  bIsFillet, lCrossSection, lSolverType, eRailDir,
                                                  dRadius, dDist, sRailMin, sRailMax);
    return idStat == SM_SUCCESS && bIsFillet;
}

// The faces sharing an edge with pFace.
static void sm_NeighbourFaces(SmFace* pFace, SmTArray<SmFace*>& rNeighbours)
{
    rNeighbours.ReSet();
    SmTArray<SmEdge*> sEdges;
    pFace->GetEdges(sEdges);
    for (ULONG ii = 0; ii < sEdges.GetSize(); ii++)
    {
        SmTArray<SmFace*> sEdgeFaces;
        sEdges[ii]->GetFaces(sEdgeFaces);
        for (ULONG jj = 0; jj < sEdgeFaces.GetSize(); jj++)
        {
            if (sEdgeFaces[jj] != pFace)
                rNeighbours.AddUnique(sEdgeFaces[jj]);
        }
    }
}

// The fillets among rFaces and their corner faces: non-planar faces joining only fillets, three or
// more (a boss side between two is not a corner).
static void sm_FilletsAndCorners(const SmTArray<SmFace*>& rFaces, SmTArray<SmFace*>& rFeatureFaces)
{
    rFeatureFaces.ReSet();
    for (ULONG ii = 0; ii < rFaces.GetSize(); ii++)
    {
        if (sm_IsFilletFace(rFaces[ii]))
            rFeatureFaces.Add(rFaces[ii]);
    }
    for (ULONG ii = 0; ii < rFaces.GetSize(); ii++)
    {
        if (rFeatureFaces.IsIn(rFaces[ii]) || rFaces[ii]->GetSurface()->IsPlanar(SM_ZONE_TOL_3D))
            continue;
        SmTArray<SmFace*> sNeighbours;
        sm_NeighbourFaces(rFaces[ii], sNeighbours);
        SmBoolean bCorner = sNeighbours.GetSize() >= 3;
        for (ULONG jj = 0; jj < sNeighbours.GetSize() && bCorner; jj++)
            bCorner = rFeatureFaces.IsIn(sNeighbours[jj]);
        if (bCorner)
            rFeatureFaces.Add(rFaces[ii]);
    }
}

static ULONG sm_IndexOf(const SmTArray<SmFace*>& rFaces, SmFace* pFace)
{
    ULONG lIndex = 0;
    rFaces.FindElement(pFace, lIndex);
    return lIndex;
}

// Removes rFaces from pWork, one feature per group of them that touch along edges, the faces around
// each extended over it. pWork is a scratch copy: on failure it may be left half changed.
static SmStatus sm_DefeatureFaces(SmBrep* pWork, const SmTArray<SmFace*>& rFaces)
{
    SmFeatureExecutive sExecutive(*pWork->GetContext(), *pWork);
    SmTArray<SmFeature*> sFeatures;
    SmTArray<SmFace*> sGrouped;
    for (ULONG ii = 0; ii < rFaces.GetSize(); ii++)
    {
        if (sGrouped.IsIn(rFaces[ii]))
            continue;
        SmTArray<SmFace*> sGroup;
        sGroup.Add(rFaces[ii]);
        sGrouped.Add(rFaces[ii]);
        for (ULONG jj = 0; jj < sGroup.GetSize(); jj++)
        {
            SmTArray<SmFace*> sNeighbours;
            sm_NeighbourFaces(sGroup[jj], sNeighbours);
            for (ULONG kk = 0; kk < sNeighbours.GetSize(); kk++)
            {
                if (rFaces.IsIn(sNeighbours[kk]) && !sGrouped.IsIn(sNeighbours[kk]))
                {
                    sGroup.Add(sNeighbours[kk]);
                    sGrouped.Add(sNeighbours[kk]);
                }
            }
        }
        SmFeature* pFeature = sExecutive.AddFeature(sGroup, SM_RB_EXTEND_SURFACE, SM_RB_FROM_OTHER_BREP);
        if (pFeature == NULL)
            return SM_ERR;
        sFeatures.Add(pFeature);
    }

    // DoDefeature rebuilds pWork in place; the feature breps it returns are ours to delete.
    SmBrep* pBase = NULL;
    SmTArray<SmBrep*> sFeatureBreps;
    SmObjsDelete<SmBrep*> sCleanFeatures(&sFeatureBreps);
    SER(sExecutive.DoDefeature(pBase, &sFeatureBreps));
    for (ULONG ii = 0; ii < sFeatures.GetSize(); ii++)
    {
        if (!sFeatures[ii]->GetBaseWasApplied())
            return SM_ERR;
    }

    // Rejoin the faces the extended surfaces split.
    SmTemporaryChangeValue<SmBoolean> sEnableEdit(pWork->m_bEditingEnabled, TRUE);
    return pWork->RemoveTopologicalEdgesAndVertices();
}

// TRUE when pAfter, made from pBefore, is sound: the kernel's own checks pass, and a closed solid
// stayed one.
static SmBoolean sm_IsSound(SmBrep* pBefore, SmBrep* pAfter)
{
    return pAfter->AssertValid(NULL, SM_LEVEL_2) && (!pBefore->IsManifoldSolid() || pAfter->IsManifoldSolid());
}

// pBrep becomes pWork; restored from a copy (best effort) if that fails.
static SmStatus sm_Replace(SmBrep* pBrep, SmBrep* pWork)
{
    SmBrep* pBackup = new (*pBrep->GetContext()) SmBrep(*pBrep, NULL, NULL);
    SmObjDelete sCleanBackup(pBackup);

    SmTemporaryChangeValue<SmBoolean> sEnableEdit(pBrep->m_bEditingEnabled, TRUE);
    SmStatus eReplaceStat = pBrep->RemoveAllTopology();
    if (eReplaceStat == SM_SUCCESS)
        eReplaceStat = pBrep->MergeBrep(*pWork);
    if (eReplaceStat != SM_SUCCESS && pBrep->RemoveAllTopology() == SM_SUCCESS)
        pBrep->MergeBrep(*pBackup);
    return eReplaceStat;
}

// The faces of rFeatureFaces that touch rFaces through a chain of them, rFaces among them.
static void sm_GroupsOf(const SmTArray<SmFace*>& rFaces, const SmTArray<SmFace*>& rFeatureFaces, SmTArray<SmFace*>& rGroups)
{
    rGroups = rFaces;
    for (ULONG ii = 0; ii < rGroups.GetSize(); ii++)
    {
        SmTArray<SmFace*> sNeighbours;
        sm_NeighbourFaces(rGroups[ii], sNeighbours);
        for (ULONG jj = 0; jj < sNeighbours.GetSize(); jj++)
        {
            if (rFeatureFaces.IsIn(sNeighbours[jj]))
                rGroups.AddUnique(sNeighbours[jj]);
        }
    }
}

// A copy of pBrep to remove the faces at rIndices (in GetFaces() order) from, rRemove those faces
// in it. Faces split along an edge though they lie on one surface (as a CAD program may leave
// them after a union) are joined first: at such an edge a fillet's corner has a fourth edge,
// which DeleteFilletFace cannot extend. Not when that would join one of rRemove away.
static SmBrep* sm_WorkCopy(SmBrep* pBrep, const SmTArray<ULONG>& rIndices, SmTArray<SmFace*>& rRemove)
{
    for (ULONG lJoin = 1; ; lJoin = 0)
    {
        SmBrep* pWork = new (*pBrep->GetContext()) SmBrep(*pBrep, NULL, NULL);
        SmTArray<SmFace*> sFaces;
        pWork->GetFaces(sFaces);
        rRemove.ReSet();
        for (ULONG ii = 0; ii < rIndices.GetSize(); ii++)
            rRemove.Add(sFaces[rIndices[ii]]);
        if (!lJoin)
            return pWork;
        SmBoolean bKept = FALSE;
        {
            SmTemporaryChangeValue<SmBoolean> sEnableEdit(pWork->m_bEditingEnabled, TRUE);
            if (pWork->RemoveTopologicalEdgesAndVertices() == SM_SUCCESS)
            {
                pWork->GetFaces(sFaces);
                bKept = TRUE;
                for (ULONG ii = 0; ii < rRemove.GetSize() && bKept; ii++)
                    bKept = sFaces.IsIn(rRemove[ii]);
            }
        }
        if (bKept)
            return pWork;
        delete pWork;
    }
}

// The flat faces beside pFillet on planes three times their size. DeleteFilletFace extends the faces
// beside a fillet over it, which goes wrong when it has to extend their surfaces too (a patch made
// just for its face, as a STEP import has them); a body built in the kernel keeps its faces on
// larger surfaces and never needs it.
static SmStatus sm_WidenPlanesBeside(SmFace* pFillet)
{
    SmTArray<SmFace*> sNeighbours;
    sm_NeighbourFaces(pFillet, sNeighbours);
    for (ULONG ii = 0; ii < sNeighbours.GetSize(); ii++)
    {
        SmFace* pFace = sNeighbours[ii];
        SmBSplineSurface* pSrf = SM_CAST_PTR(SmBSplineSurface, pFace->GetSurface());
        if (pSrf == NULL || !pSrf->IsPlanar(SM_ZONE_TOL_3D))
            continue;
        SmExtent2d sDomain = pSrf->GetNaturalUVDomain();
        sDomain.ExpandRelative(3.0);
        SER(pSrf->ExpandBoundarySpan(sDomain.GetMin().x, SM_SP_U));
        SER(pSrf->ExpandBoundarySpan(sDomain.GetMax().x, SM_SP_U));
        SER(pSrf->ExpandBoundarySpan(sDomain.GetMin().y, SM_SP_V));
        SER(pSrf->ExpandBoundarySpan(sDomain.GetMax().y, SM_SP_V));
        SER(pFace->RemoveUVTrimCurves());
        pFace->SetUVDomain(pSrf->GetNaturalUVDomain());
        double dGap;
        SER(pFace->CreateUVTrimCurves(TRUE, NULL, NULL, dGap, dGap, dGap, dGap, dGap, dGap));
    }
    return SM_SUCCESS;
}

// pBrep with the faces at rIndices (in GetFaces() order) removed, as a new Brep (NULL when that
// fails or the result is not sound). With bOneByOne each face goes on its own: deleted, the faces
// beside it extended (DeleteFilletFace); else a face that touches no other of them goes on its own,
// the rest as features (sm_DefeatureFaces), which takes only whole groups: a group may not touch
// a fillet left in place.
static SmBrep* sm_WithoutFaces(SmBrep* pBrep, const SmTArray<ULONG>& rIndices, SmBoolean bOneByOne)
{
    SmTArray<SmFace*> sRemove, sAlone, sTogether;
    SmBrep* pWork = sm_WorkCopy(pBrep, rIndices, sRemove);
    SmObjDelete sCleanWork(pWork);
    for (ULONG ii = 0; ii < sRemove.GetSize(); ii++)
    {
        SmTArray<SmFace*> sNeighbours;
        sm_NeighbourFaces(sRemove[ii], sNeighbours);
        SmBoolean bAlone = TRUE;
        for (ULONG jj = 0; jj < sNeighbours.GetSize() && bAlone; jj++)
            bAlone = !sRemove.IsIn(sNeighbours[jj]);
        if (bOneByOne || bAlone)
            sAlone.Add(sRemove[ii]);
        else
            sTogether.Add(sRemove[ii]);
    }
    SmStatus eStat = SM_SUCCESS;
    if (sAlone.GetSize() > 0)
    {
        SmTemporaryChangeValue<SmBoolean> sEnableEdit(pWork->m_bEditingEnabled, TRUE);
        for (ULONG ii = 0; ii < sAlone.GetSize() && eStat == SM_SUCCESS; ii++)
        {
            eStat = sm_WidenPlanesBeside(sAlone[ii]);
            if (eStat == SM_SUCCESS)
                eStat = pWork->DeleteFilletFace(sAlone[ii]);
        }
        if (eStat == SM_SUCCESS)
            eStat = pWork->StitchAndOrient();
    }
    if (eStat == SM_SUCCESS && sTogether.GetSize() > 0)
        eStat = sm_DefeatureFaces(pWork, sTogether);
    if (eStat != SM_SUCCESS || !sm_IsSound(pBrep, pWork))
        return NULL;
    sCleanWork.Clear();
    return pWork;
}

// pBrep with the faces at rIndices removed as features, all of them (sm_DefeatureFaces), as a new
// Brep; NULL when that fails or the result is not sound.
static SmBrep* sm_DefeaturedCopy(SmBrep* pBrep, const SmTArray<ULONG>& rIndices)
{
    SmTArray<SmFace*> sRemove;
    SmBrep* pWork = sm_WorkCopy(pBrep, rIndices, sRemove);
    SmObjDelete sCleanWork(pWork);
    if (sm_DefeatureFaces(pWork, sRemove) != SM_SUCCESS || !sm_IsSound(pBrep, pWork))
        return NULL;
    sCleanWork.Clear();
    return pWork;
}

/*******************************************************************//**
PURPOSE --- Remove the Brep's fillet faces (faces with tangent rail edges).

USAGE NOTES --- Fillets meeting at rounded corners are removed with their
    corner faces. Works on a copy: pBrep changes only when every fillet is
    removed and the result is sound; else returns SM_ERR and pBrep is as it was.
***********************************************************************/
SmApiStatus SmApiRemoveFillet
(
    SmBrep*  pBrep                         ///< [in/out]: Pointer to brep                                             <br>
)
{
    if( pBrep == NULL )
        return SM_ERR_INVALID_INPUT;

    SmTArray<SmFace*> sFaces, sFeatureFaces;
    pBrep->GetFaces( sFaces );
    sm_FilletsAndCorners( sFaces, sFeatureFaces );
    if( sFeatureFaces.GetSize() == 0 )
        return SM_SUCCESS;

    SmTArray<ULONG> sIndices;
    for( ULONG ii = 0; ii < sFeatureFaces.GetSize(); ii++ )
        sIndices.Add( sm_IndexOf( sFaces, sFeatureFaces[ii] ) );
    SmBrep* pWork = sm_WithoutFaces( pBrep, sIndices, FALSE );
    if( pWork == NULL )
        pWork = sm_DefeaturedCopy( pBrep, sIndices );
    if( pWork == NULL )
        return SM_ERR;
    SmObjDelete sCleanWork( pWork );
    return sm_Replace( pBrep, pWork );
}

/*******************************************************************//**
PURPOSE --- Remove some of the Brep's fillet faces: the faces around them are
    extended over them.

USAGE NOTES --- A fillet that cannot go on its own (it ends on other fillets or
    on a rounded corner) goes with the whole group of fillets and corner faces
    it belongs to. rRemoved gets the indices (in GetFaces()
    order of pBrep before) of every face removed, rFaces' and the ones that had
    to go with them. Works on a copy: pBrep changes only when the result is
    sound; else returns SM_ERR and pBrep is as it was. A face of rFaces that is
    neither a fillet nor a corner face between fillets is SM_ERR_INVALID_INPUT.
***********************************************************************/
SmApiStatus SmApiRemoveFilletFaces
(
    SmBrep*                  pBrep,        ///< [in/out]: Pointer to brep                                             <br>
    const SmTArray<SmFace*>& rFaces,       ///< [in]: Fillet faces of pBrep to remove                                 <br>
    SmTArray<ULONG>&         rRemoved      ///< [out]: Indices of the faces removed                                   <br>
)
{
    rRemoved.ReSet();
    if( pBrep == NULL || rFaces.GetSize() == 0 )
        return SM_ERR_INVALID_INPUT;

    SmTArray<SmFace*> sFaces, sFeatureFaces;
    pBrep->GetFaces( sFaces );
    sm_FilletsAndCorners( sFaces, sFeatureFaces );
    SmTArray<ULONG> sAsked;
    for( ULONG ii = 0; ii < rFaces.GetSize(); ii++ )
    {
        if( !sFeatureFaces.IsIn( rFaces[ii] ) )
            return SM_ERR_INVALID_INPUT;
        sAsked.AddUnique( sm_IndexOf( sFaces, rFaces[ii] ) );
    }
    SmTArray<SmFace*> sGroups;
    sm_GroupsOf( rFaces, sFeatureFaces, sGroups );
    SmTArray<ULONG> sWithGroups;
    for( ULONG ii = 0; ii < sGroups.GetSize(); ii++ )
        sWithGroups.Add( sm_IndexOf( sFaces, sGroups[ii] ) );

    // the fillets asked for on their own; else with the groups they belong to, a fillet alone in
    // its group deleted, the rest as features; else every face of the groups as features
    const SmTArray<ULONG>* pRemoved = &sAsked;
    SmBrep* pWork = sm_WithoutFaces( pBrep, sAsked, TRUE );
    if( pWork == NULL )
    {
        pRemoved = &sWithGroups;
        pWork = sm_WithoutFaces( pBrep, sWithGroups, FALSE );
    }
    if( pWork == NULL )
        pWork = sm_DefeaturedCopy( pBrep, sWithGroups );
    if( pWork == NULL )
        return SM_ERR;
    SmObjDelete sCleanWork( pWork );
    SER( sm_Replace( pBrep, pWork ) );
    rRemoved = *pRemoved;
    return SM_SUCCESS;
}

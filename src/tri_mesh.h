/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations

   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com

   Christoph Kloss, christoph.kloss@cfdem.com
   Copyright 2009-2012 JKU Linz
   Copyright 2012-     DCS Computing GmbH, Linz

   LIGGGHTS is based on LAMMPS
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   This software is distributed under the GNU General Public License.

   See the README file in the top-level directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing authors:
   Christoph Kloss (JKU Linz, DCS Computing GmbH, Linz)
   Philippe Seil (JKU Linz)
------------------------------------------------------------------------- */

#ifndef LMP_TRI_MESH_H
#define LMP_TRI_MESH_H

#include "surface_mesh.h"
#include "atom.h"
#include "math_extra_liggghts.h"
#include <fstream>

#ifdef SUPERQUADRIC_ACTIVE_FLAG
#include "math_extra_liggghts_superquadric.h"
using namespace MathExtraLiggghtsNonspherical;
#endif

namespace LAMMPS_NS
{
  //NP 3 nodes per face, max 5 neighs per face
  typedef SurfaceMesh<3,5> SurfaceMeshBase;

  class TriMesh : public SurfaceMeshBase
  {
      public:

        TriMesh(LAMMPS *lmp);
        virtual ~TriMesh();

        double resolveTriSphereContact(int iPart, int nTri, double rSphere, double *cSphere, double *delta);
        double resolveTriSphereContactBary(int iPart, int nTri, double rSphere, double *cSphere,
                                           double *contactPoint,double *bary);

#ifdef SUPERQUADRIC_ACTIVE_FLAG
        double resolveTriSuperquadricContact(int nTri, double *normal, double *contactPoint, Superquadric particle);
        double resolveTriSuperquadricContact(int nTri, double *normal, double *contactPoint, Superquadric particle, double *bary);

        bool sphereTriangleIntersection(int nTri, double rSphere, double *cSphere);
        int superquadricTriangleIntersection(int nTri, double *point_of_lowest_potential, Superquadric particle);
        double pointToTriangleDistance(int iTri, double *Csphere, double *delta, bool treatActiveFlag, double *bary);
#endif

        bool resolveSameSide(int nTri, double *x0, double *x1);

        bool resolveTriSphereNeighbuild(int nTri, double rSphere, double *cSphere, double treshold);

        int generateRandomOwnedGhost(double *pos);
        int generateRandomSubbox(double *pos);

        virtual int generateRandomOwnedGhostWithin(double *pos,double delta);

      protected:

        double calcArea(int n);
        bool isInElement(double *pos,int i);

      private:

        inline double precision_trimesh()
        { return MultiNodeMesh<3>::precision(); }
        double calcDist(double *cs, double *closestPoint, double *en0);
        double calcDistToPlane(double *p, double *pPlane, double *nPlane);

        double resolveCornerContactBary(int iTri, int iNode, bool obtuse,
                                    double *p, double *delta, double *bary);
        double resolveEdgeContactBary(int iTri, int iEdge, double *p, double *delta, double *bary);
        double resolveFaceContactBary(int iTri, double *p, double *node0ToSphereCenter, double *delta);

  };

  // *************************************
  #include "tri_mesh_I.h"
#ifdef SUPERQUADRIC_ACTIVE_FLAG
  #include "tri_mesh_I_superquadric.h"
#endif

  // *************************************

} /* LAMMPS_NS */
#endif /* TRIMESH_H_ */

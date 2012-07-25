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
   Evan Smuts (U Cape Town, surface velocity rotation)
------------------------------------------------------------------------- */

#include "fix_mesh_surface.h"
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "force.h"
#include "bounding_box.h"
#include "input_mesh_tri.h"
#include "fix_contact_history.h"
#include "fix_neighlist_mesh.h"
#include "multi_node_mesh.h"
#include "modify.h"
#include "comm.h"
#include "math_extra.h"

using namespace LAMMPS_NS;
using namespace FixConst;

#define EPSILON_V 0.00001

FixMeshSurface::FixMeshSurface(LAMMPS *lmp, int narg, char **arg)
: FixMesh(lmp, narg, arg),
  fix_contact_history_(0),
  fix_mesh_neighlist_(0),
  stress_flag_(false),
  velFlag_(false),
  angVelFlag_(false),
  curvature_(0.)
{
    // check if type has been read
    if(atom_type_mesh_ == -1)
       error->fix_error(FLERR,this,"expecting keyword 'type'");

    // parse further args

    bool hasargs = true;
    while(iarg_ < narg && hasargs)
    {
      hasargs = false;
      if (strcmp(arg[iarg_],"surface_vel") == 0) {
          if (narg < iarg_+4) error->fix_error(FLERR,this,"not enough arguments");
          iarg_++;
          velFlag_ = true;
          vSurf_[0] = force->numeric(arg[iarg_++]);
          vSurf_[1] = force->numeric(arg[iarg_++]);
          vSurf_[2] = force->numeric(arg[iarg_++]);
          hasargs = true;
      } else if (strcmp(arg[iarg_],"surface_ang_vel") == 0) {
          if (narg < iarg_+11) error->fix_error(FLERR,this,"not enough arguments");
          iarg_++;
          angVelFlag_ = true;
          if(strcmp(arg[iarg_++],"origin"))
              error->fix_error(FLERR,this,"expecting keyword 'origin' after 'rotation'");
          origin_[0] = force->numeric(arg[iarg_++]);
          origin_[1] = force->numeric(arg[iarg_++]);
          origin_[2] = force->numeric(arg[iarg_++]);
          if(strcmp(arg[iarg_++],"axis"))
              error->fix_error(FLERR,this,"expecting keyword 'axis' after definition of 'origin'");
          axis_[0] = force->numeric(arg[iarg_++]);
          axis_[1] = force->numeric(arg[iarg_++]);
          axis_[2] = force->numeric(arg[iarg_++]);
          if(strcmp(arg[iarg_++],"omega"))
              error->fix_error(FLERR,this,"expecting keyword 'omega' after definition of 'axis'");
          // positive omega give anti-clockwise (CCW) rotation
          omegaSurf_ = force->numeric(arg[iarg_++]);
          hasargs = true;
      } else if (strcmp(arg[iarg_],"curvature") == 0) {
          if (narg < iarg_+2) error->fix_error(FLERR,this,"not enough arguments");
          iarg_++;
          curvature_ = force->numeric(arg[iarg_++]);
          if(curvature_ < 0. || curvature_ > 60)
            error->fix_error(FLERR,this,"0° < curvature < 60° required");
          curvature_ = cos(curvature_*M_PI/180.);
      } else if(strcmp(style,"mesh/surface") == 0) {
          char *errmsg = new char[strlen(arg[iarg_])+20];
          sprintf(errmsg,"unknown keyword: %s", arg[iarg_]);
          error->fix_error(FLERR,this,errmsg);
          delete []errmsg;
      }
    }
}

/* ---------------------------------------------------------------------- */

FixMeshSurface::~FixMeshSurface()
{

}

/* ---------------------------------------------------------------------- */

void FixMeshSurface::post_create()
{
    FixMesh::post_create();

    if(curvature_ > 0.)
        triMesh()->setCurvature(curvature_);

    if(velFlag_ && angVelFlag_)
        error->fix_error(FLERR,this,"cannot use 'surface_vel' and 'surface_ang_vel' together");

    if(velFlag_)
        initVel();

    if(angVelFlag_)
        initAngVel();
}

/* ---------------------------------------------------------------------- */

void FixMeshSurface::pre_delete(bool unfixflag)
{
    FixMesh::pre_delete(unfixflag);

    // contact tracker and neighlist are created via fix wall/gran
    if(fix_contact_history_)
      modify->delete_fix(fix_contact_history_->id);
    if(fix_mesh_neighlist_)
      modify->delete_fix(fix_mesh_neighlist_->id);
}

/* ---------------------------------------------------------------------- */

int FixMeshSurface::setmask()
{
    int mask = FixMesh::setmask();
    return mask;
}

/* ---------------------------------------------------------------------- */

void FixMeshSurface::setup_pre_force(int vflag)
{
    FixMesh::setup_pre_force(vflag);

    //NP have to call this in setup_pre_force() because
    //NP mesh neigh list is generated the first time in FixNeighlistMesh::setup_pre_force()
    //NP which comes after FixMesh::setup_pre_force()

    // create neigh list for owned and local elements
    //NP must call this here before first use by fix wall/gran post_force()
    //NP via fix wall/gran setup() - which comes after fix mesh setup()

    //NP must know exact # ghosts when initializing the neighlist
    //NP so call after parallel operations

    if(meshNeighlist())
        meshNeighlist()->initializeNeighlist();

    /*NL*/ //fprintf(screen,"setup fix %s end\n",id);
}

/* ---------------------------------------------------------------------- */

void FixMeshSurface::pre_force(int vflag)
{
    FixMesh::pre_force(vflag);
}

/* ----------------------------------------------------------------------
   reverse comm for mesh
------------------------------------------------------------------------- */

void FixMeshSurface::final_integrate()
{
    FixMesh::final_integrate();
}

/* ----------------------------------------------------------------------
   called from fix wall/gran out of post_create()
------------------------------------------------------------------------- */

void FixMeshSurface::createNeighList()
{
    if(fix_mesh_neighlist_) return;
    char *neighlist_name = new char[strlen(id)+1+10];
    sprintf(neighlist_name,"neighlist_%s",id);

    char **fixarg = new char*[4];
    fixarg[0]= neighlist_name;
    fixarg[1]= (char *) "all";
    fixarg[2]= (char *) "neighlist/mesh";
    fixarg[3]= id;
    modify->add_fix(4,fixarg);

    fix_mesh_neighlist_ =
        static_cast<FixNeighlistMesh*>(modify->find_fix_id(neighlist_name));

    delete []fixarg;
    delete []neighlist_name;

    /*
    //NP make sure a neighbor list is available
    fix_mesh_neighlist_->pre_neighbor();
    fix_mesh_neighlist_->pre_force(0);
    */
}

/* ----------------------------------------------------------------------
   called from fix wall/gran out of post_create()
------------------------------------------------------------------------- */

void FixMeshSurface::createContactHistory(int dnum)
{
    if(fix_contact_history_) return;

    // create a contact tracker for the mesh
    char *contacthist_name = new char[strlen(id)+1+8];
    char *my_id = new char[strlen(id)+1];
    sprintf(contacthist_name,"tracker_%s",id);
    sprintf(my_id,"%s",id);

    char dnum_char[10];
    sprintf(dnum_char,"%d",dnum);

    char **fixarg = new char*[6];
    fixarg[0] = contacthist_name;
    fixarg[1] = (char *) "all";
    fixarg[2] = (char *) "contacthistory/mesh";
    fixarg[3] = (char *) "mesh";
    fixarg[4] = my_id;
    fixarg[5]= dnum_char;

    modify->add_fix(6,fixarg);

    fix_contact_history_ = static_cast<FixContactHistory*>(modify->find_fix_id(contacthist_name));

    delete []fixarg;
    delete []contacthist_name;
    delete []my_id;
}

/* ----------------------------------------------------------------------
   sets mesh velocity for conveyor model
------------------------------------------------------------------------- */

void FixMeshSurface::initVel()
{
    double conv_vel[3];
    int size, nVec;
    double scp, tmp[3], facenormal[3], ***v_node;
    vectorCopy3D(vSurf_,conv_vel);
    double conv_vSurf_mag = vectorMag3D(conv_vel);

    // register new mesh property v
    mesh()->prop().addGlobalProperty< VectorContainer<double,3> >("v","comm_none","frame_invariant","restart_no");
    mesh()->prop().setGlobalProperty< VectorContainer<double,3> >("v",conv_vel);

    // register new element property v - error if exists already via moving mesh
    mesh()->prop().addElementProperty<MultiVectorContainer<double,3,3> >("v","comm_none","frame_invariant","restart_no");
    size = mesh()->prop().getElementProperty<MultiVectorContainer<double,3,3> >("v")->size();
    nVec = mesh()->prop().getElementProperty<MultiVectorContainer<double,3,3> >("v")->nVec();

    v_node = mesh()->prop().getElementProperty<MultiVectorContainer<double,3,3> >("v")->begin();

    // set mesh velocity
    TriMesh *trimesh = triMesh();
    for (int i = 0; i < size; i++)
    {
        trimesh->surfaceNorm(i,facenormal);
        scp = vectorDot3D(conv_vel,facenormal);
        vectorScalarMult3D(facenormal,scp,tmp);
        for(int j = 0; j < nVec; j++)
        {
            vectorSubtract3D(conv_vel,tmp,v_node[i][j]);
            if(vectorMag3D(v_node[i][j]) > 0.)
            {
                vectorScalarDiv3D(v_node[i][j],vectorMag3D(v_node[i][j]));
                vectorScalarMult3D(v_node[i][j],conv_vSurf_mag);
            }
        }
    }
}

/* ----------------------------------------------------------------------
   sets mesh velocity for rotation model
------------------------------------------------------------------------- */

void FixMeshSurface::initAngVel()
{
    int size, nVec;
    double rot_origin[3],rot_axis[3],rot_omega;
    vectorCopy3D(origin_,rot_origin);
    vectorCopy3D(axis_,rot_axis);
    rot_omega = omegaSurf_;
    double tmp[3], scp, unitAxis[3], tangComp[3], Utang[3], surfaceV[3];
    double node[3],facenormal[3], magAxis, magUtang, ***v_node;

    // register new element property v - error if exists already via moving mesh
    mesh()->prop().addElementProperty<MultiVectorContainer<double,3,3> >("v","comm_none","frame_invariant","restart_no");
    size = mesh()->prop().getElementProperty<MultiVectorContainer<double,3,3> >("v")->size();
    nVec = mesh()->prop().getElementProperty<MultiVectorContainer<double,3,3> >("v")->nVec();

    size = mesh()->prop().getElementProperty<MultiVectorContainer<double,3,3> >("v")->size();
    nVec = mesh()->prop().getElementProperty<MultiVectorContainer<double,3,3> >("v")->nVec();
    v_node = mesh()->prop().getElementProperty<MultiVectorContainer<double,3,3> >("v")->begin();

    // calculate unit vector of rotation axis
    magAxis = vectorMag3D(rot_axis);
    vectorScalarDiv3D(rot_axis,magAxis,unitAxis);

    TriMesh *trimesh = triMesh();
    for (int i = 0; i < size; i++)
    {
        trimesh->surfaceNorm(i,facenormal);
        // number of nodes per face (3)
        for(int j = 0; j < nVec; j++)
        {
            // position of node - origin of rotation (to get lever arm)
            trimesh->node(i,j,node);
            vectorSubtract3D(node,rot_origin,surfaceV);

            // lever arm X rotational axis = tangential component
            vectorCross3D(surfaceV,unitAxis,tangComp);

            // multiplying by omega scales the tangential component to give tangential velocity
            vectorScalarMult3D(tangComp,-rot_omega,Utang);

            // EPSILON is wall velocity, not rotational omega
            if(vectorMag3D(Utang) < EPSILON_V)
                error->fix_error(FLERR,this,"Rotation velocity too low");

            scp = vectorDot3D(Utang, facenormal);
            vectorScalarMult3D(facenormal,scp,tmp);

            // removes components normal to wall
            vectorSubtract3D(Utang,tmp,v_node[i][j]);
            magUtang = vectorMag3D(Utang);

            if(vectorMag3D(v_node[i][j]) > 0.)
            {
               vectorScalarDiv3D(v_node[i][j],vectorMag3D(v_node[i][j]));
               vectorScalarMult3D(v_node[i][j],magUtang);
            }
        }
    }
}
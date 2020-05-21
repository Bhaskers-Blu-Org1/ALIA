// jhcLocalOcc.cpp : builds and maintains local occupancy map around robot
//
// Written by Jonathan H. Connell, jconnell@alum.mit.edu
//
///////////////////////////////////////////////////////////////////////////
//
// Copyright 2020 IBM Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// 
///////////////////////////////////////////////////////////////////////////

#include "Interface/jhcMessage.h"      // common video

#include "Environ/jhcLocalOcc.h"


///////////////////////////////////////////////////////////////////////////
//                      Creation and Initialization                      //
///////////////////////////////////////////////////////////////////////////

//= Default destructor does necessary cleanup.

jhcLocalOcc::~jhcLocalOcc ()
{
}


//= Default constructor initializes certain values.

jhcLocalOcc::jhcLocalOcc ()
{
  dbg = 0;
  Defaults();
  Reset();
}


///////////////////////////////////////////////////////////////////////////
//                         Processing Parameters                         //
///////////////////////////////////////////////////////////////////////////

//= Parameters used for ground obstacle map.
// ipp, zhi, and zlo are member variables of base class jhcOverhead3D

int jhcLocalOcc::env_params (const char *fname)
{
  jhcParam *ps = &eps;
  int ok;

  ps->SetTag("occ_env", 0);
  ps->NextSpecF( &dej,    96.0, "Distance to map edge (in)");     // 8 ft
  ps->NextSpecF( &ipp,     0.3, "XY resolution (in)");
  ps->NextSpecF( &hat,     4.0, "Max height over camera (in)");
  ps->NextSpecF( &zhi,     4.0, "Wall start height (in)");      
  ps->NextSpecF( &zlo,    -4.0, "Sensing below surface (in)");
  ps->NextSpecF( &fbump,   2.5, "Max floor deviation (in)");

  ps->NextSpec4( &drop,  100,   "Object area to ignore (pel)");
  ps->NextSpec4( &hole,  500,   "Floor hole to ignore (pel)");
  ok = ps->LoadDefs(fname);
  ps->RevertAll();
  return ok;
}


//= Parameters used for retricting Kinect sensing cone FOV.

int jhcLocalOcc::geom_params (const char *fname)
{
  jhcParam *ps = &gps;
  int ok;

  ps->SetTag("occ_geom", 0);
  ps->NextSpecF( &dlf,    3.0, "Trim beam left (deg)");
  ps->NextSpecF( &drt,    5.5, "Trim beam right (deg)");
  ps->NextSpecF( &dtop,   4.5, "Trim beam top (deg)");
  ps->NextSpecF( &dbot,   1.0, "Trim beam bot (deg)");
  ps->Skip();
  ps->NextSpecF( &rside,  8.0, "Robot half width (in)");

  ps->NextSpecF( &rfwd,  15.0, "Fwd robot protrusion (in)");
  ps->NextSpecF( &rback, 14.0, "Rear robot extension (in)");
  ok = ps->LoadDefs(fname);
  ps->RevertAll();
  return ok;
}


//= Parameters used for controlling what parts of map are known.

int jhcLocalOcc::conf_params (const char *fname)
{
  jhcParam *ps = &kps;
  int ok;

  ps->SetTag("occ_conf", 0);
  ps->NextSpecF( &wmat, 36.0,  "Doormat width (in)");
  ps->NextSpecF( &hmat, 24.0,  "Doormat height (in)");
  ps->NextSpecF( &tmat,  2.0,  "Minimum before forget (sec)");
  ps->NextSpecF( &umat,  0.80, "Min fraction known");
  ps->NextSpecF( &kmat,  0.85, "Max fraction known");
  ps->Skip();

  ps->NextSpecF( &temp,  5.0,  "Moving obj decay (sec)");
  ps->NextSpecF( &fade, 30.0,  "Confidence decay (sec)");
  ok = ps->LoadDefs(fname);
  ps->RevertAll();
  return ok;
}


//= Parameters used for finding clear traverable areas.

int jhcLocalOcc::nav_params (const char *fname)
{
  jhcParam *ps = &nps;
  int ok;

  ps->SetTag("occ_nav", 0);
  ps->NextSpecF( &pad,     1.5, "Robot clearance (in)");
  ps->NextSpecF( &lead,   18.0, "Path scan length (in)");
  ps->NextSpecF( &veer,   15.0, "Veer search (deg)");
  ps->Skip();
  ps->NextSpecF( &block,   4.0, "Min path length (in)");
  ps->NextSpecF( &detour,  2.0, "Min goal advance (in)");

  ps->NextSpecF( &stime,   0.3, "Backup delay (sec)");
  ps->NextSpecF( &btime,   2.0, "Max backup time (sec)");
  ok = ps->LoadDefs(fname);
  ps->RevertAll();
  return ok;
}


///////////////////////////////////////////////////////////////////////////
//                           Parameter Bundles                           //
///////////////////////////////////////////////////////////////////////////

//= Read all relevant defaults variable values from a file.

int jhcLocalOcc::Defaults (const char *fname)
{
  int ok = 1;

  ok &= env_params(fname);
  ok &= geom_params(fname);
  ok &= conf_params(fname);
  ok &= nav_params(fname);
  return ok;
}


//= Write current processing variable values to a file.

int jhcLocalOcc::SaveVals (const char *fname) const
{
  int ok = 1;

  ok &= eps.SaveVals(fname);
  ok &= gps.SaveVals(fname);
  ok &= kps.SaveVals(fname);
  ok &= nps.SaveVals(fname);
  return ok;
}


///////////////////////////////////////////////////////////////////////////
//                              Main Functions                           //
///////////////////////////////////////////////////////////////////////////

//= Reset state for the beginning of a sequence.

void jhcLocalOcc::Reset ()
{
  int cm, ct;

  // set up instantaneous projection
  mw = 2.0 * dej;
  mh = 2.0 * dej;
  x0 = dej;
  y0 = dej;
  ztab0 = 0.0;
  jhcOverhead3D::Reset();

  // set image sizes and clear basic maps
  dev.SetSize(map);
  bad.SetSize(map);
  obst.InitSize(map);
  conf.InitSize(map);

  // confidence timing and pixel values
  rate = 30.0;
  cwait = ROUND(rate * fade / 255.0);
  cm = ROUND(rate * fade / cwait);
  ct = ROUND(rate * temp / cwait);
  cmax = BOUND(cm);
  ctmp = BOUND(ct);
  ccnt = 0;

  // robot position and orientation and travel history
  rx = 0.0;
  ry = 0.0;
  raim = 0.0;
  nh = 0;
  fill = 0;

  // oriented local maps and navigation
  set_spin(veer);
  stuck = 0;
  trip = 1;
}


//= Get ready to accept new depth data after robot moves.
// "fwd" and "lf" are base motion in previous direction, then "dr" rotation 
// allows robot travel in fractional pixels without blurring map
// takes around 0.9ms (max 1.5ms) at 2.7GHz when moving

int jhcLocalOcc::AdjustMaps (double fwd, double lf, double dr)
{
  double rads = D2R * (raim + 90.0), c = cos(rads), s = sin(rads);
  int shx, shy;

  // map maintenance tasks
  if (++ccnt >= cwait)                    
  {
    jhcLUT::Offset(conf, conf, -1);    
    OverGate(obst, obst, conf, 0, 0);    // erase if unobserved
    ccnt = 0;
  }

  // figure out new robot position (inches and degrees)
  adj_hist(fwd, lf, dr);
  rx += c * fwd - s * lf; 
  ry += s * fwd + c * lf;
  raim += dr;

  // see if map needs integral shift
  shx = (int)(-rx / ipp);
  shy = (int)(-ry / ipp);
  if ((shx == 0) && (shy == 0))
    return 0;

  // move maps and adjust robot position
  Shift(obst, shx, shy);
  Shift(conf, shx, shy);
  rx += ipp * shx;
  ry += ipp * shy;
  return 1;
}


//= Move robot position history points to account for new position.
// "fwd" and "lf" are base motion in previous direction, then "dr" rotation 
// maintains trajectory relative to robot (primarily for Tail() function)

void jhcLocalOcc::adj_hist (double fwd, double lf, double dr)
{
  double x, y, rads = -D2R * dr, c = cos(rads), s = sin(rads);
  int i, j;

  // add current robot location
  xhist[fill] = 0.0;
  yhist[fill] = 0.0;

  // move all old locations
  for (i = 1; i < nh; i++)
  {
    j = ((fill + 300) - i) % 300;      // modulo neg is neg!
    x = xhist[j] - lf;
    y = yhist[j] - fwd;
    xhist[j] = x * c - y * s;
    yhist[j] = x * s + y * c;
  }

  // increment count and rotate circular array
  nh = __min(nh + 1, 300);
  fill = (fill + 1) % 300;
}
 

//= Update floor map based on depth image taken by camera with given pose.
// "d16" is frontal depth map from Kinect, should call SetOptics if using Kinect2
// "pos" is camera position wrt map origin (y forward, x to right, z up)
// "dir" is camera angles in form of: (pan tilt roll) not pointing vector
// floor is within "fbump" of fitted plane and is generally traversable area 
// walls are map >= "zhi" (up to "hat" over camera) and define shape of environment
// other non-zero map areas are likely obstacles (permanent or temporary)
// takes about 8.9ms at 2.7GHz

int jhcLocalOcc::RefineMaps (const jhcImg& d16, const jhcMatrix& pos, const jhcMatrix& dir) 
{
  jhcMatrix p2(4), d2(4);
  int ok;

  // convert camera position using current map orientation 
  p2.RotPan3(pos, raim);
  p2.IncVec3(rx, ry, 0.0);
  d2.RelVec3(dir, raim, 0.0, 0.0);
  SetCam(0, p2, d2, 1.2 * dej);

  // get actual heights above surface and mark missing floor area
  map.FillArr(0);
  expect_floor(map);
  Reproject(map, d16, 0, 0, pos.Z() + hat, 0);

  // find deviations from planar floor fit to get obstructions
  ok = PlaneDev(dev, map, fbump, 2.0 * fbump);
  if (ok <= 0)
  {
    LimitMax(dev, map, 2);                       // valid depth as below floor
    Threshold(bad, map, 254);                    // only very tall stuff blocks
  }
  else
  {
    MarkTween(dev, map, 1, 1, 1);                // missing depth cannot fit     
    BandGate(bad, map, dev, 178, 78);            // all non-floor items block
  }
  RemSmall(bad, bad, 0.0, drop);                   
  
  // combine new scan with existing map and robot footprint
  mixin_scan(obst, conf, bad, dev);
  block_bot(obst, conf);
  RemSmall(bad, obst, 0.0, drop, 100);           // erase small problems
  erase_blips(obst, bad);

  // analyze travel directions and check doormat
  build_spin(obst);
  fresh = known_ahead(conf);
  return 1;
}


//= Find expected floor area and set height to lowest value (i.e. missing floor).
// finds corners of sensing area at floor height, assumes roll = 0
// similar to jhcOverhead3D::Footprint but more accurate

void jhcLocalOcc::expect_floor (jhcImg& dest) const
{
  double hh = 0.5 * hfov, lrads = D2R * (hh - dlf), rrads = D2R * (hh - drt);
  double hv = 0.5 * vfov, trads = D2R * (hv - dtop), brads = D2R * (hv - dbot);
  double prads = D2R * p0[0], c = cos(prads), s = sin(prads), mrads = D2R * t0[0];
  double cx0 = x0 + cx[0], cy0 = y0 + cy[0], dz = cz[0] - ztab, ppi = 1.0 / ipp;
  double hyp0, ej0, off0, bx0, by0, lfw0, rtw0, swx, swy, sex, sey;
  double hyp1, ej1, off1, bx1, by1, lfw1, rtw1, nwx, nwy, nex, ney;
  double tx, ty, fx;

  // find middle of bottom of beam, punt if angle too shallow
  hyp0 = -dz / sin(mrads - brads);
  ej0 = rmax[0] / cos(brads);
  if ((hyp0 < 0.0) || (hyp0 > ej0))
    return;
  off0 = sqrt(hyp0 * hyp0 - dz * dz);
  if ((mrads - brads) < (D2R * -90.0))           // backward facing
    off0 = -off0;

  // find left and right corners of bottom beam edge
  bx0 = cx0 + off0 * c;
  by0 = cy0 + off0 * s;
  lfw0 = hyp0 * tan(lrads);
  rtw0 = hyp0 * tan(rrads);
  swx = ppi * (bx0 - lfw0 * s);
  swy = ppi * (by0 + lfw0 * c);
  sex = ppi * (bx0 + rtw0 * s);
  sey = ppi * (by0 - rtw0 * c); 

  // find middle of top of beam, else use far end of sensing cone
  hyp1 = -dz / sin(mrads + trads);
  ej1 = rmax[0] / cos(trads);
  if ((hyp1 < 0.0) || (hyp1 > ej1))
  {
    // find top edge of beam then backtrack to floor orthogonally wrt tilt
    tx = ej1 * cos(mrads + trads);
    ty = ej1 * sin(mrads + trads) + dz;
    fx = tx + ty * tan(mrads);
    hyp1 = sqrt(fx * fx + dz * dz);
  }
  off1 = sqrt(hyp1 * hyp1 - dz * dz);

  // find left and right corners of top beam edge
  bx1 = cx0 + off1 * c;
  by1 = cy0 + off1 * s;
  lfw1 = hyp1 * tan(lrads);
  rtw1 = hyp1 * tan(rrads);
  nwx = ppi * (bx1 - lfw1 * s);
  nwy = ppi * (by1 + lfw1 * c); 
  nex = ppi * (bx1 + rtw1 * s);
  ney = ppi * (by1 - rtw1 * c); 

  // mark area inside corners
  FillPoly4(dest, nwx, nwy, nex, ney, sex, sey, swx, swy, 1);
}


//= Add non-floor things as permanent obstacles to unknown areas, else mark as temporary.
// <pre>
//                         OLD
//           | obst  temp  miss  flr  none
//    NEW    | 255   200   128    50     0
//  ---------+-----------------------------
//  obst 255 | 255   200   255   200   255
//  miss 128 | 255   200   128    50   128
//  flr   50 |  50    50    50    50    50 
//  none   0 | 255   200   128    50     0
//
// </pre>
// if new flr  -> flr
// if new miss -> miss if miss or none
// if new obst -> temp if temp or floor, else obst

void jhcLocalOcc::mixin_scan (jhcImg& obs, jhcImg& cf, const jhcImg& junk, const jhcImg& flat) const
{
  int x, y, rw = junk.RoiW(), rh = junk.RoiH(), sk = obs.RoiSkip(junk);
  const UC8 *j = junk.RoiSrc(), *f = flat.RoiSrc();
  UC8 *m = obs.RoiDest(junk), *c = cf.RoiDest(junk);

  for (y = rh; y > 0; y--, m += sk, c += sk, j += sk, f += sk)
    for (x = rw; x > 0; x--, m++, c++, j++, f++)
      if ((*f >= 78) && (*f <= 178))               // NEW FLOOR (overrides all)
      {
        *m = 50;                                   // blue = floor 
        *c = cmax;
      }
      else if (*f == 1)                            // NEW MISS (weakest feature)
      {
        if (*m <= 1)
        {
          *m = 128;                                // green = missing 
          *c = cmax;
        }
      }
      else if (*j > 0)                             // NEW OBST (temp or fixed)
      {
        if ((*m == 50) || (*m == 200))
        {
          *m = 200;                                // red = temporary 
          *c = __min(*c, ctmp);                    // fades fast after first found
        }
        else
        {
          *m = 255;                                // white = fixed 
          *c = cmax;
        }
      }
      else if (*f > 1)
        *c = cmax;                                 // floor fitting failed
}


//= Set area corresponding to robot (with padding) in rotated map to be floor.
// needs 1 pixel extra padding all around to guarantee clr_paths succeeds 

void jhcLocalOcc::block_bot (jhcImg& obs, jhcImg& cf) const
{
  double len = (rfwd + rback + 2.0 * pad) / ipp + 2.0, wid = 2.0 * (rside + pad) / ipp + 2.0; 
  double off = 0.5 * (rfwd - rback) / ipp, ang = raim + 90.0, rads = D2R * ang;
  double rx0 = (rx + x0) / ipp + off * cos(rads), ry0 = (ry + y0) / ipp + off * sin(rads);

  BlockRot(obs, rx0, ry0, len, wid, ang, 50);
  BlockRot( cf, rx0, ry0, len, wid, ang, cmax);
}


//= Get rid of small problems in combined map.

void jhcLocalOcc::erase_blips (jhcImg& obs, const jhcImg& junk) const
{
  int x, y, rw = junk.RoiW(), rh = junk.RoiH(), sk = obs.RoiSkip(junk);
  const UC8 *j = junk.RoiSrc();
  UC8 *m = obs.RoiDest(junk);

  for (y = rh; y > 0; y--, m += sk, j += sk)
    for (x = rw; x > 0; x--, m++, j++)
      if ((*m > 100) && (*j < 255))
        *m = 0;
}


///////////////////////////////////////////////////////////////////////////
//                           Synthetic Sensors                           //
///////////////////////////////////////////////////////////////////////////

//= Robot heading associated with beam having certain deviation from zero.
// heading is robot-relative (0 = forward)

double jhcLocalOcc::Angle (int dev) const
{
  int d = __max(-ndir, __min(dev, ndir - 1));

  return(d * 180.0 / ndir);
}


//= Give collision-free travel distance along some sensor beam direction.
// can clamp negative values to zero if desired

double jhcLocalOcc::Path (int dev, int pos) const
{
  int d = __max(-ndir, __min(dev, ndir - 1));
  double r = dist[ndir + d];

  if (pos > 0)
    return __max(0.0, r);
  return r;
}


//= Give the possible travel distance straight forward until some final offset.
// robot should be end + pad away after travel

double jhcLocalOcc::Ahead (double end) const
{
  return __max(0.0, dist[ndir] - end);
}


//= Give the possible travel distance straight backward until some final offset.
// robot should be end + pad away after travel

double jhcLocalOcc::Behind (double end) const
{
  return __max(0.0, dist[0] - end);
}


//= Make up a bunch of local maps for various robot orientations.

void jhcLocalOcc::set_spin (double da)
{
  double s = rside + pad, f = __max(rfwd, rback) + lead + pad;
  int i, nd, sz = ROUND(2.0 * sqrt(s * s + f * f) / ipp) + 3;
 
  // figure out number of orientations and pixel size
  nd = ROUND(180.0 / da) & 0xFE;
  nd = __min(nd, 18);
  if ((nd == ndir) && (sz == spin[0].XDim()))
    return;

  // make individual maps
  for (i = 0; i < nd; i++)
    spin[i].SetSize(sz, sz, 1);
  ndir = nd;
}


//= Fill in forward and backward drivable distances at various angles.
// assumes motion is turning in place by some angle followed by driving straight
// <pre>
// given ndir = 12 (hnd = 6):
//
// spin images
//   abs =  0                      hnd                     ndir
//   dev = -hnd                     0                      +hnd
//   idx =  0   1   2   3   4   5   6   7   8   9  10  11  (12)
//   ang = -90 -75 -60 -45 -30 -15  0  15  30  45  60  75
//
// dist readings
//   abs =   0                           hnd                     ndir                  ndir+hnd                 2*ndir
//   dev = -ndir                        -hnd                       0                     +hnd                    +ndir
//   idx =   0    1    2    3    4    5   6   7   8   9   10  11  12  13  14  15  16  17  18  19  20  21  22  23  (24)
//   ang = -180 -165 -150 -135 -120 -105 -90 -75 -60 -45 -30 -15   0  15  30  45  60  75  90 105 120 135 150 165
//   src =   B6   B7   B8   B9  B10  B11  F0  F1  F2  F3  F4  F5  F6  F7  F8  F9 F10 F11  B0  B1  B2  B3  B4  B5  
//
// </pre>
// takes about 1.6ms for 15 deg spacing with 18" lookahead @ 0.3" resolution

void jhcLocalOcc::build_spin (const jhcImg& env) 
{
  double ang, step = 180.0 / ndir;
  int dev, hnd = ndir / 2, nd2 = 2 * ndir;

  // rotate local portion of map and measure robot free path length
  ang = raim - hnd * step;
  for (dev = -hnd; dev < hnd; dev++, ang += step)
  {
    rigid_samp(spin[hnd + dev], env, -ang);
    clr_paths(dist[ndir + dev], dist[(nd2 + dev) % nd2], spin[hnd + dev]);
  }
  dist[ndir] = __max(0.0, dist[ndir]);
  dist[0]    = __max(0.0, dist[0]);

  // find range of reachable orientations (deviations rt0 to lf1 are okay)
  for (rt0 = -1; rt0 >= -ndir; rt0--)
    if ((dist[ndir + rt0] < 0.0) || (dist[nd2 + rt0] < 0.0))
      break;
  rt0++;
  for (lf1 = 1; lf1 < ndir; lf1++)
    if ((dist[ndir + lf1] < 0.0) || (dist[lf1] < 0.0))
       break;
  lf1--;
}


//= Sample main map into smaller map after recentering and rotating.
// variant of jhcResize::Rigid without source pixel check or scaling

void jhcLocalOcc::rigid_samp (jhcImg& dest, const jhcImg& src, double degs) const
{
  double cx = 0.5 * dest.XLim(), cy = 0.5 * dest.YLim(), rads = D2R * degs;
  double rx0 = (rx + x0) / ipp, ry0 = (ry + y0) / ipp, c = cos(rads), s = sin(rads);
  int isx, isx0 = ROUND(65536.0 * (rx0 - cx * c - cy * s)), is = ROUND(65536.0 * s);
  int isy, isy0 = ROUND(65536.0 * (ry0 + cx * s - cy * c)), ic = ROUND(65536.0 * c);
  int x, y, w = dest.XDim(), h = dest.YDim(), dln = dest.Line();
  UC8 *d, *d0 = dest.PxlDest();

  for (y = h; y > 0; y--, d0 += dln, isx0 += is, isy0 += ic)
  {
    isx = isx0;
    isy = isy0;
    d = d0;
    for (x = w; x > 0; x--, d++, isx += ic, isy -= is)
      *d = (UC8) src.ARef((isx + 32768) >> 16, (isy + 32768) >> 16);
  }
}


//= Fill in forward and backward drivable distances at various angles.
// distances are in inches straight forward or straight reverse in map
// marks up projection with scanned path (for debugging)
// returns 1 for valid data, 0 if robot cannot exist in given view

int jhcLocalOcc::clr_paths (double& fwd, double& rev, jhcImg& view) const
{
  double cx = 0.5 * view.XLim(), cy = 0.5 * view.YLim(), scan = lead / ipp;
  double rs = (rside + pad) / ipp, rb = (rback + pad) / ipp, rf = (rfwd + pad) / ipp;
  int yrev = (int) floor(cy - rb - scan), ymid = ROUND(cy), yfwd = (int) ceil(cy + rf + scan); 
  int xlf = (int) floor(cx - rs), xrt = (int) ceil(cx + rs), rw = xrt - xlf + 1;
  int x, y, sk = view.RoiSkip(rw), ln = view.Line();
  UC8 *s0, *s;

  // scan forward from middle of robot
  x = 0;
  s = view.RoiDest(xlf, ymid);
  for (y = ymid; (y <= yfwd) && (x <= 0); y++, s += sk)
    for (x = rw; x > 0; x--, s++)
      if (*s > 80)
        break;
      else
        *s = 230;                          // yellow
  fwd = ((y - 1) - (cy + rf)) * ipp;
  fwd = __min(fwd, lead);

  // scan backward from middle of robot
  x = 0;
  s0 = view.RoiDest(xlf, ymid - 1);
  for (y = ymid - 1; (y >= yrev) && (x <= 0); y--, s0 -= ln)
    for (s = s0, x = rw; x > 0; x--, s++)
      if (*s > 80)
        break;
      else
        *s = 180;                          // orange
  rev = ((cy - rb) - (y + 1)) * ipp;
  rev = __min(rev, lead);
  return 1;
}


//= See what fraction of pixels in front of robot are relatively fresh.
// variant of jhcResize::Rigid without source pixel check or scaling

double jhcLocalOcc::known_ahead (const jhcImg& cf) const
{
  double cx = 0.5 * wmat, cy = 0.5 * hmat, off = rfwd + cy, rads = -D2R * raim;
  double c = cos(rads), s = sin(rads), px = rx + x0 + off * s, py = ry + y0 + off * c; 
  int x, isx, isx0 = ROUND(65536.0 * (px - cx * c - cy * s) / ipp), is = ROUND(65536.0 * s);
  int y, isy, isy0 = ROUND(65536.0 * (py + cx * s - cy * c) / ipp), ic = ROUND(65536.0 * c);
  int w = ROUND(wmat / ipp), h = ROUND(hmat / ipp), th = ROUND(rate * tmat / cwait), cnt = 0;

  // find closest source pixel and check against threshold
  for (y = h; y > 0; y--, isx0 += is, isy0 += ic)
  {
    isx = isx0;
    isy = isy0;
    for (x = w; x > 0; x--, isx += ic, isy -= is)
      if (cf.ARef((isx + 32768) >> 16, (isy + 32768) >> 16) >= th)
        cnt++;
  }
  return(cnt / (double)(w * h));
}


///////////////////////////////////////////////////////////////////////////
//                               Navigation                              //
///////////////////////////////////////////////////////////////////////////

//= Determine whether area directly in front of robot is known.
// generally block motion until sure this is clear

bool jhcLocalOcc::Blind () 
{
  if ((trip > 0) && (fresh >= kmat))
    trip = 0;
  else if ((trip <= 0) && (fresh < umat))
    trip = 1;
  return(trip > 0);
}


//= Pick travel distance and heading based on some target (robot advances along y axis).
// returns distance to target (for convenience)

double jhcLocalOcc::Avoid (double& trav, double& head, double tx, double ty) 
{
  double diff, hd0 = R2D * atan2(-tx, ty), d0 = sqrt(tx * tx + ty * ty);
  int swait = ROUND(stime * rate), bstop = swait + ROUND(btime * rate); 

  // pick direction for best robust progress
  head = pick_dir(d0, hd0);
  trav = __min(d0, dist[ndir]);

  // state machine determines if backup needed
  if (head < -180.0)      
  {
    stuck++;
    head = 0.0;                            // no rotation when no path
    jprintf(1, dbg, "NO PATH %d\n", stuck);
  }  
  else if ((stuck > 0) && (head != 0))
  {
    stuck = swait;
    trav = 0.0;                            // don't move again until WELL aligned
    jprintf(1, dbg, "~~~ aligning ~~~\n");
  }
  else 
    stuck = 0;                             // aligned so cancel any backup

  // backup a while else refine normal motion
  if ((stuck > swait) && (stuck < bstop))
  {
    trav = -dist[0];
    jprintf(1, dbg, "  <<< backup %d\n", stuck - swait);
  }
  else
  {
    diff = fabs(head - hd0);
    if (diff > 180.0)
      diff = 360.0 - diff;
    if (diff < (90.0 / ndir))              // aim directly if close
      head = hd0;
    if (fabs(head) > 60.0)                 // no motion if poorly aligned             
      trav = 0.0;
  }
  jprintf(1, dbg, "head = %3.1f, trav = %3.1f\n\n", head, trav);
  return d0;
}


//= Look for reachable direction with best progress toward goal.
// averages over neighboring directions to avoid get stuck next to things
// returns preferred heading angle, or -360 if nothing seems good

double jhcLocalOcc::pick_dir (double td, double ta) const
{
  double prog[36];
  double trads = D2R * ta, dr = PI / ndir, step = 180.0 / ndir, hs = 0.5 * step;
  double rd, len, sum, adv, best = 0.0, win = -360.0;
  int dev;

  // find progress toward goal possible by traveling along each beam
  jprintf(2, dbg, "target %3.1f in @ %3.1f deg : search [%3.1f %3.1f]\n", td, ta, rt0 * step, lf1 * step);
  for (dev = rt0; dev <= lf1; dev++)
  {
    rd = __max(0.0, dist[ndir + dev]);
    len = rd * cos(dev * dr - trads);
    prog[ndir + dev] = __min(len, td);
  }

  // search reachable directions for suitably long paths
  for (dev = rt0; dev <= lf1; dev++)
    if ((dist[ndir + dev] >= block) && (prog[ndir + dev] >= detour))
    {
      // smooth progress over adjacent three beams (if reachable)
      sum = 2.0 * prog[ndir + dev];
      if (dev > rt0)
        sum += prog[ndir + dev - 1];
      if (dev < lf1)
        sum += prog[ndir + dev + 1];
      sum /= 4.0;
      jprintf(3, dbg, "  %4.0f: prog %5.1f, avg %5.1f ***\n", dev * step, prog[ndir + dev], sum);

      // keep beam direction with most progress (or closest toward goal)
      if ((win < -180.0) || (sum > best) || 
          ((sum == best) && (prog[ndir + dev] > adv)))
      {
        best = sum;
        adv = prog[ndir + dev];
        win = dev * step;
      }
    }
  else
    jprintf(3, dbg, "  %4.0f: prog %5.1f beam %5.1f\n", dev * step, prog[ndir + dev], dist[ndir + dev]); 
  jprintf(2, dbg, "  pick_dir = %1.0f degs -> avg %3.1f [prog %3.1f]\n", win, best, adv);
  return win;
}


///////////////////////////////////////////////////////////////////////////
//                           Debugging Graphics                          //
///////////////////////////////////////////////////////////////////////////

//= Get the local region map, possibly rotating so that robot is always pointing upward.

int jhcLocalOcc::LocalMap (jhcImg &dest, int rot) const
{
  double rx0 = (rx + x0) / ipp, ry0 = (ry + y0) / ipp;
  double xc = 0.5 * dest.XLim(), yc = 0.5 * dest.YLim();

  if (!dest.Valid(1))
    return Fatal("Bad image to jhcLocalOcc::MapRot");
  if (rot <= 0)
    return dest.CopyArr(obst);
  return Rigid(dest, obst, -raim, xc, yc, rx0, ry0);
}


//= Get floor confidence value around the robot, possibly rotating so robot point up.

int jhcLocalOcc::Confidence (jhcImg &dest, int rot) const
{
  double rx0 = (rx + x0) / ipp, ry0 = (ry + y0) / ipp;
  double xc = 0.5 * dest.XLim(), yc = 0.5 * dest.YLim();

  if (!dest.Valid(1))
    return Fatal("Bad image to jhcLocalOcc::Confidence");
  if (rot <= 0)
    return dest.CopyArr(obst);
  return Rigid(dest, conf, -raim, xc, yc, rx0, ry0);
}


//= Show area of confidence map used for initial gating of motion. 

int jhcLocalOcc::Doormat (jhcImg& dest, int rot) const
{
  const jhcImg *ref = ((rot > 0) ? &dest : NULL);
  double rx0, ry0, rads = robot_pose(rx0, ry0, ref), len = (rfwd + 0.5 * hmat) / ipp;
  double cx = rx0 + len * cos(rads), cy = ry0 + len * sin(rads);

  if (!dest.Valid(1))
    return Fatal("Bad image to jhcLocalOcc::Doormat");
  RectRot(dest, cx, cy, hmat / ipp, wmat / ipp, R2D * rads, 1, -7); 
  return 1;
}


//= Show cross at robot center.

int jhcLocalOcc::RobotMark (jhcImg& dest, int rot) const
{
  const jhcImg *ref = ((rot > 0) ? &dest : NULL);
  double rx0, ry0;

  if (!dest.Valid(1, 3))
    return Fatal("Bad image to jhcLocalOcc::RobotMark");
  robot_pose(rx0, ry0, ref);
  return Cross(dest, rx0, ry0, 17, 17, 3, -5);  
}


// Show outline of robot body and direction arrow.

int jhcLocalOcc::RobotBody (jhcImg& dest, int rot) const
{
  const jhcImg *ref = ((rot > 0) ? &dest : NULL);
  double off = 0.5 * (rfwd - rback) / ipp, len = (rfwd + rback) / ipp, wid = 2.0 * rside / ipp; 
  double rx0, ry0, rads = robot_pose(rx0, ry0, ref), c = cos(rads), s = sin(rads), sz = 7.0;
  double fx = sz * c, fy = sz * s, dx = sz * s, dy = -sz * c;

  if (!dest.Valid(1, 3))
    return Fatal("Bad image to jhcLocalOcc::RobotBody");
  RectCent(dest, rx0 + off * c, ry0 + off * s, len, wid, R2D * rads, 3, -5);
  DrawLine(dest, rx0 + fx, ry0 + fy, rx0 - fx - dx, ry0 - fy - dy, 3, -5); 
  DrawLine(dest, rx0 + fx, ry0 + fy, rx0 - fx + dx, ry0 - fy + dy, 3, -5); 
  return 1;
}


//= Show robot heading direction as a line.

int jhcLocalOcc::RobotDir (jhcImg& dest, int rot) const
{
  const jhcImg *ref = ((rot > 0) ? &dest : NULL);
  double rx0, ry0, rads = robot_pose(rx0, ry0, ref), len = 1.5 * dest.XDim();

  if (!dest.Valid(1, 3))
    return Fatal("Bad image to jhcLocalOcc::RobotDir");
  return DrawLine(dest, rx0, ry0, rx0 + len * cos(rads), ry0 + len * sin(rads), 3, -6);
}


//= Show distance robot can move in various directions without regard to reachability.
// skips if robot orientation is impossible, no guarantee orientation is reachable

int jhcLocalOcc::Dists (jhcImg& dest, int rot) const
{
  const jhcImg *ref = ((rot > 0) ? &dest : NULL);
  double d, c, s, len, off = rside / ipp, dr = D2R * 180.0 / ndir;
  double rx0, ry0, rads = robot_pose(rx0, ry0, ref) - D2R * 180.0;
  int dev, col, hnd = ndir / 2;

  if (!dest.Valid(1, 3))
    return Fatal("Bad images to jhcLocalOcc::Dists");
  for (dev = -ndir; dev < ndir; dev++, rads += dr)
    if ((d = dist[ndir + dev]) >= 0.0)
    {
      c = cos(rads);
      s = sin(rads);
      len = (rfwd + d) / ipp;
      col = (((dev >= -hnd) && (dev <= hnd)) ? 230 : 180);
      DrawLine(dest, rx0 + off * c, ry0 + off * s, rx0 + len * c, ry0 + len * s, 3, col);
    }
  return 1;
}


//= Show distance robot can move in various directions that it can actually achieve.
// skips all directions the robot cannot turn in place to reach

int jhcLocalOcc::Paths (jhcImg& dest, int rot) const
{
  const jhcImg *ref = ((rot > 0) ? &dest : NULL);
  double d, c, s, len, off = rside / ipp, dr = D2R * 180.0 / ndir;
  double rx0, ry0, rads, rads0 = robot_pose(rx0, ry0, ref) + rt0 * dr;
  int i, dev, dev2, nd2 = 2 * ndir;

  if (!dest.Valid(1, 3))
    return Fatal("Bad images to jhcLocalOcc::Paths");

  // backward travel (might duplicate forward at 180 degs opposite)
  rads = rads0;
  for (dev = rt0; dev <= lf1; dev++, rads += dr)
  {
    i = (nd2 + dev) % nd2;
    dev2 = i - ndir;
    if ((d = dist[i]) >= 0.0)
    {
      c = cos(rads);
      s = sin(rads);
      len = (rfwd + dist[i]) / ipp;
      DrawLine(dest, rx0 - off * c, ry0 - off * s, rx0 - len * c, ry0 - len * s, 3, 180); 
    }
  }

  // forward travel (preferred, so yellow takes precedence)  
  rads = rads0;
  for (dev = rt0; dev <= lf1; dev++, rads += dr)
    if ((d = dist[ndir + dev]) > 0.0)
    {
      c = cos(rads);
      s = sin(rads);
      len = (rfwd + d) / ipp;
      DrawLine(dest, rx0 + off * c, ry0 + off * s, rx0 + len * c, ry0 + len * s, 3, 230);
    }
  return 1;
}


//= Show robot's recent trajectory (only in rotated map).

int jhcLocalOcc::Tail (jhcImg& dest, double secs) const
{
  double x, y, xc = 0.5 * dest.XLim(), yc = 0.5 * dest.YLim(), px = xc, py = yc;
  int i, j, n = ROUND(secs * rate) + 1;

  if (!dest.Valid(1, 3) || !dest.SameSize(map))
    return Fatal("Bad images to jhcLocalOcc::Tail");
  n = __min(n, nh);
  for (i = 1; i < n; i++)
  {
    j = ((fill + 300) - i) % 300;      // modulo neg is neg!
    x = xc + xhist[j] / ipp;
    y = yc + yhist[j] / ipp;
    if ((ROUND(x) != ROUND(px)) || (ROUND(y) != ROUND(py)))
    {
      DrawLine(dest, px, py, x, y, 5, 1);
      px = x;
      py = y;
    }
  }
  return 1;
}


//= Show rough active depth zone of sensor in overhead map image.
// diminishes edges of beam appropriately but ignores roll
// only configured to work for large fixed map images (not spin)

int jhcLocalOcc::ScanBeam (jhcImg& dest) const
{
  double hh = 0.5 * hfov, lf = hh - dlf, rt = hh - drt;
  double a1 = D2R * (p0[0] + lf), ej1 = 1.2 * dej / (ipp * cos(D2R * lf));
  double a2 = D2R * (p0[0] - rt), ej2 = 1.2 * dej / (ipp * cos(D2R * rt));
  double kx0 = (cx[0] + x0) / ipp, ky0 = (cy[0] + y0) / ipp;
  double kx1 = kx0 + ej1 * cos(a1), ky1 = ky0 + ej1 * sin(a1);
  double kx2 = kx0 + ej2 * cos(a2), ky2 = ky0 + ej2 * sin(a2);

  if (!dest.Valid(1, 3) || !dest.SameSize(map))
    return Fatal("Bad images to jhcLocalOcc::ScanBeam");
  DrawLine(dest, kx0, ky0, kx1, ky1, 1, -5);
  DrawLine(dest, kx1, ky1, kx2, ky2, 1, -5);
  DrawLine(dest, kx2, ky2, kx0, ky0, 1, -5);
  return 1;
}


//= Figures out coordinates for robot center and returns heading in radians.
// if dest is non-null then centers robot in image with direction of travel being upward

double jhcLocalOcc::robot_pose (double& rx0, double& ry0, const jhcImg *ref) const
{
  // adjusted position and angle of robot in big map
  if (ref == NULL)
  {
    rx0 = (rx + x0) / ipp;
    ry0 = (ry + y0) / ipp;
    return(D2R * (raim + 90.0));
  }

  // center of image pointed up (typically for spin)
  rx0 = 0.5 * ref->XLim();
  ry0 = 0.5 * ref->YLim();
  return(D2R * 90.0);
}



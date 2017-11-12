#define PJ_LIB__
#include <errno.h>
#include <proj.h>
#include "projects.h"

PROJ_HEAD(laea, "Lambert Azimuthal Equal Area") "\n\tAzi, Sph&Ell";

enum Mode {
    N_POLE = 0,
    S_POLE = 1,
    EQUIT  = 2,
    OBLIQ  = 3
};

struct pj_opaque {
    double sinb1;
    double cosb1;
    double xmf;
    double ymf;
    double mmf;
    double qp;
    double dd;
    double rq;
    double *apa;
    enum Mode mode;
};

#define EPS10   1.e-10
#define NITER   20
#define CONV    1.e-10

static XY e_forward (LP lp, PJ *P) {          /* Ellipsoidal, forward */
    XY xy = {0.0,0.0};
    struct pj_opaque *Q = P->opaque;
    double coslam, sinlam, sinphi, q, sinb=0.0, cosb=0.0, b=0.0;

    coslam = cos(lp.lam);
    sinlam = sin(lp.lam);
    sinphi = sin(lp.phi);
    q = pj_qsfn(sinphi, P->e, P->one_es);

    if (Q->mode == OBLIQ || Q->mode == EQUIT) {
        sinb = q / Q->qp;
        cosb = sqrt(1. - sinb * sinb);
    }

    switch (Q->mode) {
    case OBLIQ:
        b = 1. + Q->sinb1 * sinb + Q->cosb1 * cosb * coslam;
        break;
    case EQUIT:
        b = 1. + cosb * coslam;
        break;
    case N_POLE:
        b = M_HALFPI + lp.phi;
        q = Q->qp - q;
        break;
    case S_POLE:
        b = lp.phi - M_HALFPI;
        q = Q->qp + q;
        break;
    }
    if (fabs(b) < EPS10) {
        proj_errno_set(P, PJD_ERR_TOLERANCE_CONDITION);
        return xy;
    }

    switch (Q->mode) {
    case OBLIQ:
        b = sqrt(2. / b);
        xy.y = Q->ymf * b * (Q->cosb1 * sinb - Q->sinb1 * cosb * coslam);
        goto eqcon;
        break;
    case EQUIT:
        b = sqrt(2. / (1. + cosb * coslam));
        xy.y = b * sinb * Q->ymf;
eqcon:
        xy.x = Q->xmf * b * cosb * sinlam;
        break;
    case N_POLE:
    case S_POLE:
        if (q >= 0.) {
            b = sqrt(q);
            xy.x = b * sinlam;
            xy.y = coslam * (Q->mode == S_POLE ? b : -b);
        } else
            xy.x = xy.y = 0.;
        break;
    }
    return xy;
}


static XY s_forward (LP lp, PJ *P) {           /* Spheroidal, forward */
    XY xy = {0.0,0.0};
    struct pj_opaque *Q = P->opaque;
    double  coslam, cosphi, sinphi;

    sinphi = sin(lp.phi);
    cosphi = cos(lp.phi);
    coslam = cos(lp.lam);
    switch (Q->mode) {
    case EQUIT:
        xy.y = 1. + cosphi * coslam;
        goto oblcon;
    case OBLIQ:
        xy.y = 1. + Q->sinb1 * sinphi + Q->cosb1 * cosphi * coslam;
oblcon:
        if (xy.y <= EPS10) {
            proj_errno_set(P, PJD_ERR_TOLERANCE_CONDITION);
            return xy;
        }
        xy.y = sqrt(2. / xy.y);
        xy.x = xy.y * cosphi * sin(lp.lam);
        xy.y *= Q->mode == EQUIT ? sinphi :
           Q->cosb1 * sinphi - Q->sinb1 * cosphi * coslam;
        break;
    case N_POLE:
        coslam = -coslam;
        /*-fallthrough*/
    case S_POLE:
        if (fabs(lp.phi + P->phi0) < EPS10) {
            proj_errno_set(P, PJD_ERR_TOLERANCE_CONDITION);
            return xy;
        }
        xy.y = M_FORTPI - lp.phi * .5;
        xy.y = 2. * (Q->mode == S_POLE ? cos(xy.y) : sin(xy.y));
        xy.x = xy.y * sin(lp.lam);
        xy.y *= coslam;
        break;
    }
    return xy;
}


static LP e_inverse (XY xy, PJ *P) {          /* Ellipsoidal, inverse */
    LP lp = {0.0,0.0};
    struct pj_opaque *Q = P->opaque;
    double cCe, sCe, q, rho, ab=0.0;

    switch (Q->mode) {
    case EQUIT:
    case OBLIQ:
        xy.x /= Q->dd;
        xy.y *=  Q->dd;
        rho = hypot(xy.x, xy.y);
        if (rho < EPS10) {
            lp.lam = 0.;
            lp.phi = P->phi0;
            return lp;
        }
        sCe = 2. * asin(.5 * rho / Q->rq);
        cCe = cos(sCe);
        sCe = sin(sCe);
        xy.x *= sCe;
        if (Q->mode == OBLIQ) {
            ab = cCe * Q->sinb1 + xy.y * sCe * Q->cosb1 / rho;
            xy.y = rho * Q->cosb1 * cCe - xy.y * Q->sinb1 * sCe;
        } else {
            ab = xy.y * sCe / rho;
            xy.y = rho * cCe;
        }
        break;
    case N_POLE:
        xy.y = -xy.y;
        /*-fallthrough*/
    case S_POLE:
        q = (xy.x * xy.x + xy.y * xy.y);
        if (q == 0.0) {
            lp.lam = 0.;
            lp.phi = P->phi0;
            return (lp);
        }
        ab = 1. - q / Q->qp;
        if (Q->mode == S_POLE)
            ab = - ab;
        break;
    }
    lp.lam = atan2(xy.x, xy.y);
    lp.phi = pj_authlat(asin(ab), Q->apa);
    return lp;
}


static LP s_inverse (XY xy, PJ *P) {           /* Spheroidal, inverse */
    LP lp = {0.0,0.0};
    struct pj_opaque *Q = P->opaque;
    double  cosz=0.0, rh, sinz=0.0;

    rh = hypot(xy.x, xy.y);
    if ((lp.phi = rh * .5 ) > 1.) {
        proj_errno_set(P, PJD_ERR_TOLERANCE_CONDITION);
        return lp;
    }
    lp.phi = 2. * asin(lp.phi);
    if (Q->mode == OBLIQ || Q->mode == EQUIT) {
        sinz = sin(lp.phi);
        cosz = cos(lp.phi);
    }
    switch (Q->mode) {
    case EQUIT:
        lp.phi = fabs(rh) <= EPS10 ? 0. : asin(xy.y * sinz / rh);
        xy.x *= sinz;
        xy.y = cosz * rh;
        break;
    case OBLIQ:
        lp.phi = fabs(rh) <= EPS10 ? P->phi0 :
           asin(cosz * Q->sinb1 + xy.y * sinz * Q->cosb1 / rh);
        xy.x *= sinz * Q->cosb1;
        xy.y = (cosz - sin(lp.phi) * Q->sinb1) * rh;
        break;
    case N_POLE:
        xy.y = -xy.y;
        lp.phi = M_HALFPI - lp.phi;
        break;
    case S_POLE:
        lp.phi -= M_HALFPI;
        break;
    }
    lp.lam = (xy.y == 0. && (Q->mode == EQUIT || Q->mode == OBLIQ)) ?
        0. : atan2(xy.x, xy.y);
    return (lp);
}


static void *destructor (PJ *P, int errlev) {
    if (0==P)
        return 0;

    if (0==P->opaque)
        return pj_default_destructor (P, errlev);

    pj_dealloc (P->opaque->apa);

    return pj_default_destructor(P, errlev);
}


PJ *PROJECTION(laea) {
    double t;
    struct pj_opaque *Q = pj_calloc (1, sizeof (struct pj_opaque));
    if (0==Q)
        return pj_default_destructor (P, ENOMEM);
    P->opaque = Q;
    P->destructor = destructor;

    t = fabs(P->phi0);
    if (fabs(t - M_HALFPI) < EPS10)
        Q->mode = P->phi0 < 0. ? S_POLE : N_POLE;
    else if (fabs(t) < EPS10)
        Q->mode = EQUIT;
    else
        Q->mode = OBLIQ;
    if (P->es != 0.0) {
        double sinphi;

        P->e = sqrt(P->es);
        Q->qp = pj_qsfn(1., P->e, P->one_es);
        Q->mmf = .5 / (1. - P->es);
        Q->apa = pj_authset(P->es);
        if (0==Q->apa)
            return destructor(P, ENOMEM);
        switch (Q->mode) {
        case N_POLE:
        case S_POLE:
            Q->dd = 1.;
            break;
        case EQUIT:
            Q->dd = 1. / (Q->rq = sqrt(.5 * Q->qp));
            Q->xmf = 1.;
            Q->ymf = .5 * Q->qp;
            break;
        case OBLIQ:
            Q->rq = sqrt(.5 * Q->qp);
            sinphi = sin(P->phi0);
            Q->sinb1 = pj_qsfn(sinphi, P->e, P->one_es) / Q->qp;
            Q->cosb1 = sqrt(1. - Q->sinb1 * Q->sinb1);
            Q->dd = cos(P->phi0) / (sqrt(1. - P->es * sinphi * sinphi) *
               Q->rq * Q->cosb1);
            Q->ymf = (Q->xmf = Q->rq) / Q->dd;
            Q->xmf *= Q->dd;
            break;
        }
        P->inv = e_inverse;
        P->fwd = e_forward;
    } else {
        if (Q->mode == OBLIQ) {
            Q->sinb1 = sin(P->phi0);
            Q->cosb1 = cos(P->phi0);
        }
        P->inv = s_inverse;
        P->fwd = s_forward;
    }

    return P;
}


int pj_laea_selftest (void) {return 10000;}

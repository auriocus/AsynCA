#ifndef CASCMD_H
#define CASCMD_H

#include <casdef.h>
#undef INLINE /* conflicting definition from Tcl and EPICS */
#include <tcl.h>
#include "casExport.h"

#include <fdMgr.h>

struct GetRequestEvent {
	Tcl_Event ev;
	casPV *PV;
};

#endif

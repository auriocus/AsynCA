#ifndef CASCMD_H
#define CASCMD_H

#include <casdef.h>
#undef INLINE /* conflicting definition from Tcl and EPICS */
#include <tcl.h>
#include "casExport.h"

#include <fdmgr.h>
#include "tclclass.h"


struct GetRequestEvent {
	Tcl_Event ev;
	casPV *PV;
};

extern "C" {
	static Tcl_ThreadCreateProc EpicsEventLoop;
}

// server instance
class AsynServer : public TclClass {
public:
    AsynServer (ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]);
    ~AsynServer();

	int test(int objc, Tcl_Obj * const objv[]);
	int test2(int objc, Tcl_Obj * const objv[]);

	Tcl_ThreadId mainid;
};

TCLCLASSDECLARE(AsynServer)

// server instance
class AsynPV {
public:
    AsynPV () { }
    ~AsynPV() { }

	Tcl_ThreadId mainid;
};


//TCLCLASSDECLARE(AsynPV)
#endif
